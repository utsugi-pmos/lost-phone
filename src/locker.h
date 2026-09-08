// SPDX-License-Identifier: GPL-2.0-or-later
//
// Locking the phone, and writing a message on the lock screen.
//
// TWO MEASUREMENTS DECIDED THIS FILE
// ----------------------------------
// 1. `loginctl lock-session` answers "Session does not support lock screen" on
//    this phone. The lock belongs to KDE, so it is asked over the session bus.
//    (And `GetActive` lies about the state, which is why nothing here trusts
//    it -- see the note in the KDE lock documentation of this repo.)
//
// 2. The Plasma Mobile lock screen already draws notifications, of itself:
//    LockScreenContent.qml instantiates NotificationsComponent over a
//    WatchedNotificationsModel. So the contact message needs no patched theme,
//    no wallpaper trick and nothing that an `apk upgrade` would wash away -- it
//    is a resident, critical notification, and it appears on the lock screen
//    because that screen was always going to show it.

#pragma once

#include <QObject>
#include <QString>

class Locker : public QObject
{
    Q_OBJECT

public:
    explicit Locker(QObject *parent = nullptr);

    // Turns the screen on.
    //
    // Without this, the alarm rings with the screen off and the STOP button ends
    // up behind: pressing power, unlocking and finding the window. That is, exactly
    // the steps the alarm screen was there to remove. Measured on the surya: it
    // rang and the screen did not turn on.
    void despertarPantalla();

    // Locks the session now. Safe to call when already locked: it is the "make
    // sure" that runs at every start while lost mode is on.
    void lock();

    // The text a stranger reads on the lock screen. Empty removes it.
    void showMessage(const QString &text);
    void clearMessage();

Q_SIGNALS:
    // Somebody pressed Parar on the alarm screen. Kept as a signal even though
    // the screen now calls StopRing over D-Bus directly: the lost-mode message
    // still uses actions, and one route in is one route to test.
    void stopRequested();

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &action);

private:
    // The notification id, so the message can be taken down again rather than
    // piling up a new one every time lost mode is re-asserted at boot.
    unsigned int m_notificationId = 0;
};
