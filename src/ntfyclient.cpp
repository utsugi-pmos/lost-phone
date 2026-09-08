// SPDX-License-Identifier: GPL-2.0-or-later

#include "ntfyclient.h"

#include "ntfyprotocol.h"

#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace
{
constexpr int kBackoffBaseSeconds = 10;
constexpr int kBackoffMaxSeconds = 15 * 60;

// ntfy sends a keepalive event every 45 seconds, so a stream that has said
// nothing for well over two of those is a stream that has quietly died --
// which is what a phone that changed cell or came back from suspend leaves
// behind. Reconnecting is cheap; a channel that looks connected and is not is
// the failure this feature cannot have.
constexpr int kSilenceTimeoutMs = 150 * 1000;
}

NtfyClient::NtfyClient(QObject *parent)
    : QObject(parent)
{
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &NtfyClient::subscribe);
}

QUrl NtfyClient::topicUrl(const QString &topic, const QString &suffix) const
{
    QString base = m_settings.ntfyServer.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return QUrl(base + QLatin1Char('/') + topic + suffix);
}

void NtfyClient::authorise(QNetworkRequest &request) const
{
    // Optional: only a protected topic needs it. A token that is not there must
    // not turn into an "Authorization: Bearer " header with nothing after it,
    // which some servers reject outright.
    if (!m_settings.ntfyToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_settings.ntfyToken.toUtf8());
    }
}

void NtfyClient::configure(const Settings &settings)
{
    const bool changed = settings.ntfyServer != m_settings.ntfyServer
        || settings.ntfyTopic != m_settings.ntfyTopic
        || settings.ntfyToken != m_settings.ntfyToken
        || settings.ntfyEnabled != m_settings.ntfyEnabled;
    const bool wasRunning = m_stream && m_stream->isRunning();

    m_settings = settings;

    if (!settings.enabled || !settings.ntfyEnabled || settings.ntfyServer.isEmpty()
        || settings.ntfyTopic.isEmpty() || settings.ntfyKey.isEmpty()) {
        stop();
        return;
    }
    if (changed || !wasRunning) {
        stop();
        m_failures = 0;
        subscribe();
    }
}

void NtfyClient::stop()
{
    m_retry.stop();
    if (m_stream) {
        m_stream->abort();
        m_stream->deleteLater();
        m_stream = nullptr;
    }
    m_buffer.clear();
    setConnected(false);
}

void NtfyClient::setConnected(bool connected)
{
    if (m_connected == connected) {
        return;
    }
    m_connected = connected;
    Q_EMIT connectedChanged(connected);
}

void NtfyClient::subscribe()
{
    if (m_stream) {
        return;
    }
    // ?since=all would replay the backlog, and replaying orders is how a phone
    // ends up ringing at three in the morning because of something you sent
    // last week. Only what arrives from now on.
    QNetworkRequest request(topicUrl(m_settings.ntfyTopic, QStringLiteral("/json?since=now")));
    authorise(request);
    request.setTransferTimeout(0);  // it is meant to stay open

    m_stream = m_net.get(request);

    // The stream is read line by line as it arrives, not at the end: there is
    // no end, that is the whole point of it.
    connect(m_stream, &QNetworkReply::readyRead, this, [this] {
        if (!m_stream) {
            return;
        }
        setConnected(true);
        m_failures = 0;
        m_buffer += m_stream->readAll();
        int cut;
        while ((cut = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(cut).trimmed();
            m_buffer.remove(0, cut + 1);
            if (!line.isEmpty()) {
                onLine(line);
            }
        }
        // A line that never ends is either a very long message or a broken
        // server; either way it is not worth holding.
        if (m_buffer.size() > 64 * 1024) {
            m_buffer.clear();
        }
        m_retry.start(kSilenceTimeoutMs);  // the silence watchdog, re-armed
    });

    connect(m_stream, &QNetworkReply::finished, this, [this] {
        if (!m_stream) {
            return;
        }
        const QString error = m_stream->error() == QNetworkReply::NoError
            ? QStringLiteral("the server closed the stream")
            : m_stream->errorString();
        m_stream->deleteLater();
        m_stream = nullptr;
        setConnected(false);

        // Backoff. A phone with no coverage retrying in a tight loop flattens
        // itself trying to tell you where it is.
        ++m_failures;
        const int wait = qMin(kBackoffMaxSeconds, kBackoffBaseSeconds * (1 << qMin(m_failures, 6)));
        qInfo() << "lost-phoned: ntfy disconnected (" << error << "), retrying in" << wait << "s";
        m_retry.start(wait * 1000);
    });

    // Nothing heard for far longer than ntfy's own keepalive: treat it as dead
    // and start again.
    m_retry.start(kSilenceTimeoutMs);
}

void NtfyClient::onLine(const QByteArray &line)
{
    // Everything that decides whether this is an order lives in
    // NtfyProtocol::commandFrom, so it can be tested without a server. This
    // only carries the answer.
    const QString order = NtfyProtocol::commandFrom(line, m_settings.ntfyKey);
    if (order.isEmpty()) {
        return;
    }
    qInfo() << "lost-phoned: order by ntfy:" << order.section(QLatin1Char(' '), 0, 0);
    Q_EMIT command(order);
}

void NtfyClient::say(const QString &text)
{
    if (m_settings.ntfyReplyTopic.isEmpty() || m_settings.ntfyServer.isEmpty()) {
        return;
    }
    QNetworkRequest request(topicUrl(m_settings.ntfyReplyTopic));
    authorise(request);
    request.setRawHeader("Title", "Find my phone");
    request.setTransferTimeout(20 * 1000);
    QNetworkReply *reply = m_net.post(request, text.toUtf8());
    connect(reply, &QNetworkReply::finished, reply, [reply] {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "lost-phoned: could not publish to ntfy:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

void NtfyClient::report(const Fix &fix)
{
    say(fix.toSms());
}
