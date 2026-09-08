// SPDX-License-Identifier: GPL-2.0-or-later
//
// The phone's half of the relay: the channel that works when the phone is out
// in the street.
//
// HOW IT REACHES A PHONE NOBODY CAN ADDRESS
// -----------------------------------------
// It does not get reached: it reaches out. The phone holds a GET open against
// the relay, the relay answers the moment an order arrives, and the phone
// starts another. That crosses CGNAT because it is an ordinary outgoing
// request, needs no library on either side, and -- measured against the real
// relay -- delivers an order in 11 ms.
//
// The wait between one poll ending empty and the next one starting IS the
// heartbeat the settings expose. Zero is "always connected": an order lands in
// a second and the radio never rests. Thirty minutes is the default, because a
// phone that is easy to reach and flat by five o'clock has not been found.
//
// WHAT IT IS NOT ALLOWED TO DO
// ----------------------------
// Nothing on its own. It hands every order to the same capability check the SMS
// channel goes through, so a relay somebody has broken into cannot do more than
// was already switched on here.

#pragma once

// locator.h for Fix, and it already brings config.h with it.
#include "locator.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

class RelayClient : public QObject
{
    Q_OBJECT

public:
    explicit RelayClient(QObject *parent = nullptr);

    // Applies new settings: starts polling, stops, or reconnects to a different
    // relay. Safe to call on every config change, which is what the daemon does.
    void configure(const Settings &settings);

    // Types the pairing code. On success the token is written to the config by
    // the daemon and polling starts on its own.
    void pair(const QString &url, const QString &code, const QString &name);

    void report(const Fix &fix);

private:
    // The last position that could not be delivered, and whether there is one.
    //
    // Getting a position and throwing it away because there was no network at that
    // instant is the worst possible moment to lose data: the phone HAS FIXED, knows
    // where it is, and goes quiet for half an hour until the next refresh. It is
    // saved and sent the moment the connection shows signs of life again.
    Fix m_sinEntregar;
    bool m_hayPendiente = false;

public:

    bool connected() const { return m_connected; }

Q_SIGNALS:
    // An order arrived. The daemon decides whether it is allowed.
    void command(const QString &verb);

    // The fingerprint travels with the token: they are the two halves of the same
    // trust, and saving one without the other is of no use.
    void paired(const QString &deviceId, const QString &token, const QString &fingerprint);
    void pairingFailed(const QString &reason);
    void connectedChanged(bool connected);

private:
    void poll();
    void stop();
    void setConnected(bool connected);
    QUrl endpoint(const QString &path) const;

    // Pins the relay certificate, or accepts it for the first time if we are
    // pairing. Everything that talks to the relay goes through here.
    void confiar(QNetworkReply *reply, bool primeraVez);

    // The fingerprint seen during pairing, until it is saved.
    QString m_huellaVista;

    QNetworkAccessManager m_net;
    Settings m_settings;
    QPointer<QNetworkReply> m_poll;
    QTimer m_retry;
    bool m_connected = false;

    // Backoff for a relay that is down or a network that is not there. Without
    // it a phone with no coverage would retry in a tight loop and flatten
    // itself trying to tell you where it is.
    int m_failures = 0;
};
