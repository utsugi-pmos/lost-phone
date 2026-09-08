// SPDX-License-Identifier: GPL-2.0-or-later

#include "backend.h"

#include <QDBusConnection>
#include <QDir>
#include <QNetworkInterface>
#include <QFileInfo>

#include <QDBusMessage>
#include <QDBusReply>
#include <QRandomGenerator>
#include <QTimer>

namespace
{
// No 0/O, no 1/l/I: the point of a generated key is that it gets typed
// correctly into a strange phone under stress, once.
const QString kAlphabet = QStringLiteral("abcdefghjkmnpqrstuvwxyz23456789");
constexpr int kKeyLength = 8;

// QDBusInterface is a QObject and so cannot be returned by value; it is built
// where it is used. Cheap enough -- these calls happen on a button press and on
// a two-second poll, not in a loop.
QDBusMessage callDaemon(const QString &method, const QVariantList &args = {})
{
    QDBusMessage message =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.lostphone"),
                                       QStringLiteral("/LostPhone"),
                                       QStringLiteral("org.kde.LostPhone"), method);
    message.setArguments(args);
    return QDBusConnection::sessionBus().call(message, QDBus::Block, 1000);
}
}

LostPhoneBackend::LostPhoneBackend(QObject *parent)
    : QObject(parent)
{
    load();
    refreshDaemon();
}

void LostPhoneBackend::load()
{
    m_saved = LostPhoneConfig::load();
    m_working = m_saved;
    m_dirty = false;
    Q_EMIT changed();
    Q_EMIT dirtyChanged();
}

void LostPhoneBackend::save()
{
    LostPhoneConfig::save(m_working);
    m_saved = m_working;
    m_dirty = false;
    Q_EMIT dirtyChanged();

    // The daemon re-reads on its own through the file watch; ask it for its
    // state a moment later so the UI shows the result rather than the intent.
    QTimer::singleShot(500, this, &LostPhoneBackend::refreshDaemon);
}

void LostPhoneBackend::restoreDefaults()
{
    // Neither the key nor the pairing is a preference to reset: losing them to
    // a Defaults button would unpair the phone from its relay.
    const QString key = m_working.smsKey;
    const QString url = m_working.relayUrl;
    const QString lanToken = m_working.lanToken;
    const QString deviceId = m_working.relayDeviceId;
    const QString token = m_working.relayToken;
    m_working = Settings();
    m_working.smsKey = key;
    m_working.relayUrl = url;
    m_working.relayDeviceId = deviceId;
    m_working.relayToken = token;
    m_working.lanToken = lanToken;
    markDirty();
    Q_EMIT changed();
}

// 43 characters out of the full alphabet. Not meant to be read aloud.
QString LostPhoneBackend::randomToken()
{
    const QString alfabeto = QStringLiteral(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    QString token;
    token.reserve(43);
    for (int i = 0; i < 43; ++i) {
        token.append(alfabeto.at(int(QRandomGenerator::system()->bounded(alfabeto.size()))));
    }
    return token;
}

QString LostPhoneBackend::suggestKey() const
{
    QString key;
    key.reserve(kKeyLength);
    for (int i = 0; i < kKeyLength; ++i) {
        key.append(kAlphabet.at(int(QRandomGenerator::system()->bounded(kAlphabet.size()))));
    }
    return key;
}

// Read from disk every time it is asked for. The alternative -- a list written
// in the code -- goes stale the moment somebody installs another theme, and it
// would do so silently: the chosen melody would still be there and the list
// would not.
QVariantList LostPhoneBackend::sonidos() const
{
    QVariantList fuera;

    // The generated tone first, because it does not depend on any file existing
    // and is the only one that stays if the sound theme is uninstalled.
    fuera.append(QVariantMap{{QStringLiteral("nombre"), QStringLiteral("Shrill tone")},
                             {QStringLiteral("ruta"), QStringLiteral("tono")}});

    const QStringList carpetas = {
        QStringLiteral("/usr/share/sounds/plasma-mobile/stereo/notifications"),
        QStringLiteral("/usr/share/sounds/plasma-mobile/stereo/ringtones"),
        QStringLiteral("/usr/share/sounds/freedesktop/stereo"),
    };
    for (const QString &carpeta : carpetas) {
        QDir dir(carpeta);
        const QStringList ficheros =
            dir.entryList({QStringLiteral("*.oga"), QStringLiteral("*.ogg"),
                           QStringLiteral("*.wav")},
                          QDir::Files, QDir::Name);
        for (const QString &fichero : ficheros) {
            fuera.append(QVariantMap{
                {QStringLiteral("nombre"), QFileInfo(fichero).completeBaseName()},
                {QStringLiteral("ruta"), dir.filePath(fichero)}});
        }
    }
    return fuera;
}

QString LostPhoneBackend::lastKnownSummary() const
{
    const LastKnown &l = m_saved.lastKnown;
    if (!l.valid) {
        return QString();
    }
    return QStringLiteral("%1, %2 (+-%3 m) -- %4, %5")
        .arg(l.latitude, 0, 'f', 5)
        .arg(l.longitude, 0, 'f', 5)
        .arg(l.accuracyMeters)
        .arg(l.source, l.when.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
}

void LostPhoneBackend::testRing()
{
    // Three seconds. Long enough to recognise, short enough not to be a prank
    // on whoever is in the room.
    callDaemon(QStringLiteral("TestRing"), {3});
    QTimer::singleShot(300, this, &LostPhoneBackend::refreshDaemon);
}

void LostPhoneBackend::stopRing()
{
    callDaemon(QStringLiteral("StopRing"));
    QTimer::singleShot(300, this, &LostPhoneBackend::refreshDaemon);
}

void LostPhoneBackend::leaveLostMode()
{
    callDaemon(QStringLiteral("LeaveLostMode"));
    QTimer::singleShot(300, this, &LostPhoneBackend::refreshDaemon);
}

void LostPhoneBackend::refreshDaemon()
{
    const QDBusMessage reply = callDaemon(QStringLiteral("Ringing"));
    const bool running = reply.type() == QDBusMessage::ReplyMessage;
    const bool isRinging = running && !reply.arguments().isEmpty()
        && reply.arguments().constFirst().toBool();

    bool relayConnected = false;
    bool ntfyConnected = false;
    bool lost = false;
    QString pairingError;
    if (running) {
        const QDBusMessage lostReply = callDaemon(QStringLiteral("LostModeActive"));
        lost = lostReply.type() == QDBusMessage::ReplyMessage
            && !lostReply.arguments().isEmpty() && lostReply.arguments().constFirst().toBool();
        const QDBusMessage conn = callDaemon(QStringLiteral("RelayConnected"));
        relayConnected = conn.type() == QDBusMessage::ReplyMessage
            && !conn.arguments().isEmpty() && conn.arguments().constFirst().toBool();
        const QDBusMessage ntfy = callDaemon(QStringLiteral("NtfyConnected"));
        ntfyConnected = ntfy.type() == QDBusMessage::ReplyMessage && !ntfy.arguments().isEmpty()
            && ntfy.arguments().constFirst().toBool();
        const QDBusMessage err = callDaemon(QStringLiteral("PairingError"));
        if (err.type() == QDBusMessage::ReplyMessage && !err.arguments().isEmpty()) {
            pairingError = err.arguments().constFirst().toString();
        }
    }

    if (running != m_daemonRunning || isRinging != m_ringing
        || relayConnected != m_relayConnected || pairingError != m_pairingError
        || lost != m_lostModeActive || ntfyConnected != m_ntfyConnected) {
        m_daemonRunning = running;
        m_ringing = isRinging;
        m_relayConnected = relayConnected;
        m_lostModeActive = lost;
        m_ntfyConnected = ntfyConnected;
        m_pairingError = pairingError;
        Q_EMIT daemonChanged();
    }
}

void LostPhoneBackend::markDirty()
{
    const bool dirty = true;
    if (m_dirty != dirty) {
        m_dirty = dirty;
        Q_EMIT dirtyChanged();
    }
}

#define SETTER(Name, member, Type)                                                                 \
    void LostPhoneBackend::set##Name(Type value)                                                   \
    {                                                                                              \
        if (m_working.member == value) {                                                           \
            return;                                                                                \
        }                                                                                          \
        m_working.member = value;                                                                  \
        markDirty();                                                                               \
        Q_EMIT changed();                                                                          \
    }

SETTER(Enabled, enabled, bool)
SETTER(SmsEnabled, smsEnabled, bool)
SETTER(SmsAllowRing, smsAllowRing, bool)
SETTER(SmsAllowLocate, smsAllowLocate, bool)
SETTER(SmsAllowLock, smsAllowLock, bool)
SETTER(SmsReply, smsReply, bool)
SETTER(SmsDeleteAfter, smsDeleteAfter, bool)
SETTER(RingSeconds, ringSeconds, int)
SETTER(RingSound, ringSound, const QString &)
SETTER(RingVolume, ringVolume, int)
SETTER(LocateGnss, locateGnss, bool)
SETTER(LocateWifi, locateWifi, bool)
SETTER(LocateCell, locateCell, bool)
SETTER(LocateLastKnown, locateLastKnown, bool)
SETTER(RelayEnabled, relayEnabled, bool)
SETTER(RelayIdleMinutes, relayIdleMinutes, int)
SETTER(RelayAllowRing, relayAllowRing, bool)
SETTER(RelayAllowLocate, relayAllowLocate, bool)
SETTER(RelayAllowLock, relayAllowLock, bool)
SETTER(RelayAllowPower, relayAllowPower, bool)
SETTER(LanAllowRing, lanAllowRing, bool)
SETTER(LanSimpleKey, lanSimpleKey, const QString &)

void LostPhoneBackend::setLanSimpleEnabled(bool value)
{
    if (m_working.lanSimpleEnabled == value) {
        return;
    }
    m_working.lanSimpleEnabled = value;
    // Turning it on with no key would leave a switch that says on and a door
    // that does not open. One is minted, and it is short on purpose: it is typed into a URL.
    if (value && m_working.lanSimpleKey.isEmpty()) {
        m_working.lanSimpleKey = suggestKey();
    }
    markDirty();
    Q_EMIT changed();
}

// This phone's Wi-Fi address, not the cable's nor loopback's: it is the one
// that will work from the computer next to it.
QString LostPhoneBackend::lanSimpleUrl() const
{
    if (m_working.lanSimpleKey.isEmpty()) {
        return {};
    }
    QString direccion;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &nic : interfaces) {
        if (!nic.flags().testFlag(QNetworkInterface::IsUp)
            || nic.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry &entrada : nic.addressEntries()) {
            const QHostAddress ip = entrada.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            // The USB link (172.16.42.x) exists only while the cable is there,
            // and showing it as "your link" would be handing out an address
            // that almost never works. Wi-Fi is the one that gets used.
            if (nic.name().startsWith(QLatin1String("wlan"))) {
                direccion = ip.toString();
            } else if (direccion.isEmpty()) {
                direccion = ip.toString();
            }
        }
    }
    if (direccion.isEmpty()) {
        return {};
    }
    return QStringLiteral("http://%1:8479/sonar?clave=%2").arg(direccion, m_working.lanSimpleKey);
}
SETTER(NtfyAllowRing, ntfyAllowRing, bool)
SETTER(NtfyAllowLocate, ntfyAllowLocate, bool)
SETTER(NtfyServer, ntfyServer, const QString &)
SETTER(NtfyTopic, ntfyTopic, const QString &)
SETTER(NtfyReplyTopic, ntfyReplyTopic, const QString &)
SETTER(NtfyKey, ntfyKey, const QString &)

void LostPhoneBackend::setNtfyEnabled(bool value)
{
    if (m_working.ntfyEnabled == value) {
        return;
    }
    m_working.ntfyEnabled = value;
    // Turning it on with no key or no topics would leave a switch that says on
    // and a channel that is off. Fill in whatever is missing.
    if (value) {
        if (m_working.ntfyKey.isEmpty()) {
            m_working.ntfyKey = suggestKey();
        }
        if (m_working.ntfyTopic.isEmpty()) {
            m_working.ntfyTopic = suggestTopic();
        }
        if (m_working.ntfyReplyTopic.isEmpty()) {
            m_working.ntfyReplyTopic = suggestTopic();
        }
    }
    markDirty();
    Q_EMIT changed();
}

QString LostPhoneBackend::suggestTopic() const
{
    // Long enough that guessing it is not a strategy: on a public server the
    // topic name is the only door, and ntfy says so itself.
    return QStringLiteral("lost-phone-") + randomToken().left(20).toLower();
}
SETTER(LanAllowLocate, lanAllowLocate, bool)

void LostPhoneBackend::setLanEnabled(bool value)
{
    if (m_working.lanEnabled == value) {
        return;
    }
    m_working.lanEnabled = value;
    if (value && m_working.lanToken.isEmpty()) {
        // Long and random, not the eight friendly characters of the SMS key:
        // nobody types this one -- the laptop command fetches it over SSH.
        m_working.lanToken = randomToken();
    }
    markDirty();
    Q_EMIT changed();
}

void LostPhoneBackend::setRelayUrl(const QString &value)
{
    const QString clean = value.trimmed();
    if (m_working.relayUrl == clean) {
        return;
    }
    m_working.relayUrl = clean;
    markDirty();
    Q_EMIT changed();
}

void LostPhoneBackend::pair(const QString &code)
{
    // The address is saved first. Pairing against a relay whose address is
    // still only in the text field would leave the phone with a token and
    // nowhere to send it.
    if (m_working.relayUrl != m_saved.relayUrl) {
        save();
    }
    callDaemon(QStringLiteral("Pair"),
               {m_working.relayUrl, code, QStringLiteral("surya")});
    // Pairing is a round trip to the relay; give it a moment before asking the
    // daemon how it went.
    QTimer::singleShot(2500, this, [this] {
        load();
        refreshDaemon();
    });
}

void LostPhoneBackend::setSmsKey(const QString &value)
{
    // Trimmed and lower-cased on the way in, because it is compared as a whole
    // word against something a stranger typed on a phone keyboard that likes to
    // capitalise the first letter of every message.
    const QString clean = value.trimmed().toLower();
    if (m_working.smsKey == clean) {
        return;
    }
    m_working.smsKey = clean;
    markDirty();
    Q_EMIT changed();
}
