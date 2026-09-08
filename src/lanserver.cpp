// SPDX-License-Identifier: GPL-2.0-or-later

#include "lanserver.h"

#include "dbus.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDebug>
#include <QTcpSocket>

namespace
{
// Fixed, and announced over mDNS anyway, so nothing has to guess it. Well above
// 1024 so the daemon never needs privileges to bind it.
constexpr quint16 kPort = 8479;

const QString kAvahi = QStringLiteral("org.freedesktop.Avahi");
constexpr int kIfaceUnspec = -1;
constexpr int kProtoUnspec = -1;

void respond(QTcpSocket *socket, int code, const QByteArray &body,
             const char *type = "application/json")
{
    // The text goes with its code. Without this, a 403 came out as "403 Not
    // Found" -- which in a log sends you looking for a path that does exist.
    QByteArray reason = "Not Found";
    switch (code) {
    case 200: reason = "OK"; break;
    case 401: reason = "Unauthorized"; break;
    case 403: reason = "Forbidden"; break;
    case 409: reason = "Conflict"; break;
    default: break;
    }
    QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    head += "Content-Type: ";
    head += type;
    head += "; charset=utf-8\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Connection: close\r\n\r\n";
    socket->write(head + body);
    socket->flush();
    socket->disconnectFromHost();
}
}

LanServer::LanServer(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<QList<QByteArray>>();
    connect(&m_server, &QTcpServer::newConnection, this, &LanServer::onConnection);
}

LanServer::~LanServer()
{
    unpublish();
}

bool LanServer::listening() const
{
    return m_server.isListening();
}

void LanServer::setResultProvider(const std::function<QString()> &provider)
{
    m_resultado = provider;
}

void LanServer::setStatusProvider(const std::function<QByteArray()> &provider)
{
    m_status = provider;
}

void LanServer::configure(const Settings &settings)
{
    m_settings = settings;

    const bool wanted = settings.enabled && settings.lanEnabled && !settings.lanToken.isEmpty();
    if (!wanted) {
        if (m_server.isListening()) {
            m_server.close();
            unpublish();
            qInfo() << "lost-phoned: home channel off";
        }
        return;
    }
    if (m_server.isListening()) {
        return;
    }
    if (!m_server.listen(QHostAddress::Any, kPort)) {
        qWarning() << "lost-phoned: could not listen on port" << kPort << ":"
                   << m_server.errorString();
        return;
    }
    publish();
    qInfo() << "lost-phoned: home channel listening on port" << kPort;
}

void LanServer::publish()
{
    if (!m_entryGroup.isEmpty()) {
        return;
    }
    // Two seconds, not the twenty-five that building a QDBusInterface would
    // wait. Avahi may not be up yet, and a phone that stalls here stalls the
    // session that started this daemon.
    const QDBusMessage group =
        Bus::call(QDBusConnection::systemBus(), kAvahi, QStringLiteral("/"),
                  QStringLiteral("org.freedesktop.Avahi.Server"),
                  QStringLiteral("EntryGroupNew"), {}, 2000);
    if (!Bus::ok(group) || group.arguments().isEmpty()) {
        qWarning() << "lost-phoned: could not announce myself over mDNS:" << group.errorMessage();
        return;
    }
    m_entryGroup = group.arguments().constFirst().value<QDBusObjectPath>().path();

    // No TXT record. Anything put in one is broadcast to the whole network in
    // clear, over and over, and nothing here is worth telling the neighbours.
    // The argument list is built by hand, with every type spelled out. Avahi
    // wants `iiussssqaay` and the port is that `q`: a uint16. Handing the
    // arguments to call() directly promoted it to a plain int, and the answer
    // was the least helpful error a bus can give -- "AddService with signature
    // iiussssiaay doesn't exist" -- for a method that exists perfectly well.
    const QVariantList arguments{
        QVariant::fromValue(kIfaceUnspec),
        QVariant::fromValue(kProtoUnspec),
        QVariant::fromValue(quint32(0)),
        QVariant::fromValue(QStringLiteral("Find my phone")),
        QVariant::fromValue(QStringLiteral("_lost-phone._tcp")),
        QVariant::fromValue(QString()),  // domain: whichever
        QVariant::fromValue(QString()),  // host: ours
        QVariant::fromValue(quint16(kPort)),
        QVariant::fromValue(QList<QByteArray>()),
    };
    const QDBusMessage added =
        Bus::call(QDBusConnection::systemBus(), kAvahi, m_entryGroup,
                  QStringLiteral("org.freedesktop.Avahi.EntryGroup"),
                  QStringLiteral("AddService"), arguments, 2000);
    if (!Bus::ok(added)) {
        qWarning() << "lost-phoned: AddService failed:" << added.errorMessage();
        unpublish();
        return;
    }
    Bus::call(QDBusConnection::systemBus(), kAvahi, m_entryGroup,
              QStringLiteral("org.freedesktop.Avahi.EntryGroup"), QStringLiteral("Commit"), {},
              2000);
}

void LanServer::unpublish()
{
    if (m_entryGroup.isEmpty()) {
        return;
    }
    Bus::call(QDBusConnection::systemBus(), kAvahi, m_entryGroup,
              QStringLiteral("org.freedesktop.Avahi.EntryGroup"), QStringLiteral("Free"), {}, 2000);
    m_entryGroup.clear();
}

void LanServer::onConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_partial.remove(socket);
            socket->deleteLater();
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            // Accumulated, NOT re-read from scratch. readAll() drains the
            // socket, so a request whose headers arrive in two TCP segments had
            // its first half thrown away here and the connection then hung for
            // ever waiting for an end-of-headers that had already gone past.
            // Small requests on a quiet LAN almost always arrive in one piece,
            // which is exactly what makes that the worst kind of bug: it works
            // on the desk and fails on the day.
            QByteArray &buffer = m_partial[socket];
            buffer += socket->readAll();

            // A listener that buffered arbitrary input would be a way to eat the
            // memory of a phone whose whole job is to still be alive when you
            // come looking for it.
            if (buffer.size() > 8192) {
                m_partial.remove(socket);
                socket->disconnectFromHost();
                return;
            }

            const int endOfHeaders = buffer.indexOf("\r\n\r\n");
            if (endOfHeaders < 0) {
                return;  // the headers are still arriving
            }

            // And the body may still be arriving too. Acting on a half-read
            // POST would mean acting on a truncated verb -- "son" instead of
            // "sonar" -- which does nothing at all and says nothing about why.
            const int bodyStart = endOfHeaders + 4;
            const int expected = contentLength(buffer.left(endOfHeaders));
            if (buffer.size() - bodyStart < expected) {
                return;
            }

            const QByteArray request = buffer;
            m_partial.remove(socket);
            handle(socket, request);
        });
    }
}

// Content-Length, or zero if there is none. Zero is the right answer for a GET
// and for a malformed header alike: neither has a body worth waiting for.
int LanServer::contentLength(const QByteArray &headers)
{
    for (const QByteArray &raw : headers.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (line.toLower().startsWith("content-length:")) {
            bool ok = false;
            const int value = line.mid(line.indexOf(':') + 1).trimmed().toInt(&ok);
            return (ok && value > 0) ? value : 0;
        }
    }
    return 0;
}

void LanServer::handle(QTcpSocket *socket, const QByteArray &request)
{
    const int endOfHeaders = request.indexOf("\r\n\r\n");
    const QByteArray headers = endOfHeaders < 0 ? request : request.left(endOfHeaders);
    const QByteArray body = endOfHeaders < 0 ? QByteArray() : request.mid(endOfHeaders + 4);

    // Authorisation is decided from the HEADERS ONLY. Scanning the whole
    // request would let a body line spelled "Authorization: ..." take part in
    // the decision, and where the credential is read from should never depend
    // on what the sender put in the body.
    const QByteArray expected = "Bearer " + m_settings.lanToken.toUtf8();
    bool authorised = false;
    QByteArray head;
    const QList<QByteArray> lines = headers.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (i == 0) {
            head = line;
            continue;
        }
        if (line.toLower().startsWith("authorization:")) {
            authorised = line.mid(line.indexOf(':') + 1).trimmed() == expected;
            break;  // the first one decides; a second must not overwrite it
        }
    }

    // The simple door: GET /sonar?clave=...
    //
    // Checked BEFORE rejecting for a missing token, and after the first line of
    // the headers has been pulled out. It is deliberately the dumbest thing that
    // works: anything that can do a GET -- a browser, a home automation button,
    // a watch, a `curl` in a script -- can make the phone ring without knowing
    // what an Authorization header is.
    //
    // The key in the URL ends up in histories and logs. That is why it is its own
    // key, ships off, and can ONLY make it ring and go quiet -- not locate, not
    // lock, not ask for status.
    //
    // That last restriction was added 2026-09-06, when the home channel was given
    // locate permission: the simple door shares a channel with the token, so it
    // would have inherited the permission without anybody deciding it. A key that
    // ends up written in the browser history, in the router log and in a home
    // automation script cannot be the one that opens your coordinates. The token,
    // which travels in a header, is there for that.
    if (!authorised && m_settings.lanSimpleEnabled && !m_settings.lanSimpleKey.isEmpty()
        && head.startsWith("GET /")) {
        const QByteArray ruta = head.mid(4, head.indexOf(' ', 4) - 4).trimmed();
        const int pregunta = ruta.indexOf('?');
        if (pregunta > 0) {
            const QString verbo = QString::fromUtf8(ruta.left(pregunta)).mid(1);
            QString clave;
            const QList<QByteArray> campos = ruta.mid(pregunta + 1).split('&');
            for (const QByteArray &campo : campos) {
                if (campo.startsWith("clave=")) {
                    clave = QString::fromUtf8(QByteArray::fromPercentEncoding(campo.mid(6)));
                }
            }
            const bool puede = verbo == QLatin1String("sonar") || verbo == QLatin1String("ring")
                || verbo == QLatin1String("parar") || verbo == QLatin1String("stop");
            if (!clave.isEmpty() && clave == m_settings.lanSimpleKey && !verbo.isEmpty()) {
                if (!puede) {
                    respond(socket, 403,
                            QByteArray("the simple door only rings and stops; for anything else "
                                       "the token is needed\n"),
                            "text/plain");
                    return;
                }
                Q_EMIT command(verbo);
                const QString motivo = m_resultado ? m_resultado() : QString();
                // Plain text and not JSON: this is read by a person on the
                // browser screen, not a program.
                if (motivo.isEmpty()) {
                    respond(socket, 200,
                            QByteArray("order sent: ") + verbo.toUtf8() + "\n", "text/plain");
                } else {
                    respond(socket, 409,
                            QByteArray("not done: ") + motivo.toUtf8() + "\n", "text/plain");
                }
                return;
            }
        }
    }

    if (!authorised) {
        respond(socket, 401, "{\"error\":\"wrong token\"}");
        return;
    }

    if (head.startsWith("GET /estado")) {
        respond(socket, 200, m_status ? m_status() : QByteArray("{}"));
        return;
    }
    if (head.startsWith("POST /orden")) {
        const QString verb = QString::fromUtf8(body).trimmed();
        if (verb.isEmpty()) {
            respond(socket, 404, "{\"error\":\"no order\"}");
            return;
        }
        // Delivered FIRST and answered AFTER. Before, this answered {"ok":true} as
        // a matter of course, even to an order rejected by permissions or that the
        // daemon did not even understand. An "ok" to something that did not happen
        // is worse than an error: it leaves you looking for the fault in the wrong
        // place.
        Q_EMIT command(verb);
        const QString motivo = m_resultado ? m_resultado() : QString();
        if (motivo.isEmpty()) {
            respond(socket, 200, "{\"ok\":true}");
        } else {
            respond(socket, 409,
                    QByteArray("{\"ok\":false,\"motivo\":\"") + motivo.toUtf8() + "\"}");
        }
        return;
    }
    respond(socket, 404, "{\"error\":\"does not exist\"}");
}
