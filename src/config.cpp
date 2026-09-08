// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <QStandardPaths>

#include <KConfigGroup>
#include <KSharedConfig>

namespace
{
const QLatin1String kConfigName{"lost-phonerc"};

// Re-read from disk on every open. KSharedConfig hands back the SAME object for
// a given name, so a daemon that has been running for days would otherwise keep
// answering with the settings it read at boot -- and the whole handoff from the
// settings UI to the daemon is this file changing underneath it.
KSharedConfig::Ptr openConfig()
{
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(kConfigName);
    cfg->reparseConfiguration();
    return cfg;
}
}

QString LostPhoneConfig::filePath()
{
    // openConfig() with a bare name already lands here; this is only so the
    // daemon can hand the exact path to a QFileSystemWatcher.
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QLatin1Char('/') + kConfigName;
}

Settings LostPhoneConfig::load()
{
    Settings s;
    KSharedConfig::Ptr config = openConfig();

    const KConfigGroup general = config->group(QStringLiteral("General"));
    s.enabled = general.readEntry("Enabled", false);

    const KConfigGroup sms = config->group(QStringLiteral("SMS"));
    s.smsEnabled = sms.readEntry("Enabled", true);
    s.smsKey = sms.readEntry("Key", QString());
    s.smsAllowRing = sms.readEntry("AllowRing", true);
    s.smsAllowLocate = sms.readEntry("AllowLocate", true);
    s.smsAllowLock = sms.readEntry("AllowLock", false);
    s.smsReply = sms.readEntry("Reply", true);
    s.smsDeleteAfter = sms.readEntry("DeleteAfter", true);

    const KConfigGroup ring = config->group(QStringLiteral("Ring"));
    s.ringSeconds = ring.readEntry("Seconds", 120);
    s.ringRequiresPin = ring.readEntry("RequiresPin", true);
    s.ringSound = ring.readEntry(
        "Sound",
        QStringLiteral("/usr/share/sounds/plasma-mobile/stereo/notifications/Blip.oga"));
    s.ringVolume = qBound(1, ring.readEntry("Volume", 100), 100);

    const KConfigGroup locate = config->group(QStringLiteral("Locate"));
    s.locateGnss = locate.readEntry("Gnss", true);
    s.gnssTimeoutSeconds = locate.readEntry("GnssTimeoutSeconds", 90);
    s.locateWifi = locate.readEntry("Wifi", true);
    s.locateCell = locate.readEntry("Cell", true);
    s.locateLastKnown = locate.readEntry("LastKnown", true);

    const KConfigGroup relay = config->group(QStringLiteral("Relay"));
    s.relayEnabled = relay.readEntry("Enabled", false);
    s.relayUrl = relay.readEntry("Url", QString());
    s.relayDeviceId = relay.readEntry("DeviceId", QString());
    s.relayToken = relay.readEntry("Token", QString());
    s.relayFingerprint = relay.readEntry("Fingerprint", QString());
    s.relayIdleMinutes = relay.readEntry("IdleMinutes", 30);
    s.relayReportMinutes = relay.readEntry("ReportMinutes", 30);
    s.relayReportLostMinutes = relay.readEntry("ReportLostMinutes", 5);
    s.relayAllowRing = relay.readEntry("AllowRing", true);
    s.relayAllowLocate = relay.readEntry("AllowLocate", false);
    s.relayAllowLock = relay.readEntry("AllowLock", false);
    s.relayAllowPower = relay.readEntry("AllowPower", false);

    const KConfigGroup ntfy = config->group(QStringLiteral("Ntfy"));
    s.ntfyEnabled = ntfy.readEntry("Enabled", false);
    s.ntfyServer = ntfy.readEntry("Server", QStringLiteral("https://ntfy.sh"));
    s.ntfyTopic = ntfy.readEntry("Topic", QString());
    s.ntfyReplyTopic = ntfy.readEntry("ReplyTopic", QString());
    s.ntfyToken = ntfy.readEntry("Token", QString());
    s.ntfyKey = ntfy.readEntry("Key", QString());
    s.ntfyAllowRing = ntfy.readEntry("AllowRing", true);
    s.ntfyAllowLocate = ntfy.readEntry("AllowLocate", false);
    s.ntfyAllowLock = ntfy.readEntry("AllowLock", false);

    const KConfigGroup lan = config->group(QStringLiteral("LAN"));
    s.lanEnabled = lan.readEntry("Enabled", false);
    s.lanToken = lan.readEntry("Token", QString());
    s.lanSimpleEnabled = lan.readEntry("SimpleEnabled", false);
    s.lanSimpleKey = lan.readEntry("SimpleKey", QString());
    s.lanAllowRing = lan.readEntry("AllowRing", true);
    s.lanAllowLocate = lan.readEntry("AllowLocate", false);
    s.lanAllowLock = lan.readEntry("AllowLock", false);

    const KConfigGroup last = config->group(QStringLiteral("LastKnown"));
    s.lastKnown.valid = last.readEntry("Valid", false);
    s.lastKnown.latitude = last.readEntry("Latitude", 0.0);
    s.lastKnown.longitude = last.readEntry("Longitude", 0.0);
    s.lastKnown.accuracyMeters = last.readEntry("AccuracyMeters", 0);
    s.lastKnown.source = last.readEntry("Source", QString());
    s.lastKnown.when = last.readEntry("When", QDateTime());

    return s;
}

void LostPhoneConfig::save(const Settings &s)
{
    KSharedConfig::Ptr config = openConfig();

    KConfigGroup general = config->group(QStringLiteral("General"));
    general.writeEntry("Enabled", s.enabled);

    KConfigGroup sms = config->group(QStringLiteral("SMS"));
    sms.writeEntry("Enabled", s.smsEnabled);
    sms.writeEntry("Key", s.smsKey);
    sms.writeEntry("AllowRing", s.smsAllowRing);
    sms.writeEntry("AllowLocate", s.smsAllowLocate);
    sms.writeEntry("AllowLock", s.smsAllowLock);
    sms.writeEntry("Reply", s.smsReply);
    sms.writeEntry("DeleteAfter", s.smsDeleteAfter);

    KConfigGroup ring = config->group(QStringLiteral("Ring"));
    ring.writeEntry("Seconds", s.ringSeconds);
    ring.writeEntry("RequiresPin", s.ringRequiresPin);
    ring.writeEntry("Sound", s.ringSound);
    ring.writeEntry("Volume", s.ringVolume);

    KConfigGroup locate = config->group(QStringLiteral("Locate"));
    locate.writeEntry("Gnss", s.locateGnss);
    locate.writeEntry("GnssTimeoutSeconds", s.gnssTimeoutSeconds);
    locate.writeEntry("Wifi", s.locateWifi);
    locate.writeEntry("Cell", s.locateCell);
    locate.writeEntry("LastKnown", s.locateLastKnown);

    KConfigGroup relay = config->group(QStringLiteral("Relay"));
    relay.writeEntry("Enabled", s.relayEnabled);
    relay.writeEntry("Url", s.relayUrl);
    relay.writeEntry("IdleMinutes", s.relayIdleMinutes);
    relay.writeEntry("ReportMinutes", s.relayReportMinutes);
    relay.writeEntry("ReportLostMinutes", s.relayReportLostMinutes);
    relay.writeEntry("AllowRing", s.relayAllowRing);
    relay.writeEntry("AllowLocate", s.relayAllowLocate);
    relay.writeEntry("AllowLock", s.relayAllowLock);
    relay.writeEntry("AllowPower", s.relayAllowPower);

    // DeviceId and Token are deliberately NOT written here, for the same reason
    // as LastKnown: they are the daemon's, written once when pairing succeeds.
    // The settings UI holds a copy loaded minutes ago, and pressing Apply must
    // never unpair a phone that paired itself in the meantime.

    KConfigGroup ntfy = config->group(QStringLiteral("Ntfy"));
    ntfy.writeEntry("Enabled", s.ntfyEnabled);
    ntfy.writeEntry("Server", s.ntfyServer);
    ntfy.writeEntry("Topic", s.ntfyTopic);
    ntfy.writeEntry("ReplyTopic", s.ntfyReplyTopic);
    ntfy.writeEntry("Token", s.ntfyToken);
    ntfy.writeEntry("Key", s.ntfyKey);
    ntfy.writeEntry("AllowRing", s.ntfyAllowRing);
    ntfy.writeEntry("AllowLocate", s.ntfyAllowLocate);
    ntfy.writeEntry("AllowLock", s.ntfyAllowLock);

    KConfigGroup lan = config->group(QStringLiteral("LAN"));
    lan.writeEntry("Enabled", s.lanEnabled);
    lan.writeEntry("Token", s.lanToken);
    lan.writeEntry("SimpleEnabled", s.lanSimpleEnabled);
    lan.writeEntry("SimpleKey", s.lanSimpleKey);
    lan.writeEntry("AllowRing", s.lanAllowRing);
    lan.writeEntry("AllowLocate", s.lanAllowLocate);
    lan.writeEntry("AllowLock", s.lanAllowLock);

    // LastKnown is deliberately NOT written here. The settings UI must never
    // overwrite a fix the daemon just took: the two write the same file from
    // two processes, and the UI holds a copy loaded minutes ago.
    config->sync();
}

void LostPhoneConfig::savePairing(const QString &deviceId, const QString &token,
                                  const QString &fingerprint)
{
    KSharedConfig::Ptr config = openConfig();
    KConfigGroup relay = config->group(QStringLiteral("Relay"));
    relay.writeEntry("DeviceId", deviceId);
    relay.writeEntry("Token", token);
    relay.writeEntry("Fingerprint", fingerprint);
    // Pairing is what turns the channel on: having a token and still being
    // ignored would be the most confusing possible outcome of typing the code.
    relay.writeEntry("Enabled", !token.isEmpty());
    config->sync();
}

void LostPhoneConfig::saveLastKnown(const LastKnown &fix)
{
    KSharedConfig::Ptr config = openConfig();
    KConfigGroup last = config->group(QStringLiteral("LastKnown"));
    last.writeEntry("Valid", fix.valid);
    last.writeEntry("Latitude", fix.latitude);
    last.writeEntry("Longitude", fix.longitude);
    last.writeEntry("AccuracyMeters", fix.accuracyMeters);
    last.writeEntry("Source", fix.source);
    last.writeEntry("When", fix.when);
    config->sync();
}

bool LostPhoneConfig::allows(const Settings &s, Channel channel, Capability what)
{
    // The master switch comes first and covers everything: turning the feature
    // off has to be one decision the user can trust, not a list to audit.
    if (!s.enabled) {
        return false;
    }

    switch (channel) {
    case Channel::Sms:
        // A channel with no key is not a channel. Checked here rather than at
        // the parser so no future caller can forget it.
        if (!s.smsEnabled || s.smsKey.isEmpty()) {
            return false;
        }
        switch (what) {
        case Capability::Ring:
            return s.smsAllowRing;
        case Capability::Locate:
            return s.smsAllowLocate;
        case Capability::Lock:
            return s.smsAllowLock;
        case Capability::Power:
            // Power-off ONLY from the panel, which is behind a password. Over
            // SMS the key travels in clear; on the home network being on the
            // Wi-Fi is enough. A power-off cannot be undone from anywhere.
            return false;
        }
        return false;

    case Channel::Relay:
        // Same shape as SMS, and the same first question: a channel with no
        // credential is not a channel.
        if (!s.relayEnabled || s.relayToken.isEmpty() || s.relayUrl.isEmpty()) {
            return false;
        }
        switch (what) {
        case Capability::Ring:
            return s.relayAllowRing;
        case Capability::Locate:
            return s.relayAllowLocate;
        case Capability::Lock:
            return s.relayAllowLock;
        case Capability::Power:
            return s.relayAllowPower;
        }
        return false;

    case Channel::Ntfy:
        // Two credentials have to be there, not one: the topic (which is what
        // ntfy itself relies on) and our own key on top, because a topic on a
        // public server is a secret anybody can guess at.
        if (!s.ntfyEnabled || s.ntfyTopic.isEmpty() || s.ntfyKey.isEmpty()) {
            return false;
        }
        switch (what) {
        case Capability::Ring:
            return s.ntfyAllowRing;
        case Capability::Locate:
            return s.ntfyAllowLocate;
        case Capability::Lock:
            return s.ntfyAllowLock;
        case Capability::Power:
            // Power-off ONLY from the panel, which is behind a password. Over
            // SMS the key travels in clear; on the home network being on the
            // Wi-Fi is enough. A power-off cannot be undone from anywhere.
            return false;
        }
        return false;

    case Channel::Lan:
        // Same first question as the other two: no credential, no channel.
        if (!s.lanEnabled || s.lanToken.isEmpty()) {
            return false;
        }
        switch (what) {
        case Capability::Ring:
            return s.lanAllowRing;
        case Capability::Locate:
            return s.lanAllowLocate;
        case Capability::Lock:
            return s.lanAllowLock;
        case Capability::Power:
            // Power-off ONLY from the panel, which is behind a password. Over
            // SMS the key travels in clear; on the home network being on the
            // Wi-Fi is enough. A power-off cannot be undone from anywhere.
            return false;
        }
        return false;
    }
    return false;
}
