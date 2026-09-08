// SPDX-License-Identifier: GPL-2.0-or-later

#include "lostmode.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

QString LostModeState::filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
        + QStringLiteral("/lost-phone/modo-perdido");
}

LostMode LostModeState::load()
{
    LostMode state;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return state;  // no file means not lost, which is the safe answer
    }
    QTextStream in(&f);
    // Line 1: "1" or "0". Line 2: the ISO timestamp. The rest: the message,
    // which may contain anything including newlines, so it goes last.
    state.active = in.readLine().trimmed() == QLatin1String("1");
    state.since = QDateTime::fromString(in.readLine().trimmed(), Qt::ISODate);
    state.message = in.readAll().trimmed();
    return state;
}

void LostModeState::save(const LostMode &state)
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    // QSaveFile writes to a neighbour and renames: a phone that loses power
    // mid-write must never come back with a half-written lost mode, because
    // "half" would read as "not lost".
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    QTextStream out(&f);
    out << (state.active ? "1" : "0") << '\n'
        << (state.since.isValid() ? state.since : QDateTime::currentDateTime())
               .toString(Qt::ISODate)
        << '\n'
        << state.message << '\n';
    out.flush();
    f.commit();
}

bool LostModeState::isActive()
{
    return load().active;
}
