// SPDX-License-Identifier: GPL-2.0-or-later
//
// One way to call D-Bus, and it is not QDBusInterface.
//
// Constructing a QDBusInterface INTROSPECTS the remote object, synchronously,
// with D-Bus's default timeout: twenty-five seconds. smart-unlock's README
// measured that against this phone's reboot cycle and found them to be the same
// order of magnitude -- and a daemon started by graphical-session.target that
// blocks for that long takes the session down, which the boot watchdog turns
// into a reboot.
//
// So: explicit method calls, no introspection, and a timeout chosen per call.
// Nothing in this package may build a QDBusInterface.

#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QVariantList>

namespace Bus
{
// Three seconds by default. If something on this phone has not answered in
// three seconds it is not going to, and waiting longer only makes the failure
// more expensive.
inline QDBusMessage call(const QDBusConnection &bus, const QString &service, const QString &path,
                         const QString &interface, const QString &method,
                         const QVariantList &arguments = {}, int timeoutMs = 3000)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    return bus.call(message, QDBus::Block, timeoutMs);
}

inline bool ok(const QDBusMessage &reply)
{
    return reply.type() == QDBusMessage::ReplyMessage;
}

inline QVariant first(const QDBusMessage &reply)
{
    return ok(reply) && !reply.arguments().isEmpty() ? reply.arguments().constFirst() : QVariant();
}
}
