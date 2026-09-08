// The NMEA conversion, on real sentences captured from this phone.
//
// NMEA reports ddmm.mmmm, not degrees. Reading it as degrees puts a phone in
// Madrid at 40.24 N instead of 40.41 N -- nineteen kilometres away, and still
// plausible enough that nobody notices until they are standing in the wrong
// street. It is exactly the class of bug a test catches and a phone in a drawer
// does not.
//
// The "no fix" case is not a corner case here: it is what the surya reports
// indoors, with eleven satellites in view, measured 2026-09-04.
#include "nmea.h"

#include <cmath>
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

int main()
{
    double lat = 0.0;
    double lon = 0.0;

    std::printf("\n--- no fix, which is normal indoors ---\n");
    {
        // Captured from the surya: eleven satellites in view and quality 0.
        comprobar("an empty GPGGA gives no position",
                  !Nmea::parseGga(QStringLiteral("$GPGGA,,,,,,0,,,,,,,,*66"), lat, lon));
        comprobar("quality 0 gives no position",
                  !Nmea::parseGga(
                      QStringLiteral("$GPGGA,134923.00,4025.0068,N,00342.2274,W,0,04,2.1,660.0,M,,M,,*5A"),
                      lat, lon));
        comprobar("a truncated sentence gives no position",
                  !Nmea::parseGga(QStringLiteral("$GPGGA,134923.00"), lat, lon));
    }

    std::printf("\n--- with a fix ---\n");
    {
        // 4025.0068 N = 40 degrees + 25.0068 minutes = 40.41678 degrees.
        // 00342.2274 W = -(3 + 42.2274/60) = -3.70379 degrees.
        const bool ok = Nmea::parseGga(
            QStringLiteral("$GPGGA,134923.00,4025.0068,N,00342.2274,W,1,08,1.1,660.0,M,,M,,*5A"),
            lat, lon);
        comprobar("a sentence with a fix does give a position", ok);
        comprobar("degrees are degrees and not ddmm (latitude)", std::fabs(lat - 40.41678) < 0.0001);
        comprobar("degrees are degrees and not ddmm (longitude)", std::fabs(lon + 3.70379) < 0.0001);
        comprobar("west is negative", lon < 0);
    }

    std::printf("\n--- southern and eastern hemisphere ---\n");
    {
        const bool ok = Nmea::parseGga(
            QStringLiteral("$GPGGA,134923.00,3352.1234,S,15112.5678,E,1,08,1.1,10.0,M,,M,,*00"),
            lat, lon);
        comprobar("south and east are read", ok);
        comprobar("south is negative", lat < 0);
        comprobar("Sydney, roughly", std::fabs(lat + 33.86872) < 0.001 && std::fabs(lon - 151.20946) < 0.001);
    }

    std::printf("\n%d checks, %d failures\n", total, fallos);
    return fallos == 0 ? 0 : 1;
}
