// SPDX-License-Identifier: GPL-2.0-or-later
//
// Working out where the phone is, by trying every channel it has and saying
// honestly which one answered.
//
// Measured on the surya indoors, 2026-09-04:
//
//   GPS   $GPGSA,A,1,...    eleven satellites in view, NO fix
//   3GPP  mcc 214 mnc 03 lac 3AFE cell id 07F17CB5   <- this did answer
//
// That is the whole argument for a cascade instead of a GPS call. Indoors the
// GNSS gives nothing and the cell gives you the neighbourhood; outdoors it is
// the other way round. A "find my phone" that only asks the GPS is useless in
// the exact place people lose phones.
//
// Both come out of ONE call to `mmcli -m 0 --location-get`, which already has
// `3gpp-lac-ci, gps-raw, gps-nmea` enabled on this device and which works as
// the ordinary user -- no root, no GeoClue in the middle.
//
// The Wi-Fi rung is deliberately half a rung in phase 1: turning BSSIDs into
// coordinates needs a database, and that lookup belongs to the relay so that
// the phone never talks to a third party. What it CAN do without any database
// is name the network it can see, and "HomeWiFi, -45 dBm" answers "is it at
// home?" instantly.

#pragma once

#include "config.h"

#include <QObject>
#include <QStringList>
#include <QTimer>

struct Fix {
    bool hasCoordinates = false;
    double latitude = 0.0;
    double longitude = 0.0;
    int accuracyMeters = 0;
    QString source;  // "gnss", "last-known"

    // Context that is useful even without coordinates, and which is what an SMS
    // reply usually ends up carrying.
    QString cell;        // "214-03 LAC:3AFE CID:07F17CB5"
    QString wifi;        // "HomeWiFi -45dBm"
    int batteryPercent = -1;
    QStringList bssids;  // for the relay to resolve later; never sent by SMS

    QDateTime when;

    // One line, short enough for a single SMS, that says what is known and how
    // sure it is. Never claims a position it does not have.
    QString toSms() const;
};

class Locator : public QObject
{
    Q_OBJECT

public:
    explicit Locator(QObject *parent = nullptr);

    // Starts a fix attempt. Returns immediately; `finished` arrives when the
    // GNSS either locks or runs out of time. The cheap channels are read
    // straight away, so even a fix that times out carries the cell and the
    // Wi-Fi rather than nothing.
    // `turnOn` decides whether Wi-Fi, radio and GPS are turned on before
    // measuring.
    //
    // True when somebody is SEARCHING for the phone, because then the battery is
    // not what is protected. False for the background refresh that keeps the
    // panel's map fresh: that happens every half hour and for ever, and leaving
    // the GPS on in perpetuity for a refresh would trade the phone's battery for a
    // convenience.
    void locate(const Settings &settings, bool turnOn = true);

    bool busy() const { return m_busy; }

    // Turns on Wi-Fi, radio and the modem's location if they were off, and does
    // NOT turn them back off.
    //
    // It is public because it is not only the location that calls it: ANY order
    // that means "I am looking for my phone" -- ringing included -- should leave it
    // locatable. Whoever rings it is looking for it, and if Wi-Fi comes back along
    // the way, so do the home channel and the relay.
    //
    // A lost phone cares more about being found than about saving battery.
    void turnOnWhatIsNeeded();

    // Leaves Wi-Fi, the modem radio and the GPS as they were BEFORE we turned them
    // on, and only what we turned on ourselves: if Wi-Fi was already on, it is not
    // touched.
    //
    // Called when the alarm goes quiet. Whoever rings the phone and finds it on the
    // sofa has no reason to keep all the radios on for ever; whoever really lost it
    // is in lost mode, and there they are NOT put back -- turning them off when the
    // alarm goes quiet would leave it unlocatable just while you are looking for it.
    void giveBackRadios();

Q_SIGNALS:
    void finished(const Fix &fix);

private:
    void pollGnss();
    void complete();

    void readCell(Fix &fix);
    void readWifi(Fix &fix);
    void readBattery(Fix &fix);
    bool readGnss(Fix &fix);

    Fix m_fix;
    Settings m_settings;
    QTimer m_poll;
    QDateTime m_deadline;
    QDateTime m_refining;
    bool m_busy = false;
    bool m_encendio = false;  // whether the current attempt turned the radios on

    // What WE turned on, so it can be undone without touching anything else.
    bool m_encendiWifi = false;
    bool m_encendiWwan = false;
    bool m_encendiGps = false;
};
