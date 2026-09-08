// SPDX-License-Identifier: GPL-2.0-or-later
//
// Making this phone scream, over the top of a phone that was deliberately
// silenced. Measured on the surya, 2026-09-04, with the sink at "0%, Mute: yes"
// -- which is the state a phone is actually in when you lose it:
//
//   pcm0p/sub0/status -> state: RUNNING
//   i2c bus9 0x4c/0x4d PWR_CTL=0x0c   both tas2562 amplifiers ACTIVE
//
// So it works, and the route it works by is not negotiable:
//
//   PulseAudio owns the card. `aplay -D hw:0,0` while it is up answers
//   "audio open error: Resource busy". There is no sneaking in alongside it.
//
// Hence: go through Pulse when Pulse is alive, and only fall back to raw ALSA
// when it is not. And ALWAYS put the volume back. The `audio` setting on this
// phone documents that the volume has exactly one writer and is persisted with
// `alsactl store`; a lost-phone that left the phone pinned at maximum forever
// would quietly break that.

#pragma once

#include "config.h"

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class Ringer : public QObject
{
    Q_OBJECT

public:
    explicit Ringer(QObject *parent = nullptr);
    ~Ringer() override;

    // The settings decide what it plays and how loud. Applied on every start,
    // so changing them takes effect the next time it rings and never mid-alarm.
    void configure(const Settings &settings);

    // Starts the alarm and returns whether any audio path accepted it. Ringing
    // again while already ringing just extends it, so a second command from a
    // worried user does not stack two tones on top of each other.
    bool start(int seconds);

    // Silences it and restores the volume and mute exactly as they were.
    void stop();

    bool ringing() const { return m_ringing; }

    // The backstop, called from `lost-phoned --restaurar` and wired to the
    // unit's ExecStopPost. A daemon killed outright -- SIGKILL, OOM, a session
    // that went away mid-alarm -- would otherwise leave the phone pinned at
    // maximum volume for ever, which is precisely what the `audio` setting on
    // this device warns about: the volume has one writer and gets persisted
    // with `alsactl store`. So the pre-alarm volume is written to a file the
    // moment it is raised, and this puts it back without needing the object
    // that raised it.
    static void restoreSavedVolume();
    static QString savedVolumePath();

Q_SIGNALS:
    void ringingChanged(bool ringing);

private:
    // The tone: two alternating frequencies rather than one steady note.
    // A single tone is hard to track by ear and easy for a room to swallow;
    // a warble tells you which direction it is coming from.
    bool writeToneFile();
    QString resolveSound();

    // The output it must ALWAYS ring through: the phone's speaker.
    QString altavoz();

    void raiseVolume();
    void restoreVolume();
    void playOnce();

    QString m_tonePath;
    Settings m_settings;

    // What is actually being played this time round: the sound file if there is
    // one and it exists, otherwise the generated tone.
    QString m_playing;
    QProcess m_player;
    QTimer m_stopTimer;
    bool m_ringing = false;

    // What the phone sounded like before we touched it. Empty means "we never
    // managed to read it", and in that case we do not write anything back --
    // guessing a volume is worse than leaving it loud.
    QString m_savedVolume;
    QString m_savedMute;
    bool m_volumeSaved = false;
    QString m_sink;  // the speaker, resolved when it starts ringing

    // A player that dies the instant it starts -- no Pulse, no card, no tone
    // file -- would otherwise be restarted forever by the finished handler,
    // burning the battery of a phone somebody is trying to find. Three failures
    // in a row and the alarm gives up and says so.
    QElapsedTimer m_playStarted;
    int m_consecutiveFailures = 0;
};
