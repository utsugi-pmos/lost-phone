// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared brain of both front-ends: the Settings module (kcm.cpp) and the
// standalone application (main.cpp). Same split as smart-unlock and appsvivas,
// and for the same reason -- a KCM is a plugin tied to the KF6 ABI and an
// update can leave it unable to load, so the plain Qt/Kirigami app is the
// fallback that keeps the settings reachable.
//
// It edits a WORKING COPY and only writes ~/.config/lost-phonerc on save(), so
// Apply means what it says. Writing the file is the whole handoff to the
// daemon: lost-phoned watches it and re-reads on change.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "src/config.h"

class LostPhoneBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)

    Q_PROPERTY(bool smsEnabled READ smsEnabled WRITE setSmsEnabled NOTIFY changed)
    Q_PROPERTY(QString smsKey READ smsKey WRITE setSmsKey NOTIFY changed)
    Q_PROPERTY(bool smsAllowRing READ smsAllowRing WRITE setSmsAllowRing NOTIFY changed)
    Q_PROPERTY(bool smsAllowLocate READ smsAllowLocate WRITE setSmsAllowLocate NOTIFY changed)
    Q_PROPERTY(bool smsAllowLock READ smsAllowLock WRITE setSmsAllowLock NOTIFY changed)
    Q_PROPERTY(bool smsReply READ smsReply WRITE setSmsReply NOTIFY changed)
    Q_PROPERTY(bool smsDeleteAfter READ smsDeleteAfter WRITE setSmsDeleteAfter NOTIFY changed)

    Q_PROPERTY(int ringSeconds READ ringSeconds WRITE setRingSeconds NOTIFY changed)
    Q_PROPERTY(QString ringSound READ ringSound WRITE setRingSound NOTIFY changed)
    Q_PROPERTY(int ringVolume READ ringVolume WRITE setRingVolume NOTIFY changed)

    // [{name, path}] -- what is installed on the phone, read on the fly. Not a
    // fixed list in the code: if another sound theme gets installed tomorrow, it
    // shows up on its own.
    Q_PROPERTY(QVariantList sounds READ sounds CONSTANT)

    Q_PROPERTY(bool locateGnss READ locateGnss WRITE setLocateGnss NOTIFY changed)
    Q_PROPERTY(bool locateWifi READ locateWifi WRITE setLocateWifi NOTIFY changed)
    Q_PROPERTY(bool locateCell READ locateCell WRITE setLocateCell NOTIFY changed)
    Q_PROPERTY(bool locateLastKnown READ locateLastKnown WRITE setLocateLastKnown NOTIFY changed)

    Q_PROPERTY(bool relayEnabled READ relayEnabled WRITE setRelayEnabled NOTIFY changed)
    Q_PROPERTY(QString relayUrl READ relayUrl WRITE setRelayUrl NOTIFY changed)
    Q_PROPERTY(int relayIdleMinutes READ relayIdleMinutes WRITE setRelayIdleMinutes NOTIFY changed)
    Q_PROPERTY(bool relayAllowRing READ relayAllowRing WRITE setRelayAllowRing NOTIFY changed)
    Q_PROPERTY(bool relayAllowLocate READ relayAllowLocate WRITE setRelayAllowLocate NOTIFY changed)
    Q_PROPERTY(bool relayAllowLock READ relayAllowLock WRITE setRelayAllowLock NOTIFY changed)
    Q_PROPERTY(bool relayAllowPower READ relayAllowPower WRITE setRelayAllowPower NOTIFY changed)

    Q_PROPERTY(bool ntfyEnabled READ ntfyEnabled WRITE setNtfyEnabled NOTIFY changed)
    Q_PROPERTY(QString ntfyServer READ ntfyServer WRITE setNtfyServer NOTIFY changed)
    Q_PROPERTY(QString ntfyTopic READ ntfyTopic WRITE setNtfyTopic NOTIFY changed)
    Q_PROPERTY(QString ntfyReplyTopic READ ntfyReplyTopic WRITE setNtfyReplyTopic NOTIFY changed)
    Q_PROPERTY(QString ntfyKey READ ntfyKey WRITE setNtfyKey NOTIFY changed)
    Q_PROPERTY(bool ntfyAllowRing READ ntfyAllowRing WRITE setNtfyAllowRing NOTIFY changed)
    Q_PROPERTY(bool ntfyAllowLocate READ ntfyAllowLocate WRITE setNtfyAllowLocate NOTIFY changed)
    Q_PROPERTY(bool ntfyConnected READ ntfyConnected NOTIFY daemonChanged)

    Q_PROPERTY(bool lanEnabled READ lanEnabled WRITE setLanEnabled NOTIFY changed)
    Q_PROPERTY(bool lanAllowRing READ lanAllowRing WRITE setLanAllowRing NOTIFY changed)
    Q_PROPERTY(bool lanAllowLocate READ lanAllowLocate WRITE setLanAllowLocate NOTIFY changed)
    Q_PROPERTY(QString lanToken READ lanToken NOTIFY changed)

    Q_PROPERTY(bool lanSimpleEnabled READ lanSimpleEnabled WRITE setLanSimpleEnabled NOTIFY changed)
    Q_PROPERTY(QString lanSimpleKey READ lanSimpleKey WRITE setLanSimpleKey NOTIFY changed)

    // The FULL link, with this phone's real address already inside it and ready
    // to copy. Showing the template "http://<phone>:8479/..." would leave the
    // user the one part they cannot know from memory.
    Q_PROPERTY(QString lanSimpleUrl READ lanSimpleUrl NOTIFY changed)

    // Whether the phone is being held in lost mode right now, and the way out
    // of it. The button lives here and nowhere else: reaching this screen means
    // the phone was unlocked, which means you are the owner.
    Q_PROPERTY(bool lostModeActive READ lostModeActive NOTIFY daemonChanged)

    // Paired means "has a token". Connected means "is talking to the relay
    // right now". They are two different questions and the screen shows both:
    // a phone that paired last month and has not reached the relay since is the
    // failure this feature has to make visible.
    Q_PROPERTY(bool relayPaired READ relayPaired NOTIFY changed)
    Q_PROPERTY(bool relayConnected READ relayConnected NOTIFY daemonChanged)
    Q_PROPERTY(QString pairingError READ pairingError NOTIFY daemonChanged)

    // What the phone last managed to work out, in words. Read-only: the daemon
    // owns this and the settings UI must never write it back.
    Q_PROPERTY(QString lastKnownSummary READ lastKnownSummary NOTIFY changed)

    // Whether lost-phoned is actually running. Without it the settings are a
    // file nobody reads, and a UI that looks configured while doing nothing is
    // the worst failure this feature can have.
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY daemonChanged)
    Q_PROPERTY(bool ringing READ ringing NOTIFY daemonChanged)

    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
    explicit LostPhoneBackend(QObject *parent = nullptr);

    bool enabled() const { return m_working.enabled; }
    bool smsEnabled() const { return m_working.smsEnabled; }
    QString smsKey() const { return m_working.smsKey; }
    bool smsAllowRing() const { return m_working.smsAllowRing; }
    bool smsAllowLocate() const { return m_working.smsAllowLocate; }
    bool smsAllowLock() const { return m_working.smsAllowLock; }
    bool smsReply() const { return m_working.smsReply; }
    bool smsDeleteAfter() const { return m_working.smsDeleteAfter; }
    int ringSeconds() const { return m_working.ringSeconds; }
    QString ringSound() const { return m_working.ringSound; }
    int ringVolume() const { return m_working.ringVolume; }
    QVariantList sounds() const;
    bool locateGnss() const { return m_working.locateGnss; }
    bool locateWifi() const { return m_working.locateWifi; }
    bool locateCell() const { return m_working.locateCell; }
    bool locateLastKnown() const { return m_working.locateLastKnown; }

    bool relayEnabled() const { return m_working.relayEnabled; }
    QString relayUrl() const { return m_working.relayUrl; }
    int relayIdleMinutes() const { return m_working.relayIdleMinutes; }
    bool relayAllowRing() const { return m_working.relayAllowRing; }
    bool relayAllowLocate() const { return m_working.relayAllowLocate; }
    bool relayAllowLock() const { return m_working.relayAllowLock; }
    bool relayAllowPower() const { return m_working.relayAllowPower; }
    bool lostModeActive() const { return m_lostModeActive; }
    bool ntfyEnabled() const { return m_working.ntfyEnabled; }
    QString ntfyServer() const { return m_working.ntfyServer; }
    QString ntfyTopic() const { return m_working.ntfyTopic; }
    QString ntfyReplyTopic() const { return m_working.ntfyReplyTopic; }
    QString ntfyKey() const { return m_working.ntfyKey; }
    bool ntfyAllowRing() const { return m_working.ntfyAllowRing; }
    bool ntfyAllowLocate() const { return m_working.ntfyAllowLocate; }
    bool ntfyConnected() const { return m_ntfyConnected; }

    bool lanEnabled() const { return m_working.lanEnabled; }
    bool lanAllowRing() const { return m_working.lanAllowRing; }
    bool lanAllowLocate() const { return m_working.lanAllowLocate; }
    QString lanToken() const { return m_working.lanToken; }
    bool lanSimpleEnabled() const { return m_working.lanSimpleEnabled; }
    QString lanSimpleKey() const { return m_working.lanSimpleKey; }
    QString lanSimpleUrl() const;
    bool relayPaired() const { return !m_saved.relayToken.isEmpty(); }
    bool relayConnected() const { return m_relayConnected; }
    QString pairingError() const { return m_pairingError; }

    QString lastKnownSummary() const;
    bool daemonRunning() const { return m_daemonRunning; }
    bool ringing() const { return m_ringing; }
    bool dirty() const { return m_dirty; }

    // Q_INVOKABLE on the setters, not just the Q_PROPERTY declaration. A plain
    // public method is not in the metaobject, so QML calling setEnabled() would
    // depend on the property machinery exposing it -- and a settings switch that
    // silently does nothing is exactly the failure this feature cannot afford.
    Q_INVOKABLE void setEnabled(bool value);
    Q_INVOKABLE void setSmsEnabled(bool value);
    Q_INVOKABLE void setSmsKey(const QString &value);
    Q_INVOKABLE void setSmsAllowRing(bool value);
    Q_INVOKABLE void setSmsAllowLocate(bool value);
    Q_INVOKABLE void setSmsAllowLock(bool value);
    Q_INVOKABLE void setSmsReply(bool value);
    Q_INVOKABLE void setSmsDeleteAfter(bool value);
    Q_INVOKABLE void setRingSeconds(int value);
    Q_INVOKABLE void setRingSound(const QString &value);
    Q_INVOKABLE void setRingVolume(int value);
    Q_INVOKABLE void setLocateGnss(bool value);
    Q_INVOKABLE void setLocateWifi(bool value);
    Q_INVOKABLE void setLocateCell(bool value);
    Q_INVOKABLE void setLocateLastKnown(bool value);
    Q_INVOKABLE void setRelayEnabled(bool value);
    Q_INVOKABLE void setRelayUrl(const QString &value);
    Q_INVOKABLE void setRelayIdleMinutes(int value);
    Q_INVOKABLE void setRelayAllowRing(bool value);
    Q_INVOKABLE void setRelayAllowLocate(bool value);
    Q_INVOKABLE void setRelayAllowLock(bool value);
    Q_INVOKABLE void setRelayAllowPower(bool value);
    Q_INVOKABLE void leaveLostMode();

    // Turning the home channel on mints its token if there is none. A switch
    // that left the channel enabled and credential-less would look on and be
    // off, which is the one failure this feature cannot afford.
    Q_INVOKABLE void setNtfyEnabled(bool value);
    Q_INVOKABLE void setNtfyServer(const QString &value);
    Q_INVOKABLE void setNtfyTopic(const QString &value);
    Q_INVOKABLE void setNtfyReplyTopic(const QString &value);
    Q_INVOKABLE void setNtfyKey(const QString &value);
    Q_INVOKABLE void setNtfyAllowRing(bool value);
    Q_INVOKABLE void setNtfyAllowLocate(bool value);

    // Two topics nobody can guess, offered so the user does not name them
    // "phone" and hand the whole channel to the first person who tries.
    Q_INVOKABLE QString suggestTopic() const;

    Q_INVOKABLE void setLanEnabled(bool value);
    Q_INVOKABLE void setLanAllowRing(bool value);
    Q_INVOKABLE void setLanSimpleEnabled(bool value);
    Q_INVOKABLE void setLanSimpleKey(const QString &value);
    Q_INVOKABLE void setLanAllowLocate(bool value);

    // Hands the code to the daemon, which is the one that keeps the token
    // afterwards. Two writers of the same secret is how you end up paired twice
    // and reachable never.
    Q_INVOKABLE void pair(const QString &code);

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void restoreDefaults();

    // A key the user did not choose. People pick "1234" and then wonder why
    // the neighbour's kid can ring their phone; this offers a real one, and
    // avoids the characters that a phone keyboard makes hard to type.
    Q_INVOKABLE QString suggestKey() const;

    // Short, quiet test so the user can hear what the alarm sounds like without
    // handing the SMS channel any power. Goes to the daemon over D-Bus, because
    // the daemon is the one thing that knows how to put the volume back.
    Q_INVOKABLE void testRing();
    Q_INVOKABLE void stopRing();
    Q_INVOKABLE void refreshDaemon();

Q_SIGNALS:
    void changed();
    void dirtyChanged();
    void daemonChanged();

private:
    void markDirty();
    static QString randomToken();

    Settings m_saved;
    Settings m_working;
    bool m_dirty = false;
    bool m_daemonRunning = false;
    bool m_ringing = false;
    bool m_relayConnected = false;
    bool m_lostModeActive = false;
    bool m_ntfyConnected = false;
    QString m_pairingError;
};
