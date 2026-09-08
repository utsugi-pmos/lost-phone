// SPDX-License-Identifier: GPL-2.0-or-later
//
// The on-disk shape of the whole feature, in one place. Everything reads and
// writes ~/.config/lost-phonerc through here: the settings UI (backend.cpp), the
// Settings module and the daemon (lost-phoned). One schema, one loader, so the
// front-end that WRITES a capability and the daemon that ACTS on it can never
// disagree about where it lives or what it means. Same arrangement as
// smart-unlock, and for the same reason.
//
// THE CAPABILITY MATRIX IS THE SECURITY MODEL
// -------------------------------------------
// There is no single "on" switch. Every power the phone can be given is a
// separate flag, per channel, and the daemon checks the flag before it obeys
// -- so a leaked SMS key, or later a compromised relay, can only do what was
// explicitly turned on here. Everything except ringing over SMS ships off.

#pragma once

#include <QDateTime>
#include <QString>

// What a command asks the phone to do. Kept as an enum and not a string so the
// capability check and the command parser cannot drift apart.
enum class Capability {
    Ring,    // make noise, loudly, over the top of a silenced phone
    Locate,  // answer with a position
    Lock,    // lock the session and show a message (phase 4)

    // Power off the whole phone. It is its own capability and not a case of Lock
    // because it is a different species: it is the ONLY order that switches off
    // all the others. A powered-off phone does not ring, does not say where it is
    // and obeys no one -- you included. There is no way back from any panel.
    Power,
};

// Where a command came in through. Each channel carries its own permissions:
// ringing over SMS is safe, locking over SMS is not, and the difference is
// exactly this.
enum class Channel {
    Sms,
    Ntfy,   // somebody else's ntfy, for people who already self-host one
    Relay,  // phase 3
    Lan,    // phase 2
};

// The last position the phone managed to work out, kept so a phone whose
// battery has since died can still say where it was. This is the field that
// actually solves the real case, so it is written on every successful fix.
struct LastKnown {
    bool valid = false;
    double latitude = 0.0;
    double longitude = 0.0;
    int accuracyMeters = 0;
    QString source;  // "gnss", "wifi", "cell", "ip"
    QDateTime when;
};

struct Settings {
    // Master switch. With it off the daemon keeps running and keeps answering
    // nothing: it is the state the package installs in, so putting the package
    // on a phone never gives anyone a way to make it ring.
    bool enabled = false;

    // --- the SMS channel ----------------------------------------------------
    // The key is the only secret on this channel and it travels in clear
    // through the carrier. That is a deliberate, stated trade-off: it is the
    // only channel that survives having no data, and it is accepted from any
    // number precisely because the phone you borrow to look for yours will
    // never be on a whitelist.
    bool smsEnabled = true;
    QString smsKey;  // empty means the channel is dead, whatever else is set

    bool smsAllowRing = true;  // safe: the worst case is noise

    // On by default, and the key is what protects it.
    //
    // It was opt-in, and that was the wrong shape for the problem: "where is my
    // phone" is THE question you send by SMS, and SMS is the only channel left
    // when the phone has no data. A locate switch that is off by default means
    // the one thing you need at the worst moment answers "battery 34%".
    //
    // What guards it is the key -- generated, eight characters, and empty until
    // somebody sets one, which keeps the whole channel dead. That is a real
    // lock; a second switch on top of it was just a way to look careful.
    bool smsAllowLocate = true;

    bool smsAllowLock = false;  // opt-in: a leaked key could lock you out

    // Answer by SMS to whoever asked. Costs money, and there is no way to know
    // whether the reply arrived.
    bool smsReply = true;

    // Delete the command message once it has been acted on. Two daemons on this
    // phone consume the same SMS (spacebar-daemon and kde-telephony-daemon), so
    // without this the command sits in the messages app in plain sight.
    bool smsDeleteAfter = true;

    // --- ringing ------------------------------------------------------------
    int ringSeconds = 120;         // stops on its own; a phone that screams
                                   // forever is a phone with a flat battery
    bool ringRequiresPin = true;   // only the PIN or the panel can silence it

    // What it plays. A path to a sound file, or the literal "tono" for the
    // generated two-frequency warble.
    //
    // Blip by default -- one second, from the sound theme the phone already
    // ships. Short and repeated beats long and pretty here: the ear places a
    // sound by its ONSETS, and a one-second clip looping gives sixty of them a
    // minute where a forty-second ringtone gives one and a half. The generated
    // tone is uglier still and better again by that measure, which is why it
    // stays available rather than being replaced.
    QString ringSound = QStringLiteral("/usr/share/sounds/plasma-mobile/stereo/notifications/Blip.oga");

    // How loud, as a percentage. A hundred by default, because a phone you
    // cannot hear is a phone you do not find -- but it is your phone and your
    // neighbours.
    int ringVolume = 100;

    // --- the locate cascade -------------------------------------------------
    // Each rung can be switched off on its own. They are tried in order and the
    // best answer wins, always carrying its accuracy: a position without an
    // accuracy is a lie with coordinates.
    bool locateGnss = true;
    int gnssTimeoutSeconds = 90;
    bool locateWifi = true;
    bool locateCell = true;
    bool locateLastKnown = true;

    // --- the relay ----------------------------------------------------------
    // The channel that works with the phone out in the street. The token is
    // what the phone got when it was paired; without it the channel is dead,
    // exactly as an empty SMS key kills that one.
    bool relayEnabled = false;
    QString relayUrl;      // "https://find.example.com"
    QString relayDeviceId;
    QString relayToken;

    // The relay certificate's SHA-256 fingerprint, pinned during pairing.
    //
    // The relay is reached by IP and port, with no domain, so no public
    // authority can sign a certificate for it: it uses its own. Trusting it
    // "just because" would be worse than not encrypting, because it would look
    // secure.
    //
    // The model is SSH's: the certificate seen the FIRST time is accepted --
    // during pairing, which is when there already is a shared secret, the
    // four-digit code -- and from then on only that one. An attacker who
    // interposes later presents another and is rejected; to sneak in they would
    // have had to be there already during pairing.
    QString relayFingerprint;

    // How long to sleep before polling again after a poll comes back empty.
    // ZERO -- reconnect at once -- is the default, and the earlier default of
    // thirty minutes was a mistake worth spelling out.
    //
    // The long poll already holds the connection open for a minute on the
    // server, so this sleep is time the phone spends UNREACHABLE. At thirty
    // minutes it was listening for sixty seconds out of every thirty-one: three
    // per cent of the time. Press Ring in the panel and it might arrive half an
    // hour later, which for a lost phone is the same as never. The relay's own
    // README promised "an order reaches a phone that was waiting in 11 ms" --
    // true, and useless, because the phone was almost never waiting.
    //
    // The reasoning for the old default was battery, and it was not wrong about
    // the cost -- it was wrong about the trade. An idle TCP connection that
    // wakes once a minute is cheap; a phone you cannot reach is worthless. The
    // setting stays for whoever wants it, and the screen now says plainly that
    // every minute here is a minute the phone ignores you.
    int relayIdleMinutes = 0;

    // How often it refreshes its position on the panel, on its own.
    //
    // The heartbeats already say the phone is STILL ALIVE -- that is where "seen
    // a minute ago" comes from -- but the position was only updated when somebody
    // asked for it. The result was a panel that said "alive a minute ago" with a
    // map from an hour ago, which is the kind of data that looks useful and is
    // not.
    //
    // The cost is real and that is why it is not zero: getting a GPS fix can take
    // a good half minute of radio on. Half an hour between fixes works out to
    // five per cent of the time.
    int relayReportMinutes = 30;

    // And in lost mode, much more often: there the battery is no longer what is
    // being protected.
    int relayReportLostMinutes = 5;

    bool relayAllowRing = true;
    bool relayAllowLocate = false;
    bool relayAllowLock = false;

    // Remote power-off. Off by default, and only from the panel: it is the order
    // that takes all the others down with it, so whoever wants it has to go and
    // find it.
    bool relayAllowPower = false;

    // --- the home channel ---------------------------------------------------
    // No server at all: an HTTP listener announced over mDNS, for the case that
    // actually happens -- the phone under a cushion, on the same Wi-Fi as you.
    // The token is generated by the phone, and is NOT the SMS key: being on the
    // same network proves nothing about who you are.
    bool lanEnabled = false;
    QString lanToken;
    // The simple door: GET /sonar?clave=... and nothing more.
    //
    // It exists because the token with an Authorization header serves a program
    // and not a person: it cannot be pasted into the browser bar, put on a home
    // automation button, or called from a watch. With this, anything that can do
    // a GET can make the phone ring.
    //
    // The price, and it is real: a key in a URL ends up in the browser history,
    // in the logs of any intermediary and in the clipboard. That is why it ships
    // OFF, is its own key -- not the SMS one nor the home channel's token -- and
    // only reaches what the capability matrix already allows.
    bool lanSimpleEnabled = false;
    QString lanSimpleKey;

    bool lanAllowRing = true;
    bool lanAllowLocate = false;
    bool lanAllowLock = false;

    // --- somebody else's ntfy -----------------------------------------------
    // For people who already run one. Two topics: the phone listens on one and
    // answers on the other, so the answer arrives as a push notification on
    // whatever device you happen to be holding.
    //
    // The key is separate from every other channel's, on purpose. On a public
    // ntfy server the topic name is the only thing standing between a stranger
    // and your phone, and ntfy's own documentation says to treat topics as
    // secrets -- which is fine for "the backup finished" and not for "make this
    // phone scream".
    bool ntfyEnabled = false;
    QString ntfyServer = QStringLiteral("https://ntfy.sh");
    QString ntfyTopic;       // where orders come in
    QString ntfyReplyTopic;  // where answers go out; empty means answer nothing
    QString ntfyToken;       // only for a protected topic
    QString ntfyKey;
    bool ntfyAllowRing = true;
    bool ntfyAllowLocate = false;
    bool ntfyAllowLock = false;

    LastKnown lastKnown;
};

namespace LostPhoneConfig
{
// ~/.config/lost-phonerc, resolved through QStandardPaths so it follows
// XDG_CONFIG_HOME like every other KConfig file on the phone.
QString filePath();

Settings load();
void save(const Settings &settings);

// Written by the daemon alone, on every successful fix, without disturbing
// anything the user may be editing in the settings UI at that moment.
void saveLastKnown(const LastKnown &fix);

// Written by the daemon alone, the moment a pairing succeeds.
// The fingerprint is saved TOGETHER with the token, in the same write and at the
// same moment: they are the two halves of the same trust. Saving one without the
// other would leave a phone that trusts a server it cannot verify, or the other
// way round.
void savePairing(const QString &deviceId, const QString &token, const QString &fingerprint);

// The one place that decides whether a command is allowed. Both the SMS parser
// and, later, the relay client go through here -- so adding a channel cannot
// accidentally add a permission.
bool allows(const Settings &settings, Channel channel, Capability what);
}
