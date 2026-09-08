// SPDX-License-Identifier: GPL-2.0-or-later
//
// The one bit of the locate cascade that is pure arithmetic, kept on its own so
// it can be tested without a modem, a phone or a Qt event loop.
//
// It earns the separate file: NMEA reports ddmm.mmmm and NOT degrees, and
// reading it as degrees puts a phone in Madrid nineteen kilometres south of
// where it is -- wrong, but plausible enough that nobody notices until they are
// standing in the wrong street.

#pragma once

#include <QString>

namespace Nmea
{
// $GPGGA,hhmmss,llll.ll,a,yyyyy.yy,a,q,...
//
// Returns false when the fix quality field (q) is 0 or empty, which is what the
// surya reports indoors with eleven satellites in view -- measured, not
// assumed. A caller that ignores the return value would place the phone at
// 0,0, in the Atlantic.
bool parseGga(const QString &sentence, double &latitude, double &longitude);
}
