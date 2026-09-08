// SPDX-License-Identifier: GPL-2.0-or-later
//
// lost-phoned -- the engine behind "find my phone".
//
// The settings UI only writes a file. This is what listens, decides whether it
// is allowed, and acts. Without it running nothing happens at all, which is the
// safe default and the reason the installer enables it explicitly.
//
// WHY IT RUNS AS THE USER AND NOT AS ROOT
// ---------------------------------------
// Measured on the surya: `mmcli -m 0 --messaging-list-sms` and
// `mmcli -m 0 --location-get` both work as the ordinary user, and PulseAudio --
// which owns the sound card and is the only way to make noise while it is up --
// only exists inside the session. A root daemon would have to reach back into
// the session for the one thing it most needs to do. So this is a user service,
// exactly like smart-unlockd.
//
// The part that genuinely needs to outlive the session is lost mode, and that
// arrives in phase 4 with a small root helper of its own. Nothing here pretends
// to survive a logout, because it does not.
//
// WHAT ARRIVES, AND FROM WHERE
// ----------------------------
// Phase 1 has one channel: SMS. ModemManager announces every incoming message
// on the system bus, and this listens for that rather than polling, so a
// command acts in the second it lands instead of up to a poll interval later.
//
// Two other daemons on this phone consume the same message -- spacebar-daemon
// and kde-telephony-daemon, both measured running -- so a command SMS will show
// up in the messages app. Deleting it once acted on is the only way to keep it
// out of sight, and it is on by default.

#include "src/config.h"
#include "src/dbus.h"
#include "src/locator.h"
#include "src/lanserver.h"
#include "src/ntfyclient.h"
#include "src/locker.h"
#include "src/lostmode.h"
#include "src/relayclient.h"
#include "src/proceso.h"
#include "src/ringer.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QDir>
#include <QHash>
#include <QPair>
#include <QProcess>
#include <QProcess>
#include <QTimer>

namespace
{
const QString kMmService = QStringLiteral("org.freedesktop.ModemManager1");
const QString kMmPath = QStringLiteral("/org/freedesktop/ModemManager1");
const QString kMessagingIface = QStringLiteral("org.freedesktop.ModemManager1.Modem.Messaging");
const QString kSmsIface = QStringLiteral("org.freedesktop.ModemManager1.Sms");
const QString kPropsIface = QStringLiteral("org.freedesktop.DBus.Properties");

// One command a minute per channel. Not a security control on its own -- the
// capability matrix is that -- but it stops a stuck sender, or somebody having
// fun with a leaked key, from turning the phone into a siren that never rests.
constexpr int kMinSecondsBetweenCommands = 60;

// How long the daemon keeps its hands off everything after being started. It is
// launched by graphical-session.target, so this is the window in which the
// session is still coming up and anything that blocks takes the session -- and
// then the boot watchdog -- down with it.
constexpr int kStartDelaySeconds = 60;

// GetAll and not two Gets. There is a race here that is real and was lost once:
// the messages app stores an incoming SMS and then DELETES it from the modem, so
// every extra round trip is another chance for the object to vanish underneath
// us. One call fetches the text and the number together.
QVariantMap readProperties(const QString &path, const QString &iface)
{
    // Two seconds: this runs in the race against the messages app deleting the
    // SMS out from under us, so a long wait would be worse than useless.
    const QDBusMessage reply = Bus::call(QDBusConnection::systemBus(), kMmService, path,
                                         kPropsIface, QStringLiteral("GetAll"), {iface}, 2000);
    if (!Bus::ok(reply) || reply.arguments().isEmpty()) {
        return {};
    }
    QVariantMap out;
    const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
    arg >> out;
    return out;
}
}

class LostPhoneDaemon : public QObject
{
    Q_OBJECT

    // Name the interface explicitly. Without this Qt invents one from the
    // application name and the class name -- "local.lost_phoned.LostPhoneDaemon"
    // -- so renaming the class would silently break the app's link to the
    // daemon, and the only symptom would be a settings screen that says the
    // service is not running while it is.
    Q_CLASSINFO("D-Bus Interface", "org.kde.LostPhone")

public:
    // NOTHING here may touch D-Bus, bind a socket or talk to the session.
    //
    // This is not caution, it is a lesson this repository already paid for and
    // wrote down, and which I then repeated. smart-unlock's README:
    //
    //   "QDBusInterface blocks. Building one introspects the remote object
    //    synchronously, with D-Bus's default timeout: 25 seconds, of the same
    //    order as the measured reboot cycle."
    //   "All of that, in the constructor. The daemon is launched by
    //    graphical-session.target, that is WHILE the session is coming up."
    //
    // That reboot-looped this very phone once already. lost-phoned is also
    // PartOf=graphical-session.target, and its constructor used to publish an
    // mDNS service through Avahi over the system bus -- synchronously, while
    // the session was still coming up. Same phone, same trap, same result.
    //
    // So the constructor now does only what is safe in any state: read a file
    // and arm timers. Everything that speaks to anything waits for start().
    LostPhoneDaemon()
    {
        m_settings = LostPhoneConfig::load();

        // The settings UI writes the file from another process, so the daemon
        // has to notice rather than be told.
        //
        // The DIRECTORY is watched as well as the file, and that is not belt and
        // braces -- it is the fix for a first-run bug that this feature cannot
        // survive. QFileSystemWatcher::addPath() fails silently on a file that
        // does not exist, and on a fresh install it does not: the package
        // deliberately ships with no configuration at all. So the daemon started
        // watching nothing, and the very first time you set your key the daemon
        // never noticed -- while the settings screen cheerfully reported the
        // service as running. Measured on the surya, 2026-09-04: before a
        // restart "key=no", after it "key=yes", with the file already on disk
        // and correct.
        watchConfig();
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
            // Editors and KConfig replace files rather than rewrite them, so the
            // watch has to be re-armed or it fires exactly once.
            QTimer::singleShot(200, this, [this] {
                watchConfig();
                reload();
            });
        });
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
            QTimer::singleShot(200, this, [this] {
                watchConfig();
                reload();
            });
        });

        connect(&m_locator, &Locator::finished, this, &LostPhoneDaemon::onLocated);

        // The alarm puts its own notification up, with a Parar button, and
        // takes it down when it stops. Wired here so the Ringer stays what it
        // is -- something that makes noise -- and knows nothing about the
        // session.
        connect(&m_ringer, &Ringer::ringingChanged, this, [this](bool ringing) {
            if (!ringing) {
                // The screen closes on its own. What does have to be undone are
                // the radios: whoever rings it and finds it on the sofa has no
                // reason to keep Wi-Fi, WWAN and GPS on for ever. Only what we
                // turned on ourselves is turned back off.
                //
                // NOT in lost mode: there, turning them off when the alarm goes
                // quiet would leave the phone unlocatable just while you are
                // looking for it, which is the opposite of what all this exists
                // for.
                if (m_lost.active) {
                    qInfo() << "lost-phoned: lost mode; leaving the radios on";
                    return;
                }
                m_locator.devolverRadios();
                return;
            }
            // A whole screen, not a notification. A notification is something
            // you have to find; this is already in front of you, with one
            // control the size of a hand. It cannot appear over the lock screen
            // -- nothing can -- so on a locked phone you unlock and it is there.
            //
            // The FIRST thing, before even opening the window: turn the screen
            // on. An alarm that rings in the dark leaves the stop button three
            // steps away, which is exactly what was in the way.
            m_locker.despertarPantalla();

            // Launched into ITS OWN cgroup, and that is not a detail.
            //
            // startDetached() forks from this process, so the child stays in
            // this service's cgroup -- and this unit has MemoryMax. A whole
            // QtQuick application landed inside a limit sized for a daemon that
            // never draws, systemd's OOM killer answered by killing the WHOLE
            // unit, and the alarm went quiet two seconds after starting.
            // Measured, 2026-09-04: "The kernel OOM killer killed some
            // processes in this unit", 64M peak, one second of Blip.
            //
            // systemd-run --scope moves it out: same environment, own cgroup,
            // own accounting. If that is not available the plain fork is still
            // better than no screen at all -- the limit below now has room for
            // both, so the fallback degrades instead of killing the alarm.
            const bool lanzada = QProcess::startDetached(
                QStringLiteral("systemd-run"),
                {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--collect"),
                 QStringLiteral("--quiet"), QStringLiteral("--"), QStringLiteral("lost-phone"),
                 QStringLiteral("--alarma")});
            if (!lanzada
                && !QProcess::startDetached(QStringLiteral("lost-phone"),
                                            {QStringLiteral("--alarma")})) {
                qWarning() << "lost-phoned: could not open the alarm screen";
            }
        });

        // Pressing Parar on that notification is reachable only from inside the
        // session, which on a locked phone means the PIN was typed to get
        // there. Same rule as StopRing, one tap instead of four.
        connect(&m_locker, &Locker::stopRequested, this, [this] {
            qInfo() << "lost-phoned: stopped from the notification";
            m_ringer.stop();
        });

        // The relay channel. It only ever reports what happened; every decision
        // about whether to obey is taken here, against the same capability
        // matrix the SMS channel goes through.
        connect(&m_relay, &RelayClient::command, this, [this](const QString &verb) {
            runCommand(Channel::Relay, verb, QString());
        });

        // The home channel. Same treatment as the other two: it reports, it
        // never decides.
        connect(&m_lan, &LanServer::command, this, [this](const QString &verb) {
            runCommand(Channel::Lan, verb, QString());
        });
        m_lan.setStatusProvider([this] { return lanStatus(); });

        // The panel needs a FRESH position, not just to know the phone is alive.
        // The heartbeats already say the latter; this is the former.
        m_refresco.setSingleShot(true);
        connect(&m_refresco, &QTimer::timeout, this, [this] { refrescarPosicion(); });
        m_lan.setResultProvider([this] { return m_motivoUltimaOrden; });

        // Somebody else's ntfy. Same contract as the other three: it reports an
        // order, it never decides whether it may be carried out.
        connect(&m_ntfy, &NtfyClient::command, this, [this](const QString &verb) {
            runCommand(Channel::Ntfy, verb, QString());
        });
        connect(&m_relay, &RelayClient::paired, this,
                [this](const QString &deviceId, const QString &token, const QString &huella) {
                    LostPhoneConfig::savePairing(deviceId, token, huella);
                    reload();
                    qInfo() << "lost-phoned: paired with the relay";
                });
        connect(&m_relay, &RelayClient::pairingFailed, this, [this](const QString &why) {
            m_lastPairingError = why;
            qWarning() << "lost-phoned: pairing failed:" << why;
        });

        // Lost mode is only READ here. Acting on it -- locking the screen,
        // putting a notification up -- is session work and waits for start().
        m_lost = LostModeState::load();

        // LISTEN from the very first instant, even though we cannot ACT yet.
        //
        // Subscribing is a match rule on the bus: it introspects nothing and
        // cannot block, so it is not what was taking the startup down. And it is
        // needed here, not in start(), because an SMS that arrives during the
        // grace minute has to be READ at that moment: the messages app deletes it
        // from the modem within milliseconds.
        //
        // Measured 2026-09-06: an SMS that arrived at 17:56:43, inside the window
        // 17:56:05-17:57:06, was lost entirely. By the time the wait was over
        // there was nothing left in the modem to pick up.
        watchModem();

        // The session has to finish coming up before this daemon touches
        // anything. A minute, the same figure smart-unlock settled on after the
        // reboot loop, and for the same reason.
        m_start.setSingleShot(true);
        m_start.setInterval(kStartDelaySeconds * 1000);
        connect(&m_start, &QTimer::timeout, this, &LostPhoneDaemon::start);
        m_start.start();

        // The app and the Settings module ask the daemon what is going on, and
        // tell it to shut up, through here. Session bus: it is a user service
        // and only this user's session has any business talking to it.
        QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.lostphone"));
        // ExportScriptableSlots and not ExportAllSlots: onSmsAdded is a slot too,
        // and exporting it would let anything in the session hand the daemon an
        // arbitrary object path to go and read. Only the four marked Q_SCRIPTABLE
        // are the interface.
        QDBusConnection::sessionBus().registerObject(QStringLiteral("/LostPhone"), this,
                                                     QDBusConnection::ExportScriptableSlots);
    }

public Q_SLOTS:
    // --- the session-facing interface ---------------------------------------
    // Silencing is reachable only from inside the session, which on a locked
    // phone means the PIN was already typed to get here. That is what
    // "RequiresPin" means in practice: there is no button on the lock screen.
    // Reachable only from inside the session, which on a locked phone means the
    // PIN was already typed to get here. That is what "only with the PIN" means
    // in practice: there is no button on the lock screen itself.
    Q_SCRIPTABLE void StopRing() { m_ringer.stop(); }

    // The way out of lost mode from the phone in your hand. Same reasoning: if
    // you are running this, you unlocked the phone, so you are the owner.
    Q_SCRIPTABLE void LeaveLostMode() { leaveLostMode(); }

    Q_SCRIPTABLE bool LostModeActive() { return m_lost.active; }

    Q_SCRIPTABLE bool NtfyConnected() { return m_ntfy.connected(); }

    Q_SCRIPTABLE void TestRing(int seconds)
    {
        // Deliberately separate from the SMS path: this is the button in the
        // settings UI, so it does not consult the SMS capability and does not
        // count against the rate limit.
        m_ringer.start(qBound(1, seconds, 30));
    }

    Q_SCRIPTABLE bool Ringing() { return m_ringer.ringing(); }

    // Typed in the settings screen and handed straight to the relay. The daemon
    // does the pairing rather than the app because the daemon is the one that
    // owns the token afterwards -- two writers of the same secret is how you
    // end up paired twice and reachable never.
    Q_SCRIPTABLE void Pair(const QString &url, const QString &code, const QString &name)
    {
        m_lastPairingError.clear();
        m_relay.pair(url, code, name);
    }

    Q_SCRIPTABLE QString PairingError() { return m_lastPairingError; }

    Q_SCRIPTABLE bool RelayConnected() { return m_relay.connected(); }

    Q_SCRIPTABLE QString Status()
    {
        // The bools are turned into "yes"/"no" rather than passed to arg():
        // QString::arg(bool) picks an integral overload and prints 1/0, which in
        // a status line read at three in the morning is one more thing to decode.
        const auto si = [](bool value) {
            return value ? QStringLiteral("yes") : QStringLiteral("no");
        };
        // Split PER CHANNEL and not in a flat list. Before, "ring" and "locate"
        // sat loose in the middle and were the SMS ones, so the journal said
        // "locate=no" while the phone was being located over the home channel. It
        // was not lying, but it read as if it were -- and this line exists exactly
        // to diagnose at three in the morning, when nobody is going to go and read
        // the code.
        return QStringLiteral("active=%1 ringing=%2 lost=%3")
                   .arg(si(m_settings.enabled), si(m_ringer.ringing()), si(m_lost.active))
            + QStringLiteral(" | sms=%1 key=%2 ring=%3 locate=%4")
                  .arg(si(m_settings.smsEnabled), si(!m_settings.smsKey.isEmpty()),
                       si(m_settings.smsAllowRing), si(m_settings.smsAllowLocate))
            + QStringLiteral(" | home=%1 ring=%2 locate=%3")
                  .arg(si(m_lan.listening()), si(m_settings.lanAllowRing),
                       si(m_settings.lanAllowLocate))
            + QStringLiteral(" | relay=%1 paired=%2 connected=%3 locate=%4 poweroff=%5")
                  .arg(si(m_settings.relayEnabled), si(!m_settings.relayToken.isEmpty()),
                       si(m_relay.connected()), si(m_settings.relayAllowLocate),
                       si(m_settings.relayAllowPower))
            + QStringLiteral(" | ntfy=%1").arg(si(m_ntfy.connected()));
    }

private Q_SLOTS:
    // The session has had its minute. From here on the daemon behaves normally.
    void start()
    {
        m_started = true;
        qInfo() << "lost-phoned: starting up;" << Status();


        // The orders that arrived while it was starting up, now that it can.
        const QList<QPair<Channel, QString>> pendientes = m_pendientes;
        m_pendientes.clear();

        catchUpSms();
        m_ringer.configure(m_settings);
        m_relay.configure(m_settings);
        m_lan.configure(m_settings);
        m_ntfy.configure(m_settings);

        // A position right after starting, and then every so often. Otherwise
        // the panel of somebody who just rebooted the phone shows yesterday's map.
        //
        // AFTER configuring the relay, not before: the first version located at
        // the very top and sent the result to a client that did not yet have a
        // token or an address. It worked by a hair -- locating takes seconds and
        // configuring microseconds -- and it is exactly the kind of order that
        // breaks the day something runs a little faster.
        if (LostPhoneConfig::allows(m_settings, Channel::Relay, Capability::Locate)) {
            m_locateForRelay = true;
            m_locator.locate(m_settings, /*encender=*/false);
        }
        armarRefresco();

        // Re-asserting lost mode is the whole point of writing it down: the
        // first thing somebody who picks up a stranger's phone does is restart
        // it. But it locks the screen, so it belongs here and not in the
        // constructor.
        if (m_lost.active) {
            qInfo() << "lost-phoned: the phone is still in lost mode since"
                    << m_lost.since.toString(Qt::ISODate);
            m_locker.lock();
            m_locker.showMessage(m_lost.message);
        }

        for (const QPair<Channel, QString> &orden : pendientes) {
            qInfo().noquote() << "lost-phoned: running the order that arrived while starting up:"
                              << orden.second.section(QLatin1Char(' '), 0, 0);
            runCommand(orden.first, orden.second, m_pendienteSms.value(orden.second));
        }
        m_pendienteSms.clear();
    }

    // Orders that arrived while the daemon was keeping its hands off everything.
    //
    // The startup delay exists so this daemon cannot take the session down --
    // but a minute of deliberate deafness right after a reboot is exactly when
    // somebody is likely to be sending "sonar", because the phone rebooting is
    // often WHY they are looking for it. Losing that order silently would be
    // the worst possible moment to be tidy about it.
    //
    // Only messages younger than five minutes. Acting on an older one would be
    // the same mistake as ntfy's since=all: a phone that starts screaming over
    // something sent last night.
    void catchUpSms()
    {
        const QString modem = modemPath();
        if (modem.isEmpty()) {
            return;
        }
        const QVariantMap properties = readProperties(modem, kMessagingIface);
        const QDBusArgument list = properties.value(QStringLiteral("Messages")).value<QDBusArgument>();
        QList<QDBusObjectPath> mensajes;
        list >> mensajes;

        for (const QDBusObjectPath &path : mensajes) {
            const QVariantMap sms = readProperties(path.path(), kSmsIface);
            // MMSmsState: 3 is "received". Anything else is one we sent or one
            // still arriving.
            if (sms.value(QStringLiteral("State")).toInt() != 3) {
                continue;
            }
            const QDateTime cuando =
                QDateTime::fromString(sms.value(QStringLiteral("Timestamp")).toString(),
                                      Qt::ISODate);
            if (cuando.isValid() && cuando.secsTo(QDateTime::currentDateTime()) > 5 * 60) {
                continue;
            }
            qInfo() << "lost-phoned: picking up an SMS that arrived while starting up";
            handleSms(path.path(), 0);
        }
    }

    void onSmsAdded(const QDBusObjectPath &path, bool received)
    {
        // Logged BEFORE anything else, and on purpose. On 2026-09-04 a real SMS
        // arrived, the messages app stored it, and this daemon did nothing --
        // and there was no way to tell whether the signal had never arrived or
        // whether it had arrived and the message was already gone. One line in
        // the journal answers that question for good.
        qInfo() << "lost-phoned: ModemManager announces an SMS" << path.path()
                << "received=" << received;
        if (!received) {
            return;  // one we sent ourselves
        }

        // Read NOW, not after a delay. This phone has two other daemons on the
        // same signal -- spacebar-daemon and kde-telephony-daemon -- and they
        // store the message and then DELETE it from the modem. Waiting half a
        // second was handing them the race: by the time we looked, the object
        // was gone and Text came back empty, which this code then read as "not
        // a command". Measured, not guessed: the message was in spacebar's
        // database and nowhere else.
        //
        // If the body is not there yet, retry quickly a few times rather than
        // waiting once and giving up.
        handleSms(path.path(), 0);
    }

    // The SMS that answers "estado": a single line with what helps find it, and
    // no filler. It fits in one SMS and reads at a glance.
    QString respuestaEstado(const Fix &fix) const
    {
        QStringList partes;
        if (m_estadoConPosicion && fix.hasCoordinates) {
            partes << QStringLiteral("%1,%2 (+-%3m)")
                          .arg(fix.latitude, 0, 'f', 5)
                          .arg(fix.longitude, 0, 'f', 5)
                          .arg(fix.accuracyMeters);
            partes << QStringLiteral("osm.org/?mlat=%1&mlon=%2")
                          .arg(fix.latitude, 0, 'f', 5)
                          .arg(fix.longitude, 0, 'f', 5);
            if (fix.source == QLatin1String("last-known")) {
                partes << QStringLiteral("LAST from ")
                        + fix.when.toString(QStringLiteral("HH:mm"));
            }
        } else if (m_estadoConPosicion) {
            partes << QStringLiteral("no GPS");
        }

        // The network and the cell are what place the phone when the GPS does not
        // lock, and they say something coordinates do not: the NAME of the Wi-Fi
        // recognises a place instantly -- "HomeWiFi" is your home.
        if (!fix.wifi.isEmpty()) {
            partes << QStringLiteral("wifi ") + fix.wifi;
        }
        if (!fix.cell.isEmpty()) {
            partes << QStringLiteral("cell ") + fix.cell;
        }
        if (fix.batteryPercent >= 0) {
            partes << QStringLiteral("bat %1%%2")
                          .arg(fix.batteryPercent)
                          .arg(cargando() ? QStringLiteral(" charging") : QString());
        }
        if (m_ringer.ringing()) {
            partes << QStringLiteral("RINGING");
        }
        if (m_lost.active) {
            partes << QStringLiteral("LOST");
        }
        partes << fix.when.toString(QStringLiteral("HH:mm"));
        return partes.join(QStringLiteral(", "));
    }

    // Plugged in or not. It helps place it more than it seems: a charging phone
    // is somewhere that somebody plugged it in.
    bool cargando() const
    {
        const QDir dir(QStringLiteral("/sys/class/power_supply"));
        const QStringList entradas = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entrada : entradas) {
            QFile f(dir.filePath(entrada) + QStringLiteral("/status"));
            if (f.open(QIODevice::ReadOnly)) {
                if (QString::fromUtf8(f.readAll()).trimmed() == QLatin1String("Charging")) {
                    return true;
                }
            }
        }
        return false;
    }

    // How often to refresh, in milliseconds. Zero = never.
    int intervaloRefresco() const
    {
        const int minutos = m_lost.active ? m_settings.relayReportLostMinutes
                                          : m_settings.relayReportMinutes;
        return minutos > 0 ? minutos * 60 * 1000 : 0;
    }

    void armarRefresco()
    {
        const int ms = intervaloRefresco();
        if (ms <= 0) {
            m_refresco.stop();
            return;
        }
        m_refresco.start(ms);
    }

    // A fix on its own, so the panel's map does not go stale.
    //
    // It does NOT turn the radios on the way a real search does: this happens
    // every half hour and for ever, and leaving the GPS on in perpetuity for a
    // background refresh would trade the phone's battery for a convenience. When
    // somebody is really looking for it, that order does turn everything on.
    void refrescarPosicion()
    {
        if (!m_started || m_locator.busy()) {
            armarRefresco();
            return;
        }
        if (!LostPhoneConfig::allows(m_settings, Channel::Relay, Capability::Locate)) {
            armarRefresco();
            return;
        }
        m_locateForRelay = true;
        m_locator.locate(m_settings, /*encender=*/false);
        armarRefresco();
    }

    void onLocated(const Fix &fix)
    {
        if (!m_estadoPara.isEmpty()) {
            const QString texto = respuestaEstado(fix);
            qInfo().noquote() << "lost-phoned: answering the status:" << texto;
            sendSms(m_estadoPara, texto);
            m_estadoPara.clear();
        }

        qInfo().noquote() << "lost-phoned: position:" << fix.toSms();
        if (!m_replyTo.isEmpty() && m_settings.smsReply) {
            sendSms(m_replyTo, QStringLiteral("lost-phone: ") + fix.toSms());
        }
        m_replyTo.clear();

        // The relay gets the full fix, not the 160-character version: it has
        // room for the accuracy, the cell and the Wi-Fi, and the panel draws a
        // map out of them.
        if (m_locateForRelay) {
            m_locateForRelay = false;
            m_relay.report(fix);
        }

        // ntfy gets the short line, the same one the SMS carries: it lands as a
        // push notification on whatever device you are holding, and a
        // notification you have to unfold to read is a notification you read
        // too late.
        if (m_locateForNtfy) {
            m_locateForNtfy = false;
            m_ntfy.report(fix);
        }
    }

private:
    // What the config file says right now, as bytes. Cheap: this file is a
    // few hundred bytes and the alternative -- a timestamp -- lies when two
    // writes land in the same second, which is exactly what a settings screen
    // does when you drag a slider.
    QByteArray configFingerprint() const
    {
        QFile f(LostPhoneConfig::filePath());
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return f.readAll();
    }

    // Idempotent: adds whatever is not being watched yet. The file only exists
    // once the user has configured something, so this gets called again every
    // time anything changes in the directory until it does.
    void watchConfig()
    {
        const QString file = LostPhoneConfig::filePath();
        const QString dir = QFileInfo(file).absolutePath();
        if (!m_watcher.directories().contains(dir)) {
            m_watcher.addPath(dir);
        }
        if (QFile::exists(file) && !m_watcher.files().contains(file)) {
            m_watcher.addPath(file);
        }
    }

    void reload()
    {
        // The DIRECTORY watch fires on anything the session writes into
        // ~/.config, which is a great deal and almost none of it ours. Watching
        // the directory is still necessary -- it is what catches the config
        // file being created for the first time -- but reacting to every
        // neighbour is not. Nine identical lines in one second in the journal
        // is what that looked like.
        //
        // So: read, and only carry on if something actually changed. The
        // comparison is on the file's own contents, because that is the thing
        // whose change matters.
        const QByteArray fingerprint = configFingerprint();
        if (fingerprint == m_fingerprint) {
            return;
        }
        m_fingerprint = fingerprint;

        m_settings = LostPhoneConfig::load();

        // The file watcher can fire during the startup delay -- the settings UI
        // may well be open while the session is still coming up. If it did any
        // of this then, the delay would buy nothing.
        if (!m_started) {
            return;
        }
        m_ringer.configure(m_settings);
        m_relay.configure(m_settings);
        m_lan.configure(m_settings);
        m_ntfy.configure(m_settings);
        qInfo().noquote() << "lost-phoned:" << Status();
    }

    void watchModem()
    {
        // Subscribe by interface rather than by object path: the modem's path
        // changes every time ModemManager restarts or the modem re-enumerates,
        // and a daemon that latched onto one path would go quietly deaf.
        const bool ok = QDBusConnection::systemBus().connect(
            kMmService, QString(), kMessagingIface, QStringLiteral("Added"), this,
            SLOT(onSmsAdded(QDBusObjectPath, bool)));
        if (!ok) {
            qWarning() << "lost-phoned: could not subscribe to ModemManager's SMS";
        }
    }

    QString modemPath()
    {
        const QDBusMessage reply =
            Bus::call(QDBusConnection::systemBus(), kMmService, kMmPath,
                      QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                      QStringLiteral("GetManagedObjects"), {}, 3000);
        if (!Bus::ok(reply) || reply.arguments().isEmpty()) {
            return {};
        }
        const QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
        QString found;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            QDBusObjectPath path;
            arg >> path;
            arg.endMapEntry();
            if (path.path().contains(QStringLiteral("/Modem/"))) {
                found = path.path();
            }
        }
        arg.endMap();
        return found;
    }

    // `attempt` counts the quick retries for a message whose body has not been
    // read off the modem yet.
    void handleSms(const QString &smsPath, int attempt)
    {
        if (!m_settings.smsEnabled || m_settings.smsKey.isEmpty()) {
            return;
        }
        const QVariantMap properties = readProperties(smsPath, kSmsIface);
        const QString text = properties.value(QStringLiteral("Text")).toString();
        const QString from = properties.value(QStringLiteral("Number")).toString();
        if (text.isEmpty()) {
            if (attempt < 5) {
                QTimer::singleShot(200, this,
                                   [this, smsPath, attempt] { handleSms(smsPath, attempt + 1); });
            } else {
                qInfo() << "lost-phoned: the SMS" << smsPath
                        << "vanished before it could be read (another daemon deleted it first)";
            }
            return;
        }

        // "<key> <verb>". The key first so an ordinary message can never be a
        // command by accident, and compared whole rather than searched for, so
        // a message that merely CONTAINS the key does nothing.
        // Compared case-insensitively: the key is stored lower-case, but the
        // stranger's phone whose keyboard capitalises the first letter of every
        // message would otherwise send a command that silently does nothing.
        const QStringList words = text.simplified().split(QLatin1Char(' '));
        if (words.isEmpty() || words.first().compare(m_settings.smsKey, Qt::CaseInsensitive) != 0) {
            return;
        }
        const QString verb = words.size() > 1 ? words.at(1).toLower() : QString();

        qInfo().noquote() << "lost-phoned: order by SMS:" << verb << "from" << from;

        if (!rateLimitOk()) {
            qInfo() << "lost-phoned: dropped by the one-order-per-minute limit";
            return;
        }

        // Deleting comes before acting, and on purpose: locating takes up to a
        // minute and a half, and a command left sitting in the inbox for that
        // long is a command somebody can read over your shoulder.
        if (m_settings.smsDeleteAfter) {
            deleteSms(smsPath);
        }

        runCommand(Channel::Sms, verb, from);
    }

    // The one place an order is turned into an action, whichever door it came
    // in through. Every channel lands here, so a channel added later cannot
    // accidentally bring its own permissions with it.
    void runCommand(Channel channel, const QString &rawVerb, const QString &smsReplyTo)
    {
        // By default, it was NOT done. Every early return leaves its reason here
        // and only the end of the function clears it. That way the home channel
        // can answer what actually happened instead of a routine "ok".
        m_motivoUltimaOrden = QStringLiteral("I do not understand it");

        // Still starting up: note it down and do it when done. Losing the order
        // would be worse, because the minute after a reboot is exactly when
        // somebody is looking for the phone -- the phone having rebooted is
        // often WHY they are looking for it.
        if (!m_started) {
            qInfo().noquote() << "lost-phoned: order received while starting up, saving it:"
                              << rawVerb.section(QLatin1Char(' '), 0, 0);
            m_pendientes.append({channel, rawVerb});
            if (!smsReplyTo.isEmpty()) {
                m_pendienteSms.insert(rawVerb, smsReplyTo);
            }
            m_motivoUltimaOrden = QStringLiteral("still starting up; it is noted down");
            return;
        }

        // "bloquear llama al 600 123 456": the verb is the first word and the
        // rest is its argument, which for locking is the message a stranger
        // reads on the lock screen.
        const QString trimmed = rawVerb.trimmed();
        const QString verb = trimmed.section(QLatin1Char(' '), 0, 0).toLower();
        const QString argument = trimmed.section(QLatin1Char(' '), 1).trimmed();

        if (verb == QLatin1String("sonar") || verb == QLatin1String("ring")) {
            if (!LostPhoneConfig::allows(m_settings, channel, Capability::Ring)) {
                m_motivoUltimaOrden = QStringLiteral("this channel cannot make it ring");
                return;
            }
            // NO reply, and on purpose. An order must not cost anyone money:
            // whoever sends "sonar" is hearing the result, and confirming it by
            // SMS is charging them to be told what they already know. Only
            // "estado" gets an answer, which is the only thing that is ASKED
            // rather than sent. If something fails, it goes in the journal.
            if (!m_ringer.start(m_settings.ringSeconds)) {
                qWarning() << "lost-phoned: could not make the speaker ring";
            }
            // And after ringing, let itself be found. AFTER and not before:
            // whoever sends "sonar" wants noise NOW, and turning Wi-Fi on can
            // take several seconds. The order matters more than it seems.
            QTimer::singleShot(500, this, [this] { m_locator.encenderLoNecesario(); });
        } else if (verb == QLatin1String("parar") || verb == QLatin1String("stop")) {
            // Silencing is deliberately NOT available over SMS. Whoever is
            // holding the phone can read the key off the screen of the message
            // that just arrived, and being able to shut the alarm up is exactly
            // what they would use it for. It stops from the panel, which is
            // behind a password, or from the phone itself, which is behind the
            // PIN. The cost is real and was accepted: a phone ringing in a
            // cinema needs the panel or the PIN, not just any handset.
            if (channel == Channel::Sms) {
                m_motivoUltimaOrden = QStringLiteral("stopping is not possible over SMS, on purpose");
                return;
            }
            if (!LostPhoneConfig::allows(m_settings, channel, Capability::Ring)) {
                m_motivoUltimaOrden = QStringLiteral("this channel cannot stop it");
                return;
            }
            m_ringer.stop();  // no reply: it costs money and adds nothing
        } else if (verb == QLatin1String("donde") || verb == QLatin1String("where")
                   || verb == QLatin1String("locate")) {
            if (!LostPhoneConfig::allows(m_settings, channel, Capability::Locate)) {
                m_motivoUltimaOrden = QStringLiteral("this channel cannot locate it");
                return;
            }
            // Who asked decides where the answer goes. One flag per channel and
            // not a single "who asked" field, because two channels can ask at
            // once -- and an answer that went to the wrong one would be a
            // position handed to somebody who never asked for it.
            switch (channel) {
            case Channel::Sms:
                m_replyTo = smsReplyTo;
                break;
            case Channel::Ntfy:
                m_locateForNtfy = true;
                break;
            case Channel::Relay:
            case Channel::Lan:
                m_locateForRelay = true;
                break;
            }
            m_locator.locate(m_settings);
        } else if (verb == QLatin1String("bloquear") || verb == QLatin1String("lock")) {
            if (!LostPhoneConfig::allows(m_settings, channel, Capability::Lock)) {
                m_motivoUltimaOrden = QStringLiteral("this channel cannot lock it");
                return;
            }
            enterLostMode(argument);  // no reply
            // A phone that has just been given up for lost has to be locatable.
            QTimer::singleShot(500, this, [this] { m_locator.encenderLoNecesario(); });
        } else if (verb == QLatin1String("desbloquear") || verb == QLatin1String("unlock")) {
            // Leaving lost mode is NOT the same permission as entering it.
            // Anybody who can lock the phone must not be able to unlock it: that
            // is the difference between a safety net and a toy. Only the panel,
            // which is behind a password, and the phone itself, which is behind
            // the PIN.
            if (channel != Channel::Relay
                || !LostPhoneConfig::allows(m_settings, channel, Capability::Lock)) {
                m_motivoUltimaOrden = QStringLiteral("unlock only from the panel or with the PIN");
                return;
            }
            leaveLostMode();  // no reply
        } else if (verb == QLatin1String("apagar") || verb == QLatin1String("poweroff")) {
            // ONLY from the panel, the same as unlock, and with its own switch
            // that ships off on top of that. Two bolts for an order that has no
            // way back: a powered-off phone does not ring, does not say where it
            // is and obeys no one -- not you either.
            if (channel != Channel::Relay
                || !LostPhoneConfig::allows(m_settings, channel, Capability::Power)) {
                m_motivoUltimaOrden = QStringLiteral("power-off only from the panel, and it must be enabled");
                return;
            }
            // Written down BEFORE powering off. Otherwise tomorrow the phone
            // shows up powered off and there is no way to tell whether it was
            // this, the battery or a hang -- which is exactly the question one
            // asks.
            qWarning() << "lost-phoned: POWERING OFF the phone by order of the panel";
            m_locker.showMessage(
                QStringLiteral("Powering off the phone by order of your panel."));
            Proceso::salida(QStringLiteral("systemctl"), {QStringLiteral("poweroff")}, 10000);
        } else if (verb == QLatin1String("estado") || verb == QLatin1String("status")) {
            // "estado" is THE question, and the only one it answers. That is why
            // it brings everything at once -- position, battery, network and what
            // state it is in -- instead of forcing three SMS to learn three
            // things.
            //
            // It carries coordinates ONLY if that channel is allowed to locate.
            // Otherwise, answering the position here would be a back door to the
            // "may say where it is" switch.
            m_estadoPara = smsReplyTo;
            m_estadoConPosicion =
                LostPhoneConfig::allows(m_settings, channel, Capability::Locate);
            m_locator.locate(m_settings);
        } else {
            // Without this branch, a mistyped order was swallowed silently and
            // the home channel kept answering {"ok":true}. It cost an afternoon to
            // understand why "nothing happened": the body went as JSON and the
            // endpoint expects the verb in plain text.
            qInfo().noquote() << "lost-phoned: order I do not understand:" << verb;
            return;
        }
        m_motivoUltimaOrden.clear();
    }

    // What the laptop's command reads. Deliberately small: what it is, whether
    // it is ringing, whether it is lost, how much battery is left, and the last
    // position if the owner allowed locating. No BSSIDs, no cell ids -- this
    // answer crosses a network that has guests on it.
    QByteArray lanStatus()
    {
        const LastKnown &l = m_settings.lastKnown;
        QString posicion = QStringLiteral("null");
        if (m_settings.lanAllowLocate && l.valid) {
            posicion = QStringLiteral(
                           "{\"latitud\":%1,\"longitud\":%2,\"precision_metros\":%3,"
                           "\"origen\":\"%4\",\"cuando\":\"%5\"}")
                           .arg(l.latitude, 0, 'f', 5)
                           .arg(l.longitude, 0, 'f', 5)
                           .arg(l.accuracyMeters)
                           .arg(l.source, l.when.toString(Qt::ISODate));
        }
        return QStringLiteral("{\"sonando\":%1,\"perdido\":%2,\"ultima\":%3}")
            .arg(m_ringer.ringing() ? QStringLiteral("true") : QStringLiteral("false"),
                 m_lost.active ? QStringLiteral("true") : QStringLiteral("false"), posicion)
            .toUtf8();
    }

    void enterLostMode(const QString &message)
    {
        m_lost.active = true;
        m_lost.since = QDateTime::currentDateTime();
        armarRefresco();  // in lost mode it refreshes much more often
        if (!message.isEmpty()) {
            m_lost.message = message;
        }
        // Written BEFORE locking. If the phone died between the two, coming
        // back locked is the answer you wanted; coming back unlocked because
        // the file had not been written yet is not.
        LostModeState::save(m_lost);
        m_locker.lock();
        m_locker.showMessage(m_lost.message);
        qInfo() << "lost-phoned: lost mode ACTIVATED";
    }

    void leaveLostMode()
    {
        m_lost.active = false;
        armarRefresco();  // back to the calm rhythm
        LostModeState::save(m_lost);
        m_locker.clearMessage();
        qInfo() << "lost-phoned: lost mode deactivated";
    }

    bool rateLimitOk()
    {
        const QDateTime now = QDateTime::currentDateTime();
        if (m_lastCommand.isValid() && m_lastCommand.secsTo(now) < kMinSecondsBetweenCommands) {
            return false;
        }
        m_lastCommand = now;
        return true;
    }

    void reply(const QString &to, const QString &body)
    {
        if (m_settings.smsReply && !to.isEmpty()) {
            sendSms(to, body);
        }
    }

    void deleteSms(const QString &smsPath)
    {
        const QString modem = modemPath();
        if (modem.isEmpty()) {
            return;
        }
        Bus::call(QDBusConnection::systemBus(), kMmService, modem, kMessagingIface,
                  QStringLiteral("Delete"), {QVariant::fromValue(QDBusObjectPath(smsPath))}, 3000);
    }

    void sendSms(const QString &number, const QString &body)
    {
        const QString modem = modemPath();
        if (modem.isEmpty()) {
            qWarning() << "lost-phoned: cannot find the modem to reply";
            return;
        }
        QVariantMap properties;
        properties.insert(QStringLiteral("number"), number);
        properties.insert(QStringLiteral("text"), body.left(300));

        const QDBusMessage created =
            Bus::call(QDBusConnection::systemBus(), kMmService, modem, kMessagingIface,
                      QStringLiteral("Create"), {properties}, 5000);
        if (!Bus::ok(created) || created.arguments().isEmpty()) {
            qWarning() << "lost-phoned: could not create the reply SMS:"
                       << created.errorMessage();
            return;
        }
        const QString smsPath =
            created.arguments().constFirst().value<QDBusObjectPath>().path();
        // Sending can genuinely take a while on a weak signal; ten seconds is
        // the one place in this daemon where waiting is the right thing.
        Bus::call(QDBusConnection::systemBus(), kMmService, smsPath, kSmsIface,
                  QStringLiteral("Send"), {}, 10000);
    }

    Settings m_settings;
    Ringer m_ringer;
    Locator m_locator;
    RelayClient m_relay;
    LanServer m_lan;
    NtfyClient m_ntfy;
    Locker m_locker;
    LostMode m_lost;
    QFileSystemWatcher m_watcher;
    QDateTime m_lastCommand;
    QString m_replyTo;

    // Who to answer the "estado" to, and whether that channel may give coordinates.
    QString m_estadoPara;
    // Empty = the last order ran. Otherwise, why it did not.
    QString m_motivoUltimaOrden;
    bool m_estadoConPosicion = false;
    // Who asked for the position that is being worked out right now. A fix
    // takes up to a minute and a half, so the answer has to remember where to
    // go: back down the SMS it came from, up to the relay, or both.
    bool m_locateForRelay = false;
    bool m_locateForNtfy = false;
    bool m_started = false;   // false until the startup delay has passed
    QByteArray m_fingerprint;

    // Orders that arrived during the grace minute, with who to answer them to.
    QList<QPair<Channel, QString>> m_pendientes;
    QHash<QString, QString> m_pendienteSms;
    QTimer m_start;
    QTimer m_refresco;
    QString m_lastPairingError;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("lost-phoned"));

    // The unit's ExecStopPost. On ANY stop -- clean, crashed or killed -- put
    // the volume back where the user had it. The alarm raises it to maximum on
    // purpose and a phone left there for ever would break the one-writer rule
    // the `audio` setting depends on.
    if (argc > 1 && QLatin1String(argv[1]) == QLatin1String("--restaurar")) {
        Ringer::restoreSavedVolume();
        return 0;
    }

    LostPhoneDaemon daemon;
    return app.exec();
}

#include "main.moc"
