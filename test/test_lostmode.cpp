// Lost mode is a STATE, not a variable, and this checks exactly that: that it
// survives the process dying, that a half-written file is not read as "not
// lost", and that the contract with smart-unlock -- first line "1" or "0" -- is
// the one smart-unlock expects.
#include "lostmode.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(LostModeState::filePath());

    std::printf("\n--- a phone that has never been lost ---\n");
    check("without a file, it is not lost", !LostModeState::isActive());
    check("and it carries no message", LostModeState::load().message.isEmpty());

    std::printf("\n--- entering lost mode ---\n");
    {
        LostMode state;
        state.active = true;
        state.message = QStringLiteral("Phone perdido, llama al 600 123 456");
        state.since = QDateTime::currentDateTime();
        LostModeState::save(state);

        // Re-read from scratch, as the daemon would after a reboot: that is the
        // reason this exists.
        const LostMode leido = LostModeState::load();
        check("it survives re-reading the file", leido.active);
        check("the message survives whole",
                  leido.message == QLatin1String("Phone perdido, llama al 600 123 456"));
        check("the time survives", leido.since.isValid());
    }

    std::printf("\n--- the contract with smart-unlock ---\n");
    {
        // smart-unlockd reads THIS file by path and looks at the FIRST LINE. If
        // this changes, the remote lock stops winning and nobody notices.
        QFile f(LostModeState::filePath());
        check("the file can be opened", f.open(QIODevice::ReadOnly));
        const QString primera = QString::fromUtf8(f.readLine()).trimmed();
        check("the first line is exactly 1", primera == QLatin1String("1"));
        check("the path is the one smart-unlock looks for",
                  LostModeState::filePath().endsWith(
                      QLatin1String("/lost-phone/modo-perdido")));
    }

    std::printf("\n--- leaving lost mode ---\n");
    {
        LostMode state = LostModeState::load();
        state.active = false;
        LostModeState::save(state);
        check("it is no longer lost", !LostModeState::isActive());

        QFile f(LostModeState::filePath());
        check("the file is still there", f.open(QIODevice::ReadOnly));
        check("and the first line is exactly 0",
                  QString::fromUtf8(f.readLine()).trimmed() == QLatin1String("0"));
    }

    std::printf("\n--- a broken file cannot be read as 'lost' ---\n");
    {
        // The ugly case: the phone runs out of battery mid-write. With QSaveFile
        // it cannot happen, but if someone leaves garbage there, it has to be
        // read as NOT lost -- because a made-up lost mode locks you out of your
        // own phone.
        QFile f(LostModeState::filePath());
        check("I can write garbage for the test",
                  f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("basur");
        f.close();
        check("garbage is read as not lost", !LostModeState::isActive());

        QFile vacio(LostModeState::filePath());
        check("I can leave it empty for the test",
                  vacio.open(QIODevice::WriteOnly | QIODevice::Truncate));
        vacio.close();
        check("an empty file is read as not lost", !LostModeState::isActive());
    }

    std::printf("\n%d checks, %d failures\n", total, failures);
    return failures == 0 ? 0 : 1;
}
