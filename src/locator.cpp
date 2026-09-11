// SPDX-License-Identifier: GPL-2.0-or-later

#include "locator.h"

#include "process.h"

#include "nmea.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>

namespace
{
constexpr int kPollSeconds = 5;

// How long we keep looking AFTER the first fix.
//
// A GPS that has just come on cold does not give its best position first time:
// the first fix is usually the worst -- it can be hundreds of metres off -- and
// it converges over the following seconds as more satellites come in. Rushing
// off with it is what sends somebody looking on the next street over.
//
// So as soon as there is a position we keep polling a while longer and keep the
// LAST one, not the first. Without HDOP in the NMEA sentence there is no way to
// measure the quality, and "the most recent" is the only honest improvement that
// can be made blind.
constexpr int kRefineSeconds = 20;


// mmcli prints a table, so every value is "  KEY | name: value". One helper
// rather than a regular expression per field, because the padding varies.
QString field(const QString &text, const QString &name)
{
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const qsizetype bar = line.lastIndexOf(QLatin1Char('|'));
        if (bar < 0) {
            continue;
        }
        const QString rest = line.mid(bar + 1);
        const qsizetype colon = rest.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            continue;
        }
        if (rest.left(colon).trimmed() == name) {
            return rest.mid(colon + 1).trimmed();
        }
    }
    return {};
}

}



QString Fix::toSms() const
{
    QStringList parts;
    if (hasCoordinates) {
        parts << QStringLiteral("%1,%2 (+-%3m)")
                     .arg(latitude, 0, 'f', 5)
                     .arg(longitude, 0, 'f', 5)
                     .arg(accuracyMeters);
        parts << QStringLiteral("osm.org/?mlat=%1&mlon=%2")
                     .arg(latitude, 0, 'f', 5)
                     .arg(longitude, 0, 'f', 5);
    } else {
        parts << QStringLiteral("no GPS");
    }
    if (!cell.isEmpty()) {
        parts << QStringLiteral("cell ") + cell;
    }
    if (!wifi.isEmpty()) {
        parts << QStringLiteral("wifi ") + wifi;
    }
    if (batteryPercent >= 0) {
        parts << QStringLiteral("bat %1%").arg(batteryPercent);
    }
    if (source == QLatin1String("last-known")) {
        parts << QStringLiteral("LAST KNOWN ") + when.toString(QStringLiteral("dd/MM HH:mm"));
    } else {
        parts << when.toString(QStringLiteral("HH:mm"));
    }
    return parts.join(QStringLiteral(", "));
}

Locator::Locator(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(kPollSeconds * 1000);
    connect(&m_poll, &QTimer::timeout, this, &Locator::pollGnss);
}

void Locator::readCell(Fix &fix)
{
    const QString out = Proc::output(QStringLiteral("mmcli"),
                            {QStringLiteral("-m"), QStringLiteral("any"),
                             QStringLiteral("--location-get")});
    if (out.isEmpty()) {
        return;
    }
    const QString mcc = field(out, QStringLiteral("operator mcc"));
    const QString mnc = field(out, QStringLiteral("operator mnc"));
    const QString lac = field(out, QStringLiteral("location area code"));
    const QString cid = field(out, QStringLiteral("cell id"));
    if (!mcc.isEmpty() && !cid.isEmpty()) {
        fix.cell = QStringLiteral("%1-%2/%3/%4").arg(mcc, mnc, lac, cid);
    }
}

void Locator::readWifi(Fix &fix)
{
    // -t gives colon-separated fields, and nmcli escapes the colons inside a
    // BSSID as "\:" -- so splitting naively turns one AP into six fields. The
    // placeholder swap is uglier than a regular expression and a great deal
    // more predictable.
    const QString out = Proc::output(QStringLiteral("nmcli"),
                            {QStringLiteral("-t"), QStringLiteral("-f"),
                             QStringLiteral("BSSID,SIGNAL,SSID"), QStringLiteral("dev"),
                             QStringLiteral("wifi"), QStringLiteral("list")});
    int best = -1;
    for (QString line : out.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        line.replace(QStringLiteral("\\:"), QStringLiteral("\x01"));
        const QStringList f = line.split(QLatin1Char(':'));
        if (f.size() < 2) {
            continue;
        }
        const QString bssid = QString(f.at(0)).replace(QLatin1Char('\x01'), QLatin1Char(':'));
        const int signal = f.at(1).toInt();
        const QString ssid = f.size() > 2 ? f.at(2) : QString();
        fix.bssids << bssid;
        if (signal > best) {
            best = signal;
            fix.wifi = ssid.isEmpty() ? bssid : QStringLiteral("%1 %2%").arg(ssid).arg(signal);
        }
    }
}

void Locator::readBattery(Fix &fix)
{
    // Straight from sysfs and not from UPower: it is one file read, it works
    // with no session at all, and a phone that is about to die is exactly when
    // this has to keep working.
    const QDir dir(QStringLiteral("/sys/class/power_supply"));
    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        QFile f(dir.filePath(entry) + QStringLiteral("/capacity"));
        QFile type(dir.filePath(entry) + QStringLiteral("/type"));
        if (!type.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (QString::fromUtf8(type.readAll()).trimmed() != QLatin1String("Battery")) {
            continue;
        }
        if (f.open(QIODevice::ReadOnly)) {
            bool ok = false;
            const int pct = QString::fromUtf8(f.readAll()).trimmed().toInt(&ok);
            if (ok) {
                fix.batteryPercent = pct;
                return;
            }
        }
    }
}

bool Locator::readGnss(Fix &fix)
{
    const QString out = Proc::output(QStringLiteral("mmcli"),
                            {QStringLiteral("-m"), QStringLiteral("any"),
                             QStringLiteral("--location-get")});
    if (out.isEmpty()) {
        return false;
    }

    // The easy path: with gps-raw enabled ModemManager reports the fix itself
    // once it has one, and then there is nothing to parse.
    const QString lat = field(out, QStringLiteral("latitude"));
    const QString lon = field(out, QStringLiteral("longitude"));
    if (!lat.isEmpty() && !lon.isEmpty()) {
        bool ok1 = false;
        bool ok2 = false;
        const double la = lat.toDouble(&ok1);
        const double lo = lon.toDouble(&ok2);
        if (ok1 && ok2) {
            fix.hasCoordinates = true;
            fix.latitude = la;
            fix.longitude = lo;
            fix.accuracyMeters = 20;
            fix.source = QStringLiteral("gnss");
            return true;
        }
    }

    // The fallback: the raw NMEA, which this device always reports even when
    // there is no fix. Only GGA sentences with a non-zero quality count.
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        const QString s = line.section(QLatin1Char('|'), -1).trimmed();
        if (!s.startsWith(QStringLiteral("$GPGGA")) && !s.startsWith(QStringLiteral("$GNGGA"))) {
            continue;
        }
        double la = 0.0;
        double lo = 0.0;
        if (Nmea::parseGga(s, la, lo)) {
            fix.hasCoordinates = true;
            fix.latitude = la;
            fix.longitude = lo;
            fix.accuracyMeters = 20;
            fix.source = QStringLiteral("gnss");
            return true;
        }
    }
    return false;
}

// Turns on whatever is needed to be able to locate, and does NOT turn it back
// off.
//
// It is a conscious decision: if this phone is being asked where it is, then it
// is lost. In that situation the battery matters less than being findable, and a
// GPS that turns itself off between attempts is a GPS that never gets a fix -- it
// takes a good half minute to get one the first time.
//
// Turned on:
//   - the Wi-Fi radio, because without it there is no BSSID to see and indoors it
//     is the only thing that places you;
//   - the modem's location (raw GNSS, NMEA and cell), which is where both the GPS
//     and the tower come from;
//   - and the radios in general, in case the phone was in airplane mode.
//
// All in the spirit of `|| true`: if something cannot be turned on, carry on with
// what there is. Half a position is better than none.
void Locator::turnOnWhatIsNeeded()
{
    if (Proc::output(QStringLiteral("nmcli"),
                        {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("WIFI"),
                         QStringLiteral("radio")})
            .contains(QLatin1String("disabled"))) {
        qInfo() << "lost-phoned: turning Wi-Fi on to be able to place myself";
        Proc::output(QStringLiteral("nmcli"),
                        {QStringLiteral("radio"), QStringLiteral("wifi"), QStringLiteral("on")},
                        8000);
        m_encendiWifi = true;
    }
    if (Proc::output(QStringLiteral("nmcli"),
                        {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("WWAN"),
                         QStringLiteral("radio")})
            .contains(QLatin1String("disabled"))) {
        qInfo() << "lost-phoned: turning the modem radio on";
        Proc::output(QStringLiteral("nmcli"),
                        {QStringLiteral("radio"), QStringLiteral("wwan"), QStringLiteral("on")},
                        8000);
        m_encendiWwan = true;
    }

    // The modem's location. All three are requested at once because mmcli treats
    // them as a set: requesting only one TURNS OFF the others.
    const QString state = Proc::output(
        QStringLiteral("mmcli"),
        {QStringLiteral("-m"), QStringLiteral("any"), QStringLiteral("--location-status")});
    //
    // And we look ONLY at the "enabled:" line, not the whole output.
    // `--location-status` prints two lines and the first is "capabilities:", which
    // lists what the modem CAN do -- gps-raw always appears there, on or off.
    // Searching the full text gave "already set" no matter what, and this branch
    // never ran. Measured on the surya 2026-09-06: with the GPS turned off by
    // hand, the alarm turned Wi-Fi on and left the GPS off.
    QString enabled;
    const QStringList lines = state.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int i = line.indexOf(QLatin1String("enabled:"));
        if (i >= 0) {
            enabled = line.mid(i + 8);
            break;
        }
    }
    if (!enabled.contains(QLatin1String("gps-raw"))
        || !enabled.contains(QLatin1String("gps-nmea"))
        || !enabled.contains(QLatin1String("3gpp-lac-ci"))) {
        qInfo().noquote() << "lost-phoned: turning the modem location on; now:"
                          << enabled.trimmed();
        Proc::output(QStringLiteral("mmcli"),
                        {QStringLiteral("-m"), QStringLiteral("any"),
                         QStringLiteral("--location-enable-gps-raw"),
                         QStringLiteral("--location-enable-gps-nmea"),
                         QStringLiteral("--location-enable-3gpp")},
                        10000);
        m_encendiGps = true;
    }
}

void Locator::giveBackRadios()
{
    if (!m_encendiWifi && !m_encendiWwan && !m_encendiGps) {
        return;  // we turned nothing on: there is nothing to undo
    }
    qInfo() << "lost-phoned: putting the radios back as they were";

    // The GPS first: turning it off cuts no connection, so if something fails
    // afterwards at least this is already done.
    if (m_encendiGps) {
        Proc::output(QStringLiteral("mmcli"),
                        {QStringLiteral("-m"), QStringLiteral("any"),
                         QStringLiteral("--location-disable-gps-raw"),
                         QStringLiteral("--location-disable-gps-nmea")},
                        10000);
        m_encendiGps = false;
    }
    if (m_encendiWwan) {
        Proc::output(QStringLiteral("nmcli"),
                        {QStringLiteral("radio"), QStringLiteral("wwan"), QStringLiteral("off")},
                        8000);
        m_encendiWwan = false;
    }
    // Wi-Fi last: it is where the stop order itself may be arriving, and cutting
    // it earlier would leave everything else half done.
    if (m_encendiWifi) {
        Proc::output(QStringLiteral("nmcli"),
                        {QStringLiteral("radio"), QStringLiteral("wifi"), QStringLiteral("off")},
                        8000);
        m_encendiWifi = false;
    }
}

void Locator::locate(const Settings &settings, bool turnOn)
{
    if (m_busy) {
        // Normally a second attempt rides on top of the first, which is the
        // sensible thing: two requests at once are not worth two fixes.
        //
        // Except in one case, and it took a while to see: the background refresh
        // does NOT turn the radios on. If while one is running you press "where is
        // it", your search would ride on top of it and answer with no Wi-Fi and no
        // GPS, without telling you it settled for less. Once again the button that
        // seems to do nothing.
        //
        // So a real search drops the refresh and starts from scratch.
        if (!turnOn || m_encendio) {
            return;
        }
        qInfo() << "lost-phoned: a real search arrives; restarting the background refresh";
        m_poll.stop();
    }
    m_busy = true;
    m_encendio = turnOn;
    m_settings = settings;

    // First of all: turn on whatever is needed. Without this, a phone with Wi-Fi
    // off answers "I do not know where I am" while sitting next to the network
    // that would place it within twenty metres.
    //
    // Except in the background refresh, which makes do with whatever is on: that
    // happens every half hour and for ever, and cannot leave the GPS on in
    // perpetuity. Whoever SEARCHES for the phone does turn everything on.
    if (turnOn) {
        turnOnWhatIsNeeded();
    }
    m_fix = Fix();
    m_fix.when = QDateTime::currentDateTime();

    // The cheap channels first and unconditionally, so that even an attempt
    // that ends without a GNSS lock still answers something true.
    if (settings.locateCell) {
        readCell(m_fix);
    }
    if (settings.locateWifi) {
        readWifi(m_fix);
    }
    readBattery(m_fix);

    if (!settings.locateGnss) {
        complete();
        return;
    }

    // The deadline and the tuning window are armed HERE, before the first GPS
    // read. They were at the end, in the "has not fixed yet" branch, and that made
    // the immediate read below compare against the PREVIOUS location's deadline --
    // which had already passed. Effect: if the GPS already had a fix, it rushed
    // off with the first one instead of tuning for 20 s. Measured on the surya
    // 2026-09-06: "first fix; tuning 20 s" and the position, both in the same
    // second.
    m_deadline = QDateTime::currentDateTime().addSecs(qMax(10, settings.gnssTimeoutSeconds));
    m_refining = QDateTime();

    // The polling is armed BEFORE the first read, and the first read is a call to
    // the same pollGnss() rather than a copy of it.
    //
    // There used to be the pollGnss() block duplicated by hand here, and that copy
    // cost two bugs: it compared against the previous location's deadline (so it
    // left with the first fix instead of tuning), and when it decided to wait it
    // returned without starting the timer -- leaving the location hung for ever,
    // with the GPS already fixed, which is the most common case of all.
    m_poll.start();
    pollGnss();
}

void Locator::pollGnss()
{
    if (readGnss(m_fix)) {
        if (!m_refining.isValid()) {
            m_refining = QDateTime::currentDateTime().addSecs(kRefineSeconds);
            qInfo() << "lost-phoned: first fix; tuning"
                    << kRefineSeconds << "s before answering";
        }
        // The overall deadline rules: tuning cannot extend it.
        const QDateTime now = QDateTime::currentDateTime();
        if (now < m_refining && now < m_deadline) {
            return;
        }
        complete();
        return;
    }
    if (QDateTime::currentDateTime() >= m_deadline) {
        complete();
    }
}

void Locator::complete()
{
    m_poll.stop();
    m_busy = false;
    m_fix.when = QDateTime::currentDateTime();

    if (m_fix.hasCoordinates) {
        LastKnown fix;
        fix.valid = true;
        fix.latitude = m_fix.latitude;
        fix.longitude = m_fix.longitude;
        fix.accuracyMeters = m_fix.accuracyMeters;
        fix.source = m_fix.source;
        fix.when = m_fix.when;
        LostPhoneConfig::saveLastKnown(fix);
    } else if (m_settings.locateLastKnown && m_settings.lastKnown.valid) {
        // No fix now, but we have one from before. It is reported as what it is
        // -- a position from another moment -- because a stale coordinate
        // presented as current is how people end up searching the wrong street.
        m_fix.hasCoordinates = true;
        m_fix.latitude = m_settings.lastKnown.latitude;
        m_fix.longitude = m_settings.lastKnown.longitude;
        m_fix.accuracyMeters = m_settings.lastKnown.accuracyMeters;
        m_fix.source = QStringLiteral("last-known");
        m_fix.when = m_settings.lastKnown.when;
    }

    Q_EMIT finished(m_fix);
}
