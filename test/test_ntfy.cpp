// The check of the ntfy key, which is the channel's only real defense.
//
// On a public ntfy anyone who guesses the topic can publish to it. This is what
// separates that stranger from a phone that starts screaming, so it deserves
// tests of its own -- and with no network, no server and no event loop.
#include "ntfyprotocol.h"

#include <cstdio>

static int failures = 0;
static int total = 0;

static void check(const char *name, bool ok)
{
    ++total;
    if (!ok) {
        ++failures;
        std::printf("  FAIL   %s\n", name);
    } else {
        std::printf("  ok     %s\n", name);
    }
}

static QByteArray message(const char *text)
{
    return QByteArray("{\"event\":\"message\",\"topic\":\"t\",\"message\":\"") + text + "\"}";
}

int main()
{
    const QString key = QStringLiteral("mykey");

    std::printf("\n--- what is NOT an order ---\n");
    check("a keepalive is not an order",
              NtfyProtocol::commandFrom("{\"event\":\"keepalive\",\"topic\":\"t\"}", key).isEmpty());
    check("the open event is not one either",
              NtfyProtocol::commandFrom("{\"event\":\"open\",\"topic\":\"t\"}", key).isEmpty());
    check("a line that is not JSON is not one either",
              NtfyProtocol::commandFrom("this is not json", key).isEmpty());
    check("an empty message is not one either",
              NtfyProtocol::commandFrom(message(""), key).isEmpty());

    std::printf("\n--- the key rules ---\n");
    check("without the key in front, it does not obey",
              NtfyProtocol::commandFrom(message("sonar"), key).isEmpty());
    check("with another key, it does not obey",
              NtfyProtocol::commandFrom(message("otherkey sonar"), key).isEmpty());

    // The important bit: MENTIONING the key is not enough, it has to come FIRST.
    // Otherwise, any conversation that quoted it would fire orders.
    check("mentioning the key in the middle is not enough",
              NtfyProtocol::commandFrom(message("hey the key is mykey sonar"), key).isEmpty());
    check("the key stuck to another word does not pass",
              NtfyProtocol::commandFrom(message("mykeyx sonar"), key).isEmpty());
    check("the key alone, without a verb, does nothing",
              NtfyProtocol::commandFrom(message("mykey"), key).isEmpty());

    // And with no key configured the channel is dead, whatever is sent.
    check("with no key configured it obeys nothing",
              NtfyProtocol::commandFrom(message("mykey sonar"), QString()).isEmpty());

    std::printf("\n--- what IS an order ---\n");
    check("key and verb", NtfyProtocol::commandFrom(message("mykey sonar"), key)
                                   == QLatin1String("sonar"));
    check("it is case-insensitive",
              NtfyProtocol::commandFrom(message("MyKey sonar"), key) == QLatin1String("sonar"));
    check("extra spaces do not matter",
              NtfyProtocol::commandFrom(message("  mykey   sonar  "), key)
                  == QLatin1String("sonar"));
    check("the lock argument arrives whole",
              NtfyProtocol::commandFrom(message("mykey bloquear call 600 123 456"), key)
                  == QLatin1String("bloquear call 600 123 456"));

    std::printf("\n%d checks, %d failures\n", total, failures);
    return failures == 0 ? 0 : 1;
}
