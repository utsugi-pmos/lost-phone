// SPDX-License-Identifier: GPL-2.0-or-later

#include "lostmode.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

QString LostModeState::dirPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
        + QStringLiteral("/lost-phone/");
}

// Where lost mode is written. The file used to be "modo-perdido"; new saves go
// to "lost-mode".
QString LostModeState::filePath()
{
    return dirPath() + QStringLiteral("lost-mode");
}

// Where it is read from: the new name if it is there, otherwise the old one. A
// phone that was lost BEFORE this upgrade must still come up lost afterwards --
// that is the whole point of the file surviving a reboot.
QString LostModeState::readPath()
{
    const QString current = filePath();
    if (QFile::exists(current)) {
        return current;
    }
    const QString legacy = dirPath() + QStringLiteral("modo-perdido");
    return QFile::exists(legacy) ? legacy : current;
}

LostMode LostModeState::load()
{
    LostMode state;
    QFile f(readPath());
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
