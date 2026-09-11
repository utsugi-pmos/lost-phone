// SPDX-License-Identifier: GPL-2.0-or-later

#include "ringer.h"

#include "process.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <cmath>

namespace
{
constexpr int kSampleRate = 48000;
constexpr int kToneSeconds = 4;   // one loop of the alarm
constexpr double kLowHz = 2800.0;
constexpr double kHighHz = 3400.0;
constexpr double kSwapsPerSecond = 6.0;
constexpr short kAmplitude = 30000;  // just short of clipping at 16 bit

void appendLe32(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
    out.append(char((v >> 16) & 0xff));
    out.append(char((v >> 24) & 0xff));
}

void appendLe16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
}

// Runs a short command and returns its standard output, or an empty string if
// it could not run at all. Everything here is best-effort by design: a phone
// that cannot read its own volume still has to be able to make noise.
}

Ringer::Ringer(QObject *parent)
    : QObject(parent)
{
    m_tonePath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
        + QStringLiteral("/lost-phone-tone.wav");

    m_stopTimer.setSingleShot(true);
    connect(&m_stopTimer, &QTimer::timeout, this, &Ringer::stop);

    // The alarm is one short tone played over and over rather than one long
    // file: it keeps the file small, and it means a player that dies mid-way
    // costs us four seconds, not the whole alarm.
    connect(&m_player, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (!m_ringing) {
            return;
        }
        // Died before it could have made a sound: count it, and stop rather than
        // spin. A tone that never plays is a bug to fix, not a loop to run.
        if (code != 0 && m_playStarted.isValid() && m_playStarted.elapsed() < 500) {
            if (++m_consecutiveFailures >= 3) {
                qWarning() << "lost-phoned: no audio path accepts the tone; giving up";
                stop();
                return;
            }
        } else {
            m_consecutiveFailures = 0;
        }
        playOnce();
    });
}

Ringer::~Ringer()
{
    // The backstop. If the daemon is going away for any reason -- asked to
    // quit, crashed into cleanup, session ending -- the phone must not be left
    // pinned at maximum volume.
    if (m_ringing) {
        stop();
    }
}

bool Ringer::writeToneFile()
{
    if (QFile::exists(m_tonePath)) {
        return true;
    }

    const int frames = kSampleRate * kToneSeconds;
    QByteArray pcm;
    pcm.reserve(frames * 4);
    for (int i = 0; i < frames; ++i) {
        const double t = double(i) / kSampleRate;
        const double hz = (int(t * kSwapsPerSecond) % 2 == 0) ? kLowHz : kHighHz;
        const auto v = short(kAmplitude * std::sin(2.0 * M_PI * hz * t));
        appendLe16(pcm, quint16(v));  // stereo: the same sample to both speakers
        appendLe16(pcm, quint16(v));
    }

    QByteArray wav;
    wav.append("RIFF");
    appendLe32(wav, quint32(36 + pcm.size()));
    wav.append("WAVEfmt ");
    appendLe32(wav, 16);          // PCM header size
    appendLe16(wav, 1);           // format: PCM
    appendLe16(wav, 2);           // channels
    appendLe32(wav, kSampleRate);
    appendLe32(wav, kSampleRate * 2 * 2);  // byte rate
    appendLe16(wav, 4);           // block align
    appendLe16(wav, 16);          // bits per sample
    wav.append("data");
    appendLe32(wav, quint32(pcm.size()));
    wav.append(pcm);

    QFile f(m_tonePath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "lost-phoned: cannot write the tone to" << m_tonePath;
        return false;
    }
    f.write(wav);
    return true;
}

QString Ringer::savedVolumePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
        + QStringLiteral("/lost-phone-volume");
}

void Ringer::restoreSavedVolume()
{
    QFile f(savedVolumePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;  // never rang, or already restored cleanly
    }
    const QStringList saved = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    f.close();
    QFile::remove(savedVolumePath());
    if (saved.size() < 2 || saved.at(0).isEmpty()) {
        return;
    }
    // The third line is the output it rang through. It has to be saved: the alarm
    // goes to the SPEAKER, which need not be the default output, and returning the
    // volume to whatever is the default would leave the speaker at 100% and turn
    // down headphones nobody had touched. Files from earlier versions do not carry
    // it; for those, the default one.
    const QString sink = saved.size() >= 3 && !saved.at(2).isEmpty()
        ? saved.at(2)
        : QStringLiteral("@DEFAULT_SINK@");
    Proc::output(QStringLiteral("pactl"),
        {QStringLiteral("set-sink-volume"), sink, saved.at(0)});
    Proc::output(QStringLiteral("pactl"),
        {QStringLiteral("set-sink-mute"), sink, saved.at(1)});
}

// The SPEAKER, and not "the default output".
//
// With Bluetooth headphones connected, the default output is THEM: the alarm would
// ring inside an earbud sitting in a drawer and the phone would stay mute. For a
// lost device that is not ringing. So the board's speaker is looked up by name and
// sent there on purpose.
//
// If none is found -- another phone, other names -- it falls back to the default
// output, which is worse but is something. Staying silent because the name does not
// match would be the worst possible failure here.
QString Ringer::speaker()
{
    const QString listado = Proc::output(QStringLiteral("pactl"),
        {QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("sinks")});
    const QStringList lines = listado.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString respaldo;
    for (const QString &line : lines) {
        const QStringList campos = line.split(QLatin1Char('\t'), Qt::SkipEmptyParts);
        if (campos.size() < 2) {
            continue;
        }
        const QString name = campos.at(1);
        if (name.contains(QLatin1String("Speaker"), Qt::CaseInsensitive)
            || name.contains(QLatin1String("speaker"), Qt::CaseInsensitive)) {
            return name;
        }
        // An output from the board itself, in case the profile does not say "Speaker".
        if (respaldo.isEmpty() && name.startsWith(QLatin1String("alsa_output."))
            && !name.contains(QLatin1String("bluez"), Qt::CaseInsensitive)) {
            respaldo = name;
        }
    }
    if (!respaldo.isEmpty()) {
        return respaldo;
    }
    return QStringLiteral("@DEFAULT_SINK@");
}

void Ringer::raiseVolume()
{
    m_sink = speaker();
    qInfo().noquote() << "lost-phoned: the alarm will ring through" << m_sink;

    // Read first, so the restore has something true to go back to.
    const QString volume = Proc::output(QStringLiteral("pactl"),
                               {QStringLiteral("get-sink-volume"), m_sink});
    const QString mute = Proc::output(QStringLiteral("pactl"),
                             {QStringLiteral("get-sink-mute"), m_sink});
    if (!volume.isEmpty() && !mute.isEmpty()) {
        // "Volume: mono: 0 /   0% / -inf dB" -> "0%"
        const qsizetype pct = volume.indexOf(QLatin1Char('%'));
        if (pct > 0) {
            qsizetype start = pct;
            while (start > 0 && volume.at(start - 1).isDigit()) {
                --start;
            }
            m_savedVolume = volume.mid(start, pct - start + 1);
        }
        m_savedMute = mute.contains(QStringLiteral("yes")) ? QStringLiteral("1")
                                                           : QStringLiteral("0");
        m_volumeSaved = !m_savedVolume.isEmpty();

        // Said out loud, and that is not noise. This is the one thing in the
        // package whose job is to put your settings back, and when it gets it
        // wrong there is otherwise NO way to tell whether it restored the wrong
        // thing or restored the right thing over something that had already
        // changed underneath it. One line in the journal settles that argument.
        qInfo().noquote() << "lost-phoned: volume before the alarm:" << m_savedVolume
                          << "muted:" << (m_savedMute == QLatin1String("1") ? "yes" : "no");

        // On disk before a single sample is played, so the restore does not
        // depend on this process living long enough to do it itself.
        if (m_volumeSaved) {
            QFile f(savedVolumePath());
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(m_savedVolume.toUtf8() + '\n' + m_savedMute.toUtf8() + '\n'
                        + m_sink.toUtf8() + '\n');
            }
        }
    }

    Proc::output(QStringLiteral("pactl"),
        {QStringLiteral("set-sink-mute"), m_sink, QStringLiteral("0")});
    Proc::output(QStringLiteral("pactl"), {QStringLiteral("set-sink-volume"), m_sink,
                                  QStringLiteral("%1%").arg(qBound(1, m_settings.ringVolume, 100))});
}

void Ringer::restoreVolume()
{
    if (!m_volumeSaved) {
        qWarning() << "lost-phoned: never managed to read the previous volume; not inventing one";
        return;
    }
    // To the SAME output that was raised, not the default one.
    const QString sink = m_sink.isEmpty() ? QStringLiteral("@DEFAULT_SINK@") : m_sink;
    Proc::output(QStringLiteral("pactl"),
        {QStringLiteral("set-sink-volume"), sink, m_savedVolume});
    Proc::output(QStringLiteral("pactl"),
        {QStringLiteral("set-sink-mute"), sink, m_savedMute});
    QFile::remove(savedVolumePath());
    qInfo().noquote() << "lost-phoned: volume put back to" << m_savedVolume << "muted:"
                      << (m_savedMute == QLatin1String("1") ? "yes" : "no");
    m_volumeSaved = false;
}

void Ringer::playOnce()
{
    m_playStarted.start();

    // paplay when Pulse is up -- which on this phone it always is, and it is the
    // only way in while it holds the card. aplay through the ALSA 'default'
    // device is the fallback for a session without Pulse; hw:0,0 is the last
    // resort and only works with nobody else holding the card.
    if (!QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()) {
        QStringList args;
        if (!m_sink.isEmpty() && m_sink != QLatin1String("@DEFAULT_SINK@")) {
            args << QStringLiteral("--device=%1").arg(m_sink);
        }
        args << m_playing;
        m_player.start(QStringLiteral("paplay"), args);
        if (m_player.waitForStarted(2000)) {
            return;
        }
    }
    // aplay only understands WAV, so the ALSA fallback plays the generated tone
    // whatever melody was chosen. That is the path for a session with no
    // PulseAudio at all, where being heard matters more than being pretty.
    const QString wav = writeToneFile() ? m_tonePath : m_playing;
    m_player.start(QStringLiteral("aplay"), {QStringLiteral("-q"), QStringLiteral("-D"),
                                             QStringLiteral("default"), wav});
    if (m_player.waitForStarted(2000)) {
        return;
    }
    m_player.start(QStringLiteral("aplay"), {QStringLiteral("-q"), QStringLiteral("-D"),
                                             QStringLiteral("hw:0,0"), wav});
    m_player.waitForStarted(2000);
}

void Ringer::configure(const Settings &settings)
{
    m_settings = settings;
}

// The file if it is there, the generated tone if it is not.
//
// A melody that vanished -- the sound theme uninstalled, a path typed by hand
// that no longer exists -- must not turn into a phone that stays silent when you
// are looking for it. Falling back to something ugly beats falling back to
// nothing, and it says so in the journal rather than doing it quietly.
QString Ringer::resolveSound()
{
    const QString wanted = m_settings.ringSound;
    if (!wanted.isEmpty() && wanted != QLatin1String("tono")) {
        if (QFile::exists(wanted)) {
            return wanted;
        }
        qWarning() << "lost-phoned: cannot find" << wanted << "-- ringing with the generated tone";
    }
    return writeToneFile() ? m_tonePath : QString();
}

bool Ringer::start(int seconds)
{
    m_playing = resolveSound();
    if (m_playing.isEmpty()) {
        return false;
    }

    // Ringing again while ringing extends the alarm instead of stacking a
    // second tone on it: a worried user pressing twice should not get noise.
    m_stopTimer.start(qMax(5, seconds) * 1000);

    if (m_ringing) {
        return true;
    }

    raiseVolume();
    m_ringing = true;
    m_consecutiveFailures = 0;
    playOnce();
    Q_EMIT ringingChanged(true);
    return true;
}

void Ringer::stop()
{
    if (!m_ringing) {
        return;
    }
    m_ringing = false;
    m_stopTimer.stop();

    if (m_player.state() != QProcess::NotRunning) {
        m_player.terminate();
        if (!m_player.waitForFinished(1500)) {
            m_player.kill();
            m_player.waitForFinished(1000);
        }
    }
    restoreVolume();
    Q_EMIT ringingChanged(false);
}
