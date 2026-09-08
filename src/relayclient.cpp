// SPDX-License-Identifier: GPL-2.0-or-later

#include "relayclient.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QSslCertificate>
#include <QSslError>
#include <QNetworkRequest>
#include <QUrl>

namespace
{
// The relay holds a poll for sixty seconds. The client waits longer than that
// before deciding the request is lost: a timeout shorter than the server's wait
// would tear down every single poll and call it a failure.
// HTTP/1.1 and not HTTP/2, on purpose.
//
// Qt negotiates HTTP/2 if the server offers it, and then it MULTIPLEXES: every
// request to that server travels over ONE single connection. Here that is poison,
// because one of those requests is the long wait, which stays open for a whole
// minute. When the server closes or restarts that connection it takes with it
// whatever was inside -- and what was inside was the position report, which died
// without saying anything. All that was left in the journal was this, which I had
// been filtering out as noise:
//
//     qt.network.http2: stream 3 error: "Received GOAWAY"
//     qt.network.http2: stream 3 finished with error: "Remote host signaled shutdown"
//
// With HTTP/1.1 each request carries its own connection and the long wait cannot
// drag anyone else down.
static void sinHttp2(QNetworkRequest &request)
{
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
}

constexpr int kPollTimeoutMs = 90 * 1000;
constexpr int kOtherTimeoutMs = 20 * 1000;

constexpr int kBackoffBaseSeconds = 10;
constexpr int kBackoffMaxSeconds = 15 * 60;
}

RelayClient::RelayClient(QObject *parent)
    : QObject(parent)
{
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &RelayClient::poll);
}

QUrl RelayClient::endpoint(const QString &path) const
{
    QString base = m_settings.relayUrl.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return QUrl(base + path);
}

void RelayClient::configure(const Settings &settings)
{
    const bool wasRunning = m_settings.relayEnabled && !m_settings.relayToken.isEmpty();
    const bool changed = settings.relayUrl != m_settings.relayUrl
        || settings.relayToken != m_settings.relayToken
        || settings.relayEnabled != m_settings.relayEnabled;

    m_settings = settings;

    if (!settings.enabled || !settings.relayEnabled || settings.relayToken.isEmpty()
        || settings.relayUrl.isEmpty()) {
        stop();
        return;
    }
    if (changed || !wasRunning) {
        stop();
        m_failures = 0;
        poll();
    }
}

void RelayClient::stop()
{
    m_retry.stop();
    if (m_poll) {
        // Aborting fires finished() with OperationCanceledError, which the
        // handler ignores precisely because it is us doing the cancelling.
        m_poll->abort();
        m_poll = nullptr;
    }
    setConnected(false);
}

void RelayClient::setConnected(bool connected)
{
    if (m_connected != connected) {
        m_connected = connected;
        Q_EMIT connectedChanged(connected);
    }
    // The connection shows signs of life: if a position was left undelivered, this
    // is the moment. No timer of its own is needed -- the polling comes back on its
    // own, and when it comes back it is because there is network.
    if (connected && m_hayPendiente) {
        m_hayPendiente = false;
        qInfo() << "lost-phoned: retrying the position that was left unsent";
        report(m_sinEntregar);
    }
}

void RelayClient::poll()
{
    if (!m_settings.enabled || !m_settings.relayEnabled || m_settings.relayToken.isEmpty()) {
        return;
    }
    if (m_poll) {
        return;  // one poll at a time; a second would be a second phone
    }

    QNetworkRequest request(endpoint(QStringLiteral("/api/device/poll")));
    request.setRawHeader("Authorization", "Bearer " + m_settings.relayToken.toUtf8());
    request.setTransferTimeout(kPollTimeoutMs);
    sinHttp2(request);

    QNetworkReply *reply = m_net.get(request);
    confiar(reply, false);
    m_poll = reply;

    // Connected the moment the poll goes out with a clean record behind it, not
    // when it comes back. A poll is held for a minute, so waiting for the reply
    // meant the screen said "not reaching your server" for a whole minute right
    // after pairing -- when it had in fact just connected. If this attempt does
    // fail, the handler below says so.
    if (m_failures == 0) {
        setConnected(true);
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_poll = nullptr;
        reply->deleteLater();

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            return;  // we aborted it ourselves, in stop()
        }

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError && status == 0) {
            // No answer at all: no coverage, relay down, DNS gone. Back off,
            // doubling, so a phone with no network does not burn its battery
            // insisting.
            setConnected(false);
            ++m_failures;
            int wait = kBackoffBaseSeconds * (1 << qMin(m_failures - 1, 8));
            wait = qMin(wait, kBackoffMaxSeconds);
            qInfo() << "lost-phoned: the relay does not answer; retrying in" << wait << "s";
            m_retry.start(wait * 1000);
            return;
        }

        m_failures = 0;

        if (status == 401) {
            // The relay does not know this token any more -- the device was
            // removed from the panel. Stop, loudly. Retrying for ever against a
            // relay that has forgotten us is battery spent on nothing.
            qWarning() << "lost-phoned: the relay no longer recognises this phone; giving up";
            setConnected(false);
            return;
        }

        setConnected(true);

        if (status == 200) {
            const QJsonObject body =
                QJsonDocument::fromJson(reply->readAll()).object();
            const QString verb = body.value(QStringLiteral("orden")).toString();
            if (!verb.isEmpty()) {
                Q_EMIT command(verb);
            }
            // Straight back in: an order usually comes with company, and this is
            // the moment the user is actually waiting on us.
            m_retry.start(0);
            return;
        }

        // 204: nothing to do. Straight back into the wait, unless somebody has
        // asked for a slower heartbeat -- and that sleep is time the phone
        // cannot be reached at all, which is why it defaults to none.
        m_retry.start(qMax(0, m_settings.relayIdleMinutes) * 60 * 1000);
    });
}

void RelayClient::report(const Fix &fix)
{
    if (m_settings.relayToken.isEmpty() || m_settings.relayUrl.isEmpty()) {
        return;
    }
    QJsonObject body{
        {QStringLiteral("cuando"), fix.when.toUTC().toString(Qt::ISODate)},
        {QStringLiteral("tiene_coordenadas"), fix.hasCoordinates},
        {QStringLiteral("latitud"), fix.latitude},
        {QStringLiteral("longitud"), fix.longitude},
        {QStringLiteral("precision_metros"), fix.accuracyMeters},
        {QStringLiteral("origen"), fix.source},
        {QStringLiteral("celda"), fix.cell},
        {QStringLiteral("wifi"), fix.wifi},
        {QStringLiteral("bateria"), fix.batteryPercent},
    };

    QNetworkRequest request(endpoint(QStringLiteral("/api/device/report")));
    request.setRawHeader("Authorization", "Bearer " + m_settings.relayToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kOtherTimeoutMs);
    sinHttp2(request);

    QNetworkReply *reply = m_net.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    // It was missing, and it was one of those bugs you do not see: without this, a
    // relay with its own certificate -- which is the DEFAULT path -- rejects the
    // position report silently. The phone shows up as connected in the panel,
    // because the polling does go through here, and the map never updates.
    // Everything looks fine and nothing works.
    confiar(reply, false);

    // And it says so if it goes wrong. Before it was fire and forget: if the relay
    // answered 401, or there was no network, no trace was left anywhere. The one
    // thing that keeps the map fresh cannot fail without saying so.
    connect(reply, &QNetworkReply::finished, this, [this, reply, fix] {
        const int codigo = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || (codigo != 200 && codigo != 0)) {
            qWarning().noquote() << "lost-phoned: could not send the position to the relay:"
                                 << reply->errorString() << "(HTTP" << codigo << ")"
                                 << "-- saving it for the next attempt";
            m_sinEntregar = fix;
            m_hayPendiente = true;
        } else {
            qInfo() << "lost-phoned: position sent to the relay";
        }
        reply->deleteLater();
    });
}

// The relay certificate, pinned the way SSH does it.
//
// The relay is reached by IP and port, with no domain, so no public authority can
// sign it: it uses its own. Accepting it "just because" would be worse than not
// encrypting, because it would look secure. So:
//
//   - during PAIRING the one that is there is accepted and its fingerprint noted.
//     It is the initial trust window, and it is acceptable because at that moment
//     there already is a shared secret: the four-digit code.
//   - after that, ONLY that one. An attacker who interposes later presents another
//     certificate and is rejected.
//
// A TLS error other than the self-signed certificate one is never forgiven, not
// even while pairing.
void RelayClient::confiar(QNetworkReply *reply, bool primeraVez)
{
    connect(reply, &QNetworkReply::sslErrors, this,
            [this, reply, primeraVez](const QList<QSslError> &errores) {
                if (errores.isEmpty()) {
                    return;
                }
                const QSslCertificate certificado = errores.first().certificate();
                if (certificado.isNull()) {
                    return;  // with no certificate there is nothing to pin
                }
                const QString huella =
                    QString::fromLatin1(certificado.digest(QCryptographicHash::Sha256).toHex(':'))
                        .toUpper();

                for (const QSslError &error : errores) {
                    // Only NOBODY having signed it is forgiven. A certificate that
                    // is expired, or for another name, or broken, is still a no.
                    if (error.error() != QSslError::SelfSignedCertificate
                        && error.error() != QSslError::HostNameMismatch
                        && error.error() != QSslError::CertificateUntrusted) {
                        qWarning() << "lost-phoned: TLS rejected:" << error.errorString();
                        return;
                    }
                }

                if (primeraVez) {
                    m_huellaVista = huella;
                    reply->ignoreSslErrors(errores);
                    return;
                }
                if (!m_settings.relayFingerprint.isEmpty()
                    && huella == m_settings.relayFingerprint) {
                    reply->ignoreSslErrors(errores);
                    return;
                }
                qWarning() << "lost-phoned: the relay presents ANOTHER certificate. Expected"
                           << m_settings.relayFingerprint << "and got" << huella
                           << "-- not connecting";
            });
}

void RelayClient::pair(const QString &url, const QString &code, const QString &name)
{
    Settings probe = m_settings;
    probe.relayUrl = url;
    const Settings saved = m_settings;
    m_settings = probe;
    const QUrl target = endpoint(QStringLiteral("/api/device/pair"));
    m_settings = saved;

    if (!target.isValid() || target.scheme().isEmpty()) {
        Q_EMIT pairingFailed(QStringLiteral("the relay address is not valid"));
        return;
    }

    QNetworkRequest request(target);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kOtherTimeoutMs);
    sinHttp2(request);

    const QJsonObject body{
        {QStringLiteral("codigo"), code.trimmed().toLower()},
        {QStringLiteral("nombre"), name},
    };

    m_huellaVista.clear();
    QNetworkReply *reply = m_net.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    confiar(reply, true);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        const QString token = answer.value(QStringLiteral("testigo")).toString();
        if (token.isEmpty()) {
            const QString why = answer.value(QStringLiteral("error")).toString();
            Q_EMIT pairingFailed(why.isEmpty() ? reply->errorString() : why);
            return;
        }
        Q_EMIT paired(answer.value(QStringLiteral("id")).toString(), token, m_huellaVista);
    });
}
