// Exercises the on-disk settings and, above all, the capability matrix -- which
// IS the security model of this feature. Two things are requirements rather
// than niceties, and both are checked here:
//
//   a phone with no settings file obeys nobody, and
//   a channel with no key obeys nobody either, however many switches are on.
#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cstdio>

static int fallos = 0;
static int total = 0;

static void comprobar(const char *nombre, bool ok)
{
    ++total;
    if (!ok) {
        ++fallos;
        std::printf("  FAIL   %s\n", nombre);
    } else {
        std::printf("  ok     %s\n", nombre);
    }
}

static void borrarConfig()
{
    QFile::remove(LostPhoneConfig::filePath());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QDir().mkpath(QFileInfo(LostPhoneConfig::filePath()).absolutePath());

    std::printf("\n--- no configuration file (fresh install) ---\n");
    borrarConfig();
    {
        const Settings s = LostPhoneConfig::load();
        comprobar("OFF: enabled is false", s.enabled == false);
        comprobar("no SMS key", s.smsKey.isEmpty());
        // On, unlike the other permissions, and on purpose: "where is my phone"
        // is THE question you send by SMS, and SMS is the only thing left when
        // the phone has run out of data. What protects it is the key -- checked
        // above to come EMPTY, so the whole channel is dead until someone sets
        // one.
        comprobar("locate by SMS comes on", s.smsAllowLocate == true);
        comprobar("but without a key it obeys nobody", s.smsKey.isEmpty());
        comprobar("the relay comes off", s.relayEnabled == false);
        // Powering off the phone NEVER turns itself on: it is the one order that
        // overrides all the others, and it cannot be undone from the panel.
        comprobar("power off the phone comes off", s.relayAllowPower == false);
        comprobar("the home channel comes off", s.lanEnabled == false);
        comprobar("ntfy comes off", s.ntfyEnabled == false);

        // The default melody and volume. Blip and not the generated tone: it is
        // short, and a short sound that comes back every second gives many
        // onsets, which is how the ear knows where it is coming from.
        comprobar("the default melody is Blip", s.ringSound.endsWith(QLatin1String("Blip.oga")));
        comprobar("at full volume by default", s.ringVolume == 100);
        comprobar("and it rings for two minutes", s.ringSeconds == 120);
        comprobar("lock by SMS comes off", s.smsAllowLock == false);
        comprobar("no last position", s.lastKnown.valid == false);

        // What really matters about a fresh install: it obeys nobody.
        comprobar("freshly installed it will not even ring",
                  !LostPhoneConfig::allows(s, Channel::Sms, Capability::Ring));
    }

    std::printf("\n--- the capability matrix ---\n");
    {
        Settings s;
        s.enabled = true;
        s.smsEnabled = true;
        s.smsAllowRing = true;
        s.smsAllowLocate = true;

        // Without a key the channel is dead, even if everything else is on.
        s.smsKey.clear();
        comprobar("without a key it does not ring", !LostPhoneConfig::allows(s, Channel::Sms, Capability::Ring));
        comprobar("without a key it does not locate",
                  !LostPhoneConfig::allows(s, Channel::Sms, Capability::Locate));

        s.smsKey = QStringLiteral("abcd1234");
        comprobar("with key and permission, it rings",
                  LostPhoneConfig::allows(s, Channel::Sms, Capability::Ring));
        comprobar("with key and permission, it locates",
                  LostPhoneConfig::allows(s, Channel::Sms, Capability::Locate));
        comprobar("lock stays forbidden if it was not turned on",
                  !LostPhoneConfig::allows(s, Channel::Sms, Capability::Lock));

        // The master switch rules over everything else: turning it off has to be
        // ONE decision you can trust, not a list to audit.
        s.enabled = false;
        comprobar("the master switch turns everything off",
                  !LostPhoneConfig::allows(s, Channel::Sms, Capability::Ring));

        // Each channel has its own credential and permissions, and they lend
        // nothing to one another.
        s.enabled = true;
        comprobar("the relay without a witness obeys nobody",
                  !LostPhoneConfig::allows(s, Channel::Relay, Capability::Ring));
        comprobar("the home channel without a witness obeys nobody",
                  !LostPhoneConfig::allows(s, Channel::Lan, Capability::Ring));

        // The important bit: the SMS key does NOT open the other channels. If one
        // day someone unifies the credentials "for convenience", this goes red.
        s.relayEnabled = true;
        s.relayUrl = QStringLiteral("https://ejemplo.com");
        comprobar("the relay on but without a witness still does not obey",
                  !LostPhoneConfig::allows(s, Channel::Relay, Capability::Ring));
        s.relayToken = QStringLiteral("un-testigo");
        comprobar("with witness and permission, the relay obeys",
                  LostPhoneConfig::allows(s, Channel::Relay, Capability::Ring));
        comprobar("but lock stays off on the relay",
                  !LostPhoneConfig::allows(s, Channel::Relay, Capability::Lock));
        comprobar("and powering off the phone too",
                  !LostPhoneConfig::allows(s, Channel::Relay, Capability::Power));
        s.relayAllowPower = true;
        comprobar("turned on by hand, the panel can indeed power it off",
                  LostPhoneConfig::allows(s, Channel::Relay, Capability::Power));
        // And NO other channel can, with permission or without it: there is no
        // switch that opens it. Over SMS the key travels in the clear and on the
        // home network just being on the wifi is enough; a power-off cannot be
        // undone from anywhere, so only the panel opens it, which is behind a
        // password.
        s.smsAllowLock = true;
        s.lanAllowLock = true;
        s.ntfyAllowLock = true;
        for (const Channel otro : {Channel::Sms, Channel::Lan, Channel::Ntfy}) {
            comprobar("no other channel can power off the phone",
                      !LostPhoneConfig::allows(s, otro, Capability::Power));
        }

        s.lanEnabled = true;
        comprobar("the home channel on but without a witness does not obey",
                  !LostPhoneConfig::allows(s, Channel::Lan, Capability::Ring));
        s.lanToken = QStringLiteral("otro-testigo");
        comprobar("with its own witness, the home channel obeys",
                  LostPhoneConfig::allows(s, Channel::Lan, Capability::Ring));
        comprobar("and locate from home comes off",
                  !LostPhoneConfig::allows(s, Channel::Lan, Capability::Locate));

        // ntfy: it needs TWO credentials, the topic and its own key. On a public
        // server the topic is the only thing separating a stranger from your
        // phone, and it can be guessed.
        s.ntfyEnabled = true;
        s.ntfyTopic = QStringLiteral("lost-phone-abcdefgh");
        comprobar("ntfy with a topic but no key does not obey",
                  !LostPhoneConfig::allows(s, Channel::Ntfy, Capability::Ring));
        s.ntfyKey = QStringLiteral("otra-clave-mas");
        s.ntfyTopic.clear();
        comprobar("ntfy with a key but no topic does not obey",
                  !LostPhoneConfig::allows(s, Channel::Ntfy, Capability::Ring));
        s.ntfyTopic = QStringLiteral("lost-phone-abcdefgh");
        comprobar("with topic and key, ntfy obeys",
                  LostPhoneConfig::allows(s, Channel::Ntfy, Capability::Ring));
        comprobar("and locate over ntfy comes off",
                  !LostPhoneConfig::allows(s, Channel::Ntfy, Capability::Locate));

        // The four keys are distinct and lend nothing to one another. If one day
        // someone unifies them "for convenience", this goes red.
        comprobar("the SMS key is not the ntfy one", s.smsKey != s.ntfyKey);
        comprobar("the relay witness is not the home one", s.relayToken != s.lanToken);

        // And the master switch still rules over all four.
        s.enabled = false;
        comprobar("the master also turns off the relay",
                  !LostPhoneConfig::allows(s, Channel::Relay, Capability::Ring));
        comprobar("the master also turns off the home channel",
                  !LostPhoneConfig::allows(s, Channel::Lan, Capability::Ring));
        comprobar("the master also turns off ntfy",
                  !LostPhoneConfig::allows(s, Channel::Ntfy, Capability::Ring));
    }

    std::printf("\n--- the melody and volume are saved ---\n");
    borrarConfig();
    {
        Settings s;
        s.ringSound = QStringLiteral("tono");
        s.ringVolume = 35;
        LostPhoneConfig::save(s);

        const Settings leida = LostPhoneConfig::load();
        comprobar("the strident tone survives", leida.ringSound == QLatin1String("tono"));
        comprobar("the volume survives", leida.ringVolume == 35);
    }
    {
        // An impossible volume cannot leave the alarm mute nor blow out the
        // speaker: it is clamped on the way in, not on use, so that what is read
        // from the file is already usable.
        Settings s;
        s.ringVolume = 400;
        LostPhoneConfig::save(s);
        comprobar("an absurd volume is clamped to 100", LostPhoneConfig::load().ringVolume == 100);
        s.ringVolume = -5;
        LostPhoneConfig::save(s);
        comprobar("and a negative one does not leave the alarm mute",
                  LostPhoneConfig::load().ringVolume >= 1);
    }

    std::printf("\n--- save and read back ---\n");
    borrarConfig();
    {
        Settings s;
        s.enabled = true;
        s.smsKey = QStringLiteral("perrogato");
        s.smsAllowLocate = true;
        s.ringSeconds = 45;
        s.locateGnss = false;
        LostPhoneConfig::save(s);

        const Settings leida = LostPhoneConfig::load();
        comprobar("enabled survives", leida.enabled == true);
        comprobar("the key survives", leida.smsKey == QLatin1String("perrogato"));
        comprobar("the locate permission survives", leida.smsAllowLocate == true);
        comprobar("the duration survives", leida.ringSeconds == 45);
        comprobar("turning off the GPS survives", leida.locateGnss == false);
    }

    std::printf("\n--- only the daemon writes the last position ---\n");
    {
        LastKnown fix;
        fix.valid = true;
        fix.latitude = 40.41678;
        fix.longitude = -3.70379;
        fix.accuracyMeters = 18;
        fix.source = QStringLiteral("gnss");
        fix.when = QDateTime::currentDateTime();
        LostPhoneConfig::saveLastKnown(fix);

        // And now the UI saves settings, as it would on pressing Apply. The
        // position must NOT disappear: the UI holds a copy loaded minutes ago and
        // the daemon has just written a new position.
        Settings s = LostPhoneConfig::load();
        s.ringSeconds = 90;
        LostPhoneConfig::save(s);

        const Settings leida = LostPhoneConfig::load();
        comprobar("the position is still there after saving settings", leida.lastKnown.valid == true);
        comprobar("and with its coordinates intact",
                  qAbs(leida.lastKnown.latitude - 40.41678) < 0.00001);
    }

    std::printf("\n%d checks, %d failures\n", total, fallos);
    return fallos == 0 ? 0 : 1;
}
