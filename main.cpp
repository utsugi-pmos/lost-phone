// SPDX-License-Identifier: GPL-2.0-or-later
//
// Entry point of the standalone application. Deliberately thin: everything it
// does is in LostPhoneBackend, the same object the Settings module uses. It
// exists because a KCM can stop loading after a Plasma update, and the settings
// for a feature you only reach for in an emergency are exactly the ones that
// must not become unreachable.

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <KAboutData>
#include <KLocalizedContext>
#include <KLocalizedString>

#include "backend.h"

int main(int argc, char *argv[])
{
    // QApplication, not QGuiApplication: Kirigami pulls in QtWidgets styles for
    // its dialogs, and with QGuiApplication they fall back to something that
    // does not match the rest of the phone.
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("lost-phone"));

    KAboutData about(QStringLiteral("lost-phone"),
                     i18n("Find my phone"),
                     QStringLiteral("1.0"),
                     i18n("Ring the phone and find out where it is"),
                     KAboutLicense::GPL_V2);
    KAboutData::setApplicationData(about);

    QGuiApplication::setDesktopFileName(QStringLiteral("org.kde.lostphone"));

    // --alarma: the full-screen stop button, launched by the daemon when the
    // alarm starts. Same binary, same backend, a different first screen -- so
    // there is one place where "stop" is implemented and it cannot drift.
    const bool alarma = app.arguments().contains(QStringLiteral("--alarma"));

    LostPhoneBackend backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("lostPhoneBackend"), &backend);

    engine.loadFromModule("LostPhone", alarma ? "Alarma" : "App");

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
