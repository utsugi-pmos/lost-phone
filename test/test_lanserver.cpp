// The HTTP handling of the home channel, which is the most exposed surface of
// this whole thing: it listens on a port on the network.
//
// This test exists because of two REAL bugs that were there and were found by
// READING, not by failing -- which is exactly the reason to write it: both work
// on the bench and fail the day it matters.
//
//   1. `readAll()` empties the socket. If the headers arrived split across two
//      TCP segments, the first half was THROWN AWAY and the connection hung
//      forever waiting for an end-of-headers that had already gone by.
//
//   2. The body might not have arrived yet. A split POST gave an empty or
//      truncated verb -- "son" instead of "sonar" -- that does nothing and does
//      not say why.
//
// On a quiet LAN a small request almost always arrives in one piece. That is why
// it has to be split ON PURPOSE here.
#include "lanserver.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpSocket>

#include <cstdio>
#include <functional>

static int failures = 0;
static int total = 0;
static int commands = 0;
static QString lastCommand;

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

// Waits for something to happen, or gives up. Without this, a network test that
// fails hangs instead of saying it has failed.
static bool wait_for(const std::function<bool()> &ready, int msMax = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < msMax) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return ready();
}

static void sleep_for(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

// Sends a request in CHUNKS, with a pause between them, to force the case that
// used to break.
static QByteArray speak(const QList<QByteArray> &chunks, int pauseMs = 150)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, 8479);
    if (!socket.waitForConnected(2000)) {
        return "SIN CONEXION";
    }
    for (int i = 0; i < chunks.size(); ++i) {
        socket.write(chunks.at(i));
        socket.flush();
        if (i + 1 < chunks.size()) {
            sleep_for(pauseMs);
        }
    }
    QByteArray answer;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        answer += socket.readAll();
        if (answer.contains("\r\n\r\n") && answer.size() > 40) {
            break;
        }
    }
    return answer;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    Settings settings;
    settings.enabled = true;
    settings.lanEnabled = true;
    settings.lanToken = QStringLiteral("token-de-test");
    settings.lanAllowRing = true;

    LanServer server;
    server.setStatusProvider([] { return QByteArray("{\"ringing\":false}"); });
    QObject::connect(&server, &LanServer::command, [](const QString &verb) {
        ++commands;
        lastCommand = verb;
    });
    server.configure(settings);

    check("the server listens", wait_for([&] { return server.listening(); }));
    if (!server.listening()) {
        std::printf("\n%d checks, %d failures\n", total, failures);
        return 1;
    }

    std::printf("\n--- without a witness you do not get in ---\n");
    check("without an authorization header, 401",
              speak({"GET /state HTTP/1.1\r\nHost: x\r\n\r\n"}).startsWith("HTTP/1.1 401"));
    check("with a witness that is not the one, 401",
              speak({"GET /state HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer other\r\n\r\n"})
                  .startsWith("HTTP/1.1 401"));

    std::printf("\n--- the witness is read from the HEADERS, not the body ---\n");
    {
        const QByteArray body = "Authorization: Bearer token-de-test";
        const QByteArray r = speak({"POST /command HTTP/1.1\r\nHost: x\r\nContent-Length: "
                                     + QByteArray::number(body.size()) + "\r\n\r\n" + body});
        check("a body pretending to be the header does not authorize", r.startsWith("HTTP/1.1 401"));
    }

    std::printf("\n--- a request split across two TCP segments ---\n");
    {
        const int antes = commands;
        const QByteArray r = speak({"GET /state HTTP/1.1\r\nHost: x\r\nAuthor",
                                     "ization: Bearer token-de-test\r\n\r\n"});
        check("the split headers are reassembled", r.startsWith("HTTP/1.1 200"));
        check("and it answers the status", r.contains("ringing"));
        check("without firing any order", commands == antes);
    }

    std::printf("\n--- a POST with the body in another segment ---\n");
    {
        const int antes = commands;
        speak({"POST /command HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer token-de-test\r\n"
                "Content-Length: 5\r\n\r\n",
                "sonar"});
        check("the order arrives even when the body comes separately",
                  wait_for([&] { return commands == antes + 1; }));
        check("and it arrives WHOLE, not cut off", lastCommand == QLatin1String("sonar"));
    }

    std::printf("\n--- a normal request, in one piece ---\n");
    {
        const int antes = commands;
        const QByteArray r = speak(
            {"POST /command HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer token-de-test\r\n"
             "Content-Length: 5\r\n\r\nparar"});
        check("it answers 200", r.startsWith("HTTP/1.1 200"));
        check("it fires the order", wait_for([&] { return commands == antes + 1; }));
        check("and it is the one that was sent", lastCommand == QLatin1String("parar"));
    }

    std::printf("\n--- the simple door, off ---\n");
    {
        // Off is how it is installed. Even with the right key and everything, it
        // does not open.
        const int antes = commands;
        const QByteArray r = speak({"GET /sonar?key=openSesame HTTP/1.1\r\nHost: x\r\n\r\n"});
        check("off, not even with the good key", r.startsWith("HTTP/1.1 401"));
        check("and it fires no order", commands == antes);
    }

    settings.lanSimpleEnabled = true;
    settings.lanSimpleKey = QStringLiteral("openSesame");
    server.configure(settings);

    std::printf("\n--- the simple door, on ---\n");
    {
        const int antes = commands;
        check("without a key, 401",
                  speak({"GET /sonar HTTP/1.1\r\nHost: x\r\n\r\n"}).startsWith("HTTP/1.1 401"));
        check("with the wrong key, 401",
                  speak({"GET /sonar?key=other HTTP/1.1\r\nHost: x\r\n\r\n"})
                      .startsWith("HTTP/1.1 401"));
        // The key is compared WHOLE. A prefix is no good, or guessing the first
        // letter and probing would be enough.
        check("a prefix of the key is no good",
                  speak({"GET /sonar?key=opens HTTP/1.1\r\nHost: x\r\n\r\n"})
                      .startsWith("HTTP/1.1 401"));
        check("none of that fired an order", commands == antes);

        const QByteArray r =
            speak({"GET /sonar?key=openSesame HTTP/1.1\r\nHost: x\r\n\r\n"});
        check("with the good key, 200", r.startsWith("HTTP/1.1 200"));
        check("and the order arrives", wait_for([&] { return commands == antes + 1; }));
        check("and it is the one that was asked for", lastCommand == QLatin1String("sonar"));
        // Plain text: a person reads this in a browser, not a program.
        check("it answers in plain text", r.contains("text/plain"));
    }
    {
        // The simple door ONLY rings and stops. Not the status, not locate, not
        // lock -- even if the home channel has those permissions.
        //
        // This key travels in the URL: it ends up in the browser history, in the
        // router logs and in the home-automation script where you pasted it. A
        // key written in five places cannot be the one that opens your
        // coordinates, and the status carries them when the channel can locate.
        const int antes = commands;
        for (const char *path : {"GET /state?key=openSesame HTTP/1.1\r\nHost: x\r\n\r\n",
                                 "GET /donde?key=openSesame HTTP/1.1\r\nHost: x\r\n\r\n",
                                 "GET /bloquear?key=openSesame HTTP/1.1\r\nHost: x\r\n\r\n"}) {
            const QByteArray r = speak({QByteArray(path)});
            check("the simple door rejects it", r.startsWith("HTTP/1.1 403"));
        }
        check("and none of the three fired anything", commands == antes);

        // Stop, yes, which is the other half of a home-automation button.
        const QByteArray r =
            speak({"GET /parar?key=openSesame HTTP/1.1\r\nHost: x\r\n\r\n"});
        check("but stop, yes", r.startsWith("HTTP/1.1 200"));
        check("and it arrives", wait_for([&] { return commands == antes + 1; }));
    }
    {
        // The header witness still works with the door on: they are two
        // entrances, not one replacing the other.
        const QByteArray r = speak({"GET /state HTTP/1.1\r\nHost: x\r\n"
                                     "Authorization: Bearer token-de-test\r\n\r\n"});
        check("and the usual witness still works", r.startsWith("HTTP/1.1 200"));
    }

    std::printf("\n%d checks, %d failures\n", total, failures);
    return failures == 0 ? 0 : 1;
}
