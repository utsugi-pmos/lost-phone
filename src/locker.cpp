// SPDX-License-Identifier: GPL-2.0-or-later

#include "locker.h"

#include "dbus.h"

#include <QDBusConnection>
#include <QDebug>
#include <QVariantMap>

namespace
{
const QString kNotifications = QStringLiteral("org.freedesktop.Notifications");
const QString kNotificationsPath = QStringLiteral("/org/freedesktop/Notifications");
}

Locker::Locker(QObject *parent)
    : QObject(parent)
{
    // The notification server tells us which button was pressed. Subscribed
    // here rather than at ring time, because subscribing is cheap and doing it
    // while the alarm is going off is one more thing that can fail at the worst
    // moment.
    QDBusConnection::sessionBus().connect(kNotifications, kNotificationsPath, kNotifications,
                                          QStringLiteral("ActionInvoked"), this,
                                          SLOT(onActionInvoked(uint, QString)));
}

void Locker::onActionInvoked(uint id, const QString &action)
{
    if (id == m_notificationId && action == QLatin1String("parar")) {
        Q_EMIT stopRequested();
    }
}

void Locker::despertarPantalla()
{
    // PowerDevil, which is what rules the screen of this phone. Two seconds: if it
    // does not answer in that time it will not answer, and the alarm cannot sit
    // waiting for a screen to turn on.
    const QDBusMessage reply =
        Bus::call(QDBusConnection::sessionBus(), QStringLiteral("org.kde.Solid.PowerManagement"),
                  QStringLiteral("/org/kde/Solid/PowerManagement"),
                  QStringLiteral("org.kde.Solid.PowerManagement"), QStringLiteral("wakeup"), {},
                  2000);
    if (!Bus::ok(reply)) {
        qWarning() << "lost-phoned: could not turn the screen on:" << reply.errorMessage();
    }
}

void Locker::lock()
{
    // Asked over the session bus, not through loginctl: `loginctl lock-session`
    // answers "Session does not support lock screen" on this phone -- measured.
    // The lock belongs to KDE, so KDE is who gets asked. Two seconds, because a
    // locker that is not answering will not start answering.
    const QDBusMessage reply =
        Bus::call(QDBusConnection::sessionBus(), QStringLiteral("org.kde.screensaver"),
                  QStringLiteral("/ScreenSaver"), QStringLiteral("org.freedesktop.ScreenSaver"),
                  QStringLiteral("SetActive"), {true}, 2000);
    if (!Bus::ok(reply)) {
        qWarning() << "lost-phoned: could not lock the screen:" << reply.errorMessage();
    }
}

void Locker::showMessage(const QString &text)
{
    if (text.isEmpty()) {
        clearMessage();
        return;
    }

    QVariantMap hints;
    // Critical so it is not folded away with the rest, and resident so it stays
    // put instead of expiring while the phone sits in somebody's pocket.
    hints.insert(QStringLiteral("urgency"), uchar(2));
    hints.insert(QStringLiteral("resident"), true);

    // Replacing our own previous notification rather than adding another: lost
    // mode is re-asserted at every start, and a lock screen with nine identical
    // messages helps nobody.
    const QDBusMessage reply = Bus::call(
        QDBusConnection::sessionBus(), kNotifications, kNotificationsPath, kNotifications,
        QStringLiteral("Notify"),
        {QStringLiteral("lost-phone"), m_notificationId, QStringLiteral("find-location"),
         QStringLiteral("This phone is lost"), text, QStringList(), hints, 0},
        2000);

    const QVariant id = Bus::first(reply);
    if (id.isValid()) {
        m_notificationId = id.toUInt();
    } else {
        qWarning() << "lost-phoned: could not show the message:" << reply.errorMessage();
    }
}

void Locker::clearMessage()
{
    if (m_notificationId == 0) {
        return;
    }
    Bus::call(QDBusConnection::sessionBus(), kNotifications, kNotificationsPath, kNotifications,
              QStringLiteral("CloseNotification"), {m_notificationId}, 2000);
    m_notificationId = 0;
}
