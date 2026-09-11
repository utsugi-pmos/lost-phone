// SPDX-License-Identifier: GPL-2.0-or-later
//
// One way to run a program and read what it printed, and it always asks in the
// language that does not move.
//
// This file exists because of a bug that took hours. `pactl get-sink-mute`
// prints "Mute: yes" under LC_ALL=C and "Mute: sí" inside this phone's Spanish
// session. The alarm looked for "yes", so it recorded "not muted" every single
// time, faithfully restored the volume, and left the phone unsilenced. The
// symptom was maddening precisely because half of it worked: the volume was
// parsed from a number with a '%', and numbers do not get translated.
//
// It also explains why every test passed. Run over SSH the checks were correct,
// because an SSH session carries no locale -- so the tester saw English and the
// daemon saw Spanish. The environment being measured was not the environment
// that failed.
//
// mmcli and nmcli translate their labels too, so the locate cascade had the
// same landmine waiting. Hence: ONE helper, and nothing in this package spawns
// a program whose output it parses any other way.

#pragma once

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace Proceso
{
// LC_ALL=C and LANG=C. Not tidiness -- correctness.
inline QProcessEnvironment entornoSinIdioma()
{
    QProcessEnvironment entorno = QProcessEnvironment::systemEnvironment();
    entorno.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    entorno.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    return entorno;
}

// Runs a short command and returns its standard output, or an empty string if
// it could not run, failed, or took too long. Everything here is best-effort by
// design: a phone that cannot read its own volume still has to make noise.
inline QString salida(const QString &program, const QStringList &args, int timeoutMs = 3000)
{
    QProcess p;
    p.setProcessEnvironment(entornoSinIdioma());
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}
}
