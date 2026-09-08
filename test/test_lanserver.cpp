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

static int fallos = 0;
static int total = 0;
static int ordenes = 0;
static QString ultimaOrden;

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

// Waits for something to happen, or gives up. Without this, a network test that
// fails hangs instead of saying it has failed.
static bool esperar(const std::function<bool()> &listo, int msMax = 3000)
{
    QElapsedTimer reloj;
    reloj.start();
    while (!listo() && reloj.elapsed() < msMax) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return listo();
}

static void dormir(int ms)
{
    QElapsedTimer reloj;
    reloj.start();
    while (reloj.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

// Sends a request in CHUNKS, with a pause between them, to force the case that
// used to break.
static QByteArray hablar(const QList<QByteArray> &trozos, int pausaMs = 150)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, 8479);
    if (!socket.waitForConnected(2000)) {
        return "SIN CONEXION";
    }
    for (int i = 0; i < trozos.size(); ++i) {
        socket.write(trozos.at(i));
        socket.flush();
        if (i + 1 < trozos.size()) {
            dormir(pausaMs);
        }
    }
    QByteArray respuesta;
    QElapsedTimer reloj;
    reloj.start();
    while (reloj.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        respuesta += socket.readAll();
        if (respuesta.contains("\r\n\r\n") && respuesta.size() > 40) {
            break;
        }
    }
    return respuesta;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    Settings ajustes;
    ajustes.enabled = true;
    ajustes.lanEnabled = true;
    ajustes.lanToken = QStringLiteral("testigo-de-prueba");
    ajustes.lanAllowRing = true;

    LanServer servidor;
    servidor.setStatusProvider([] { return QByteArray("{\"sonando\":false}"); });
    QObject::connect(&servidor, &LanServer::command, [](const QString &verbo) {
        ++ordenes;
        ultimaOrden = verbo;
    });
    servidor.configure(ajustes);

    comprobar("the server listens", esperar([&] { return servidor.listening(); }));
    if (!servidor.listening()) {
        std::printf("\n%d checks, %d failures\n", total, fallos);
        return 1;
    }

    std::printf("\n--- without a witness you do not get in ---\n");
    comprobar("without an authorization header, 401",
              hablar({"GET /estado HTTP/1.1\r\nHost: x\r\n\r\n"}).startsWith("HTTP/1.1 401"));
    comprobar("with a witness that is not the one, 401",
              hablar({"GET /estado HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer otro\r\n\r\n"})
                  .startsWith("HTTP/1.1 401"));

    std::printf("\n--- the witness is read from the HEADERS, not the body ---\n");
    {
        const QByteArray cuerpo = "Authorization: Bearer testigo-de-prueba";
        const QByteArray r = hablar({"POST /orden HTTP/1.1\r\nHost: x\r\nContent-Length: "
                                     + QByteArray::number(cuerpo.size()) + "\r\n\r\n" + cuerpo});
        comprobar("a body pretending to be the header does not authorize", r.startsWith("HTTP/1.1 401"));
    }

    std::printf("\n--- a request split across two TCP segments ---\n");
    {
        const int antes = ordenes;
        const QByteArray r = hablar({"GET /estado HTTP/1.1\r\nHost: x\r\nAuthor",
                                     "ization: Bearer testigo-de-prueba\r\n\r\n"});
        comprobar("the split headers are reassembled", r.startsWith("HTTP/1.1 200"));
        comprobar("and it answers the status", r.contains("sonando"));
        comprobar("without firing any order", ordenes == antes);
    }

    std::printf("\n--- a POST with the body in another segment ---\n");
    {
        const int antes = ordenes;
        hablar({"POST /orden HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer testigo-de-prueba\r\n"
                "Content-Length: 5\r\n\r\n",
                "sonar"});
        comprobar("the order arrives even when the body comes separately",
                  esperar([&] { return ordenes == antes + 1; }));
        comprobar("and it arrives WHOLE, not cut off", ultimaOrden == QLatin1String("sonar"));
    }

    std::printf("\n--- a normal request, in one piece ---\n");
    {
        const int antes = ordenes;
        const QByteArray r = hablar(
            {"POST /orden HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer testigo-de-prueba\r\n"
             "Content-Length: 5\r\n\r\nparar"});
        comprobar("it answers 200", r.startsWith("HTTP/1.1 200"));
        comprobar("it fires the order", esperar([&] { return ordenes == antes + 1; }));
        comprobar("and it is the one that was sent", ultimaOrden == QLatin1String("parar"));
    }

    std::printf("\n--- the simple door, off ---\n");
    {
        // Off is how it is installed. Even with the right key and everything, it
        // does not open.
        const int antes = ordenes;
        const QByteArray r = hablar({"GET /sonar?clave=abreteSesamo HTTP/1.1\r\nHost: x\r\n\r\n"});
        comprobar("off, not even with the good key", r.startsWith("HTTP/1.1 401"));
        comprobar("and it fires no order", ordenes == antes);
    }

    ajustes.lanSimpleEnabled = true;
    ajustes.lanSimpleKey = QStringLiteral("abreteSesamo");
    servidor.configure(ajustes);

    std::printf("\n--- the simple door, on ---\n");
    {
        const int antes = ordenes;
        comprobar("without a key, 401",
                  hablar({"GET /sonar HTTP/1.1\r\nHost: x\r\n\r\n"}).startsWith("HTTP/1.1 401"));
        comprobar("with the wrong key, 401",
                  hablar({"GET /sonar?clave=otra HTTP/1.1\r\nHost: x\r\n\r\n"})
                      .startsWith("HTTP/1.1 401"));
        // The key is compared WHOLE. A prefix is no good, or guessing the first
        // letter and probing would be enough.
        comprobar("a prefix of the key is no good",
                  hablar({"GET /sonar?clave=abre HTTP/1.1\r\nHost: x\r\n\r\n"})
                      .startsWith("HTTP/1.1 401"));
        comprobar("none of that fired an order", ordenes == antes);

        const QByteArray r =
            hablar({"GET /sonar?clave=abreteSesamo HTTP/1.1\r\nHost: x\r\n\r\n"});
        comprobar("with the good key, 200", r.startsWith("HTTP/1.1 200"));
        comprobar("and the order arrives", esperar([&] { return ordenes == antes + 1; }));
        comprobar("and it is the one that was asked for", ultimaOrden == QLatin1String("sonar"));
        // Plain text: a person reads this in a browser, not a program.
        comprobar("it answers in plain text", r.contains("text/plain"));
    }
    {
        // The simple door ONLY rings and stops. Not the status, not locate, not
        // lock -- even if the home channel has those permissions.
        //
        // This key travels in the URL: it ends up in the browser history, in the
        // router logs and in the home-automation script where you pasted it. A
        // key written in five places cannot be the one that opens your
        // coordinates, and the status carries them when the channel can locate.
        const int antes = ordenes;
        for (const char *ruta : {"GET /estado?clave=abreteSesamo HTTP/1.1\r\nHost: x\r\n\r\n",
                                 "GET /donde?clave=abreteSesamo HTTP/1.1\r\nHost: x\r\n\r\n",
                                 "GET /bloquear?clave=abreteSesamo HTTP/1.1\r\nHost: x\r\n\r\n"}) {
            const QByteArray r = hablar({QByteArray(ruta)});
            comprobar("the simple door rejects it", r.startsWith("HTTP/1.1 403"));
        }
        comprobar("and none of the three fired anything", ordenes == antes);

        // Stop, yes, which is the other half of a home-automation button.
        const QByteArray r =
            hablar({"GET /parar?clave=abreteSesamo HTTP/1.1\r\nHost: x\r\n\r\n"});
        comprobar("but stop, yes", r.startsWith("HTTP/1.1 200"));
        comprobar("and it arrives", esperar([&] { return ordenes == antes + 1; }));
    }
    {
        // The header witness still works with the door on: they are two
        // entrances, not one replacing the other.
        const QByteArray r = hablar({"GET /estado HTTP/1.1\r\nHost: x\r\n"
                                     "Authorization: Bearer testigo-de-prueba\r\n\r\n"});
        comprobar("and the usual witness still works", r.startsWith("HTTP/1.1 200"));
    }

    std::printf("\n%d checks, %d failures\n", total, fallos);
    return fallos == 0 ? 0 : 1;
}
