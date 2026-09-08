// SPDX-License-Identifier: GPL-2.0-or-later

#include "ntfyprotocol.h"

#include <QJsonDocument>
#include <QJsonObject>

QString NtfyProtocol::commandFrom(const QByteArray &jsonLine, const QString &key)
{
    if (key.isEmpty()) {
        return {};  // no key, no channel: checked here as well as in the matrix
    }
    const QJsonObject object = QJsonDocument::fromJson(jsonLine).object();
    if (object.value(QStringLiteral("event")).toString() != QLatin1String("message")) {
        return {};  // open, keepalive, poll_request: not orders
    }
    const QString body = object.value(QStringLiteral("message")).toString().simplified();
    if (body.isEmpty()) {
        return {};
    }
    if (body.section(QLatin1Char(' '), 0, 0).compare(key, Qt::CaseInsensitive) != 0) {
        return {};
    }
    return body.section(QLatin1Char(' '), 1).trimmed();
}
