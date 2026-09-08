// SPDX-License-Identifier: GPL-2.0-or-later
//
// The channel for somebody who already self-hosts something.
//
// Our own relay is the right answer for a person who wants one box that does
// this and nothing else. But plenty of people already run ntfy -- it is the
// usual way to get a push notification out of a script -- and telling them to
// stand up a second server to find their phone is a poor trade. This speaks
// their ntfy instead, over plain HTTP, with no extra library.
//
// HOW IT WORKS, AND WHY IT IS ALMOST THE SAME AS THE RELAY
// -------------------------------------------------------
// ntfy has a streaming endpoint: GET /<tema>/json stays open and writes one
// JSON object per line as messages arrive. That is the same shape as our relay
// poll -- an ordinary outgoing request the phone holds open -- so it crosses
// CGNAT for the same reason and needs nothing on the phone but QNetworkAccess.
//
// The answer goes back by POSTing to a SECOND topic, the one you subscribe to
// from the ntfy app on whatever phone you are holding. That is the part that
// makes this pleasant: the position arrives as a push notification, not as a
// page you have to remember to open.
//
// THE TOPIC IS NOT A PASSWORD, SO THERE IS ALSO A KEY
// ---------------------------------------------------
// On a public ntfy server, anybody who guesses the topic can publish to it.
// ntfy is honest about this; its own documentation says to treat topic names as
// secrets. That is a fine model for "tell me when the backup finished" and a
// poor one for "make my phone scream". So a command here must ALSO start with
// this channel's own key, exactly like the SMS channel -- and, exactly like
// every other channel, that key is its own and is not shared with any of them.

#pragma once

// locator.h for Fix, and it already brings config.h with it.
#include "locator.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

class NtfyClient : public QObject
{
    Q_OBJECT

public:
    explicit NtfyClient(QObject *parent = nullptr);

    void configure(const Settings &settings);

    // Publishes an answer to the reply topic. Does nothing if no reply topic
    // was set, which is a legitimate way to use this: orders in, silence out.
    void report(const Fix &fix);
    void say(const QString &text);

    bool connected() const { return m_connected; }

Q_SIGNALS:
    // An order arrived, with the key already stripped. The daemon decides
    // whether it is allowed -- this class never does.
    void command(const QString &verb);

    void connectedChanged(bool connected);

private:
    void subscribe();
    void stop();
    void setConnected(bool connected);
    void onLine(const QByteArray &line);
    QUrl topicUrl(const QString &topic, const QString &suffix = {}) const;
    void authorise(QNetworkRequest &request) const;

    QNetworkAccessManager m_net;
    Settings m_settings;
    QPointer<QNetworkReply> m_stream;
    QTimer m_retry;
    QByteArray m_buffer;
    bool m_connected = false;
    int m_failures = 0;
};
