// The check of the ntfy key, which is the channel's only real defense.
//
// On a public ntfy anyone who guesses the topic can publish to it. This is what
// separates that stranger from a phone that starts screaming, so it deserves
// tests of its own -- and with no network, no server and no event loop.
#include "ntfyprotocol.h"

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

static QByteArray mensaje(const char *texto)
{
    return QByteArray("{\"event\":\"message\",\"topic\":\"t\",\"message\":\"") + texto + "\"}";
}

int main()
{
    const QString clave = QStringLiteral("miclave");

    std::printf("\n--- what is NOT an order ---\n");
    comprobar("a keepalive is not an order",
              NtfyProtocol::commandFrom("{\"event\":\"keepalive\",\"topic\":\"t\"}", clave).isEmpty());
    comprobar("the open event is not one either",
              NtfyProtocol::commandFrom("{\"event\":\"open\",\"topic\":\"t\"}", clave).isEmpty());
    comprobar("a line that is not JSON is not one either",
              NtfyProtocol::commandFrom("esto no es json", clave).isEmpty());
    comprobar("an empty message is not one either",
              NtfyProtocol::commandFrom(mensaje(""), clave).isEmpty());

    std::printf("\n--- the key rules ---\n");
    comprobar("without the key in front, it does not obey",
              NtfyProtocol::commandFrom(mensaje("sonar"), clave).isEmpty());
    comprobar("with another key, it does not obey",
              NtfyProtocol::commandFrom(mensaje("otraclave sonar"), clave).isEmpty());

    // The important bit: MENTIONING the key is not enough, it has to come FIRST.
    // Otherwise, any conversation that quoted it would fire orders.
    comprobar("mentioning the key in the middle is not enough",
              NtfyProtocol::commandFrom(mensaje("oye la clave es miclave sonar"), clave).isEmpty());
    comprobar("the key stuck to another word does not pass",
              NtfyProtocol::commandFrom(mensaje("miclavex sonar"), clave).isEmpty());
    comprobar("the key alone, without a verb, does nothing",
              NtfyProtocol::commandFrom(mensaje("miclave"), clave).isEmpty());

    // And with no key configured the channel is dead, whatever is sent.
    comprobar("with no key configured it obeys nothing",
              NtfyProtocol::commandFrom(mensaje("miclave sonar"), QString()).isEmpty());

    std::printf("\n--- what IS an order ---\n");
    comprobar("key and verb", NtfyProtocol::commandFrom(mensaje("miclave sonar"), clave)
                                   == QLatin1String("sonar"));
    comprobar("it is case-insensitive",
              NtfyProtocol::commandFrom(mensaje("MiClave sonar"), clave) == QLatin1String("sonar"));
    comprobar("extra spaces do not matter",
              NtfyProtocol::commandFrom(mensaje("  miclave   sonar  "), clave)
                  == QLatin1String("sonar"));
    comprobar("the lock argument arrives whole",
              NtfyProtocol::commandFrom(mensaje("miclave bloquear llama al 600 123 456"), clave)
                  == QLatin1String("bloquear llama al 600 123 456"));

    std::printf("\n%d checks, %d failures\n", total, fallos);
    return fallos == 0 ? 0 : 1;
}
