#include "DefaultFileManager.h"
#include "MainWindow.h"
#include "PropertiesDialog.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <cstdio>

#if defined(ARGONAUT_DBUS)
#include "FileChooserPortal.h"
#include "FileManagerService.h"
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#endif

namespace {

void copyIfMissing(const QString &from, const QString &to)
{
    if (QFileInfo::exists(from) && !QFileInfo::exists(to)) {
        QDir().mkpath(QFileInfo(to).absolutePath());
        QFile::copy(from, to);
    }
}

// The app was called Kaftilous before 0.2 and Kosmos before 0.4. On the
// first start under the new name, bring over the newest old name's settings,
// stars, content-type cache and saved API key file. Old files are only
// copied, never removed, so going back to an old build still works.
void migrateFromOldNames()
{
    QSettings current;
    if (!current.allKeys().isEmpty())
        return; // already migrated, or used as Argonaut already
    for (const QString &old : {QStringLiteral("Kosmos"), QStringLiteral("Kaftilous")}) {
        QSettings previous(old, old);
        const QStringList keys = previous.allKeys();
        if (keys.isEmpty())
            continue;
        for (const QString &key : keys)
            current.setValue(key, previous.value(key));
        current.sync();

        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        copyIfMissing(data + QLatin1Char('/') + old.toLower() + "/starred", data + "/argonaut/starred");

        // CacheLocation and AppConfigLocation follow the names: look under the old ones.
        const QString cacheNow = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        const QString configNow = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QCoreApplication::setOrganizationName(old);
        QCoreApplication::setApplicationName(old);
        const QString cacheOld = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        const QString configOld = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QCoreApplication::setOrganizationName("Argonaut");
        QCoreApplication::setApplicationName("Argonaut");
        copyIfMissing(cacheOld + "/content-types.bin", cacheNow + "/content-types.bin");
        copyIfMissing(configOld + "/anthropic-key", configNow + "/anthropic-key");
        if (QFileInfo::exists(configNow + "/anthropic-key"))
            QFile::setPermissions(configNow + "/anthropic-key", QFile::ReadOwner | QFile::WriteOwner);

#if defined(Q_OS_LINUX)
        // The dock: a pin to the old name would show no icon. And if the old
        // name was the default file manager, the new one takes over.
        QProcess gsettings;
        gsettings.start(QStringLiteral("gsettings"), {QStringLiteral("get"), QStringLiteral("org.gnome.shell"),
                                                      QStringLiteral("favorite-apps")});
        if (gsettings.waitForFinished(3000) && gsettings.exitCode() == 0) {
            QString favorites = QString::fromUtf8(gsettings.readAllStandardOutput()).trimmed();
            const QString before = favorites;
            for (const QString &name : {QStringLiteral("kosmos.desktop"), QStringLiteral("kaftilous.desktop")})
                favorites.replace(QLatin1Char('\'') + name + QLatin1Char('\''), QStringLiteral("'argonaut.desktop'"));
            if (favorites != before && !favorites.contains(QLatin1String("'argonaut.desktop', 'argonaut.desktop'")))
                QProcess::execute(QStringLiteral("gsettings"), {QStringLiteral("set"), QStringLiteral("org.gnome.shell"),
                                                                QStringLiteral("favorite-apps"), favorites});
        }
        const QString folderApp = DefaultFileManager::currentDefault();
        if (folderApp == QLatin1String("kosmos.desktop") || folderApp == QLatin1String("kaftilous.desktop"))
            DefaultFileManager::makeDefault();
#endif
        return; // the newest old name only
    }
}

void setIdentity()
{
    QCoreApplication::setApplicationName("Argonaut");
    QCoreApplication::setApplicationVersion(QStringLiteral(ARGONAUT_VERSION)); // from CMakeLists.txt
    QCoreApplication::setOrganizationName("Argonaut"); // QSettings location
    // Only when Argonaut really starts: checking --version or --help must not
    // change anything.
    const QStringList args = QCoreApplication::arguments();
    if (!args.contains(QLatin1String("--version")) && !args.contains(QLatin1String("-v"))
        && !args.contains(QLatin1String("--help")) && !args.contains(QLatin1String("-h")))
        migrateFromOldNames();
}

// Paths or URLs from the command line, split into folders and other items.
void sortLocations(const QStringList &args, QStringList *folders, QStringList *items)
{
    for (const QString &arg : args) {
        const QUrl url = QUrl::fromUserInput(arg, QDir::currentPath(), QUrl::AssumeLocalFile);
        if (!url.isLocalFile())
            continue;
        const QString path = QDir::cleanPath(url.toLocalFile());
        if (QFileInfo(path).isDir())
            folders->append(path);
        else if (QFileInfo::exists(path))
            items->append(path); // a file: show it selected in its folder
    }
}

// --make-default / --restore-nautilus: no window, so they also work from a
// text console or over SSH, even if the app itself won't start.
int switchDefault(bool makeDefault)
{
    QString error;
    QString restoredTo;
    const bool ok = makeDefault ? DefaultFileManager::makeDefault(&error)
                                : DefaultFileManager::restorePrevious(&error, &restoredTo);
    if (!ok) {
        std::fprintf(stderr, "argonaut: %s\n", qPrintable(error));
        return 1;
    }
    if (makeDefault)
        DefaultFileManager::startService();
    if (makeDefault)
        std::printf("Argonaut now opens folders and \"Show in folder\".\n"
                    "To go back: argonaut --restore-nautilus\n");
    else
        std::printf("Folders are opened by %s again.\n", qPrintable(restoredTo));
    return 0;
}

#if defined(ARGONAUT_DBUS)
// Another `argonaut --service` already has "Show in folder", or waits for it.
bool anotherServiceRuns()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                       QStringLiteral("/org/freedesktop/DBus"),
                                                       QStringLiteral("org.freedesktop.DBus"),
                                                       QStringLiteral("ListQueuedOwners"));
    call << QString::fromLatin1(FileManagerService::ServiceName);
    const QDBusMessage reply = bus.call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return false; // nobody has it
    const QString me = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    for (const QString &owner : reply.arguments().constFirst().toStringList()) {
        const uint pid = bus.interface()->servicePid(owner).value();
        if (pid && QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).canonicalFilePath() == me)
            return true;
    }
    return false;
}
#endif

MainWindow *newWindow()
{
    auto *window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    return window;
}

void present(QWidget *window, const QString &startupId)
{
    // Lets the window come to the front when another app asked for it.
    if (!startupId.isEmpty()) {
        qputenv("XDG_ACTIVATION_TOKEN", startupId.toUtf8()); // Wayland
        qputenv("DESKTOP_STARTUP_ID", startupId.toUtf8());   // X11
    }
    window->show();
    window->raise();
    window->activateWindow();
}

} // namespace

int main(int argc, char *argv[])
{
    // The switches that need no window come first, before a display is needed.
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--make-default" || arg == "--restore-nautilus" || arg == "--restore-default") {
            QCoreApplication core(argc, argv);
            setIdentity();
            return switchDefault(arg == "--make-default");
        }
    }

    QApplication app(argc, argv);
    setIdentity();
    QGuiApplication::setDesktopFileName("argonaut"); // Wayland app id: the dock finds our icon
    QApplication::setWindowIcon(applicationIcon());
    // Same base style everywhere; our style sheet and drawing do the rest.
    QApplication::setStyle("Fusion");
    // Light / dark / follow system, from Preferences.
    applyThemeMode(savedThemeMode());

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("main", "A fast file manager."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption service("service", QApplication::translate(
        "main", "Answer \"Show in folder\" for other apps (started by D-Bus)."));
    parser.addOption(service);
    const QCommandLineOption portal("portal", QApplication::translate(
        "main", "Show Open and Save windows for other apps (started by D-Bus)."));
    parser.addOption(portal);
    parser.addOption({"make-default", QApplication::translate("main", "Make Argonaut the file manager for folders and \"Show in folder\".")});
    parser.addOption({{"restore-nautilus", "restore-default"},
                      QApplication::translate("main", "Give folders back to Nautilus (or the previous file manager).")});
    parser.addPositionalArgument("locations", QApplication::translate("main", "Folders to open, or files to show."),
                                 "[locations…]");
    parser.process(app);

#if defined(ARGONAUT_DBUS)
    if (parser.isSet(portal)) {
        // xdg-desktop-portal asks us for Open/Save windows (only when the
        // user chose Argonaut for them: PortalConfig). One window per request;
        // we stay a while for the next one, then leave (D-Bus starts us again).
        app.setQuitOnLastWindowClosed(false);
        FileChooserPortal chooser;
        if (!chooser.registerOn(QDBusConnection::sessionBus())) {
            std::fprintf(stderr, "argonaut: the Open/Save service is already running.\n");
            return 1;
        }
        QHash<int, QPointer<MainWindow>> windows;
        QElapsedTimer idle;
        idle.start();
        QObject::connect(&chooser, &FileChooserPortal::requested, &app,
                         [&chooser, &windows, &idle](int id, const ChooserRequest &request) {
                             idle.restart();
                             MainWindow *window = newWindow();
                             windows.insert(id, window);
                             QObject::connect(window, &MainWindow::chooserFinished, &chooser,
                                              [&chooser, &windows, &idle, id](uint response, const QVariantMap &results) {
                                                  idle.restart();
                                                  windows.remove(id);
                                                  chooser.finish(id, response, results);
                                              });
                             window->startChooser(request);
                             present(window, QString());
                         });
        QObject::connect(&chooser, &FileChooserPortal::closeRequested, &app, [&windows](int id) {
            if (MainWindow *window = windows.value(id))
                window->cancelChooser();
        });
        auto *leave = new QTimer(&app);
        QObject::connect(leave, &QTimer::timeout, &app, [&chooser, &idle] {
            if (chooser.pendingCount() == 0 && idle.elapsed() > 5 * 60 * 1000)
                QApplication::quit();
        });
        leave->start(30000);
        return app.exec();
    }
    if (parser.isSet(service)) {
        // D-Bus started us for org.freedesktop.FileManager1 (or Preferences
        // did). We stay running while Argonaut is the default: a Nautilus
        // started later (Ubuntu's desktop icons do) only waits in line for
        // the name, and would get it the moment we quit.
        app.setQuitOnLastWindowClosed(false);
        if (anotherServiceRuns())
            return 0;
        FileManagerService manager;
        if (!manager.registerOn(QDBusConnection::sessionBus(), FileManagerService::IfTaken::WaitInLine)) {
            std::fprintf(stderr, "argonaut: couldn't ask the session bus for \"Show in folder\".\n");
            return 1;
        }
        QObject::connect(&manager, &FileManagerService::showFolders, [](const QStringList &paths, const QString &id) {
            if (paths.isEmpty())
                return;
            MainWindow *window = newWindow();
            window->openFolders(paths);
            present(window, id);
        });
        QObject::connect(&manager, &FileManagerService::showItems, [](const QStringList &paths, const QString &id) {
            if (paths.isEmpty())
                return;
            MainWindow *window = newWindow();
            window->showItems(paths);
            present(window, id);
        });
        QObject::connect(&manager, &FileManagerService::showItemProperties,
                         [](const QStringList &paths, const QString &id) {
                             if (paths.isEmpty())
                                 return;
                             auto *dialog = new PropertiesDialog(paths, nullptr);
                             dialog->setAttribute(Qt::WA_DeleteOnClose);
                             present(dialog, id);
                         });
        // Switched back to Nautilus (our service file is gone): leave once no
        // window is open, so Nautilus, waiting in line, takes over.
        auto *check = new QTimer(&app);
        QObject::connect(check, &QTimer::timeout, &app, [] {
            if (DefaultFileManager::serviceFileIsOurs())
                return;
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (widget->isVisible())
                    return;
            QApplication::quit();
        });
        check->start(10000);
        return app.exec();
    }
#else
    Q_UNUSED(service)
    Q_UNUSED(portal)
#endif

    // Folders open (the first here, the others in tabs); files are shown
    // selected in their folder.
    QStringList folders;
    QStringList items;
    sortLocations(parser.positionalArguments(), &folders, &items);
    MainWindow window;
    if (!folders.isEmpty()) {
        for (const QString &item : std::as_const(items)) // files' folders: more tabs
            if (!folders.contains(QFileInfo(item).absolutePath()))
                folders << QFileInfo(item).absolutePath();
        window.openFolders(folders);
    } else if (!items.isEmpty()) {
        window.showItems(items);
    } else {
        window.navigateTo(QDir::homePath());
    }
    window.show();
    return app.exec();
}
