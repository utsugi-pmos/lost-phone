// SPDX-License-Identifier: GPL-2.0-or-later

#include "nmea.h"

#include <QStringList>

// $GPGGA,hhmmss,llll.ll,a,yyyyy.yy,a,q,... -- q is the fix quality and 0 means
// "no fix", which is what an indoor phone reports even with satellites in view.
bool Nmea::parseGga(const QString &sentence, double &lat, double &lon)
{
    const QStringList f = sentence.split(QLatin1Char(','));
    if (f.size() < 7 || f.at(6).trimmed() == QLatin1String("0") || f.at(6).trimmed().isEmpty()) {
        return false;
    }
    if (f.at(2).isEmpty() || f.at(4).isEmpty()) {
        return false;
    }
    bool ok1 = false;
    bool ok2 = false;
    const double rawLat = f.at(2).toDouble(&ok1);
    const double rawLon = f.at(4).toDouble(&ok2);
    if (!ok1 || !ok2) {
        return false;
    }
    // NMEA is ddmm.mmmm, not degrees. Getting this wrong puts the phone in the
    // sea off Africa, which is the classic symptom.
    const int latDegrees = int(rawLat / 100);
    const int lonDegrees = int(rawLon / 100);
    const double latValue = latDegrees + (rawLat - latDegrees * 100) / 60.0;
    const double lonValue = lonDegrees + (rawLon - lonDegrees * 100) / 60.0;
    lat = (f.at(3) == QLatin1String("S")) ? -latValue : latValue;
    lon = (f.at(5) == QLatin1String("W")) ? -lonValue : lonValue;
    return true;
}
