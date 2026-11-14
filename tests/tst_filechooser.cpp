// Tests for the Open/Save dialog backend: ChooserRequest (decoding the
// portal's options, filters, results) and FileChooserPortal on D-Bus. The bus
// tests run only on a private bus (ctest starts it under dbus-run-session).

#include "FileChooserPortal.h"
#include "FileChooserRequest.h"
#include "PortalConfig.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QSignalSpy>
#include <QtTest>

// The shapes xdg-desktop-portal sends, declared here on their own, the way
// another program would: only the D-Bus signatures have to agree.
struct WireRule
{
    uint type;
    QString value;
};
struct WireFilter
{
    QString name;
    QList<WireRule> rules;
};
struct WirePair
{
    QString a, b;
};
struct WireChoice
{
    QString id, label;
    QList<WirePair> options;
    QString initial;
};
Q_DECLARE_METATYPE(WireRule)
Q_DECLARE_METATYPE(WireFilter)
Q_DECLARE_METATYPE(WirePair)
Q_DECLARE_METATYPE(WireChoice)

QDBusArgument &operator<<(QDBusArgument &arg, const WireRule &r)
{
    arg.beginStructure();
    arg << r.type << r.value;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, WireRule &r)
{
    arg.beginStructure();
    arg >> r.type >> r.value;
    arg.endStructure();
    return arg;
}
QDBusArgument &operator<<(QDBusArgument &arg, const WireFilter &f)
{
    arg.beginStructure();
    arg << f.name << f.rules;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, WireFilter &f)
{
    arg.beginStructure();
    arg >> f.name >> f.rules;
    arg.endStructure();
    return arg;
}
QDBusArgument &operator<<(QDBusArgument &arg, const WirePair &p)
{
    arg.beginStructure();
    arg << p.a << p.b;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, WirePair &p)
{
    arg.beginStructure();
    arg >> p.a >> p.b;
    arg.endStructure();
    return arg;
}
QDBusArgument &operator<<(QDBusArgument &arg, const WireChoice &c)
{
    arg.beginStructure();
    arg << c.id << c.label << c.options << c.initial;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, WireChoice &c)
{
    arg.beginStructure();
    arg >> c.id >> c.label >> c.options >> c.initial;
    arg.endStructure();
    return arg;
}

namespace {

using Rule = ChooserRequest::Rule;
using Filter = ChooserRequest::Filter;

const char *Portal = "org.freedesktop.impl.portal.desktop.argonaut";

WireFilter images()
{
    return {"Images", {{0, "*.png"}, {1, "image/jpeg"}}};
}

} // namespace

class TestFileChooser : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void mnemonics();
    void filterMatching();
    void saveFilesPaths();
    void decodesOptionsOverTheBus();
    void closeCancels();
    void saveFileDefaults();
    void portalConfig(); // last: it changes XDG_* for the process

private:
    QDBusConnection bus() const { return QDBusConnection::sessionBus(); }
    bool privateBus() const { return qEnvironmentVariable("ARGONAUT_PRIVATE_BUS") == QLatin1String("1"); }
    // Calls the portal like xdg-desktop-portal does; returns the pending call.
    QDBusPendingCall call(const QString &method, const QString &handle, const QString &title,
                          const QVariantMap &options);
};

void TestFileChooser::initTestCase()
{
    qDBusRegisterMetaType<WireRule>();
    qDBusRegisterMetaType<QList<WireRule>>();
    qDBusRegisterMetaType<WireFilter>();
    qDBusRegisterMetaType<QList<WireFilter>>();
    qDBusRegisterMetaType<WirePair>();
    qDBusRegisterMetaType<QList<WirePair>>();
    qDBusRegisterMetaType<WireChoice>();
    qDBusRegisterMetaType<QList<WireChoice>>();
    if (privateBus()) {
        QVERIFY(bus().isConnected());
        const QStringList names = bus().interface()->registeredServiceNames().value();
        QVERIFY2(!names.contains("org.gnome.Shell") && !names.contains("org.freedesktop.Notifications"),
                 "this looks like the desktop's bus, not a private one");
    }
}

void TestFileChooser::mnemonics()
{
    QCOMPARE(ChooserRequest::withoutMnemonic("_Save"), QStringLiteral("Save"));
    QCOMPARE(ChooserRequest::withoutMnemonic("Save __As"), QStringLiteral("Save _As"));
    QCOMPARE(ChooserRequest::withoutMnemonic("Open"), QStringLiteral("Open"));
}

void TestFileChooser::filterMatching()
{
    Filter f{"Images", {Rule{false, "*.png"}, Rule{true, "image/jpeg"}}};
    QVERIFY(f.matches("a.png", "image/png"));
    QVERIFY(f.matches("SHOUT.PNG", "image/png")); // globs ignore case
    QVERIFY(f.matches("photo.jpg", "image/jpeg"));
    QVERIFY(!f.matches("notes.txt", "text/plain"));
    QVERIFY(!f.matches("a.png.txt", "text/plain")); // anchored

    // Mime rules accept inheriting types and "group/*".
    Filter text{"Text", {Rule{true, "text/plain"}}};
    QVERIFY(text.matches("main.cpp", "text/x-c++src"));
    QVERIFY(!text.matches("a.png", "image/png"));
    Filter anyImage{"Any image", {Rule{true, "image/*"}}};
    QVERIFY(anyImage.matches("a.webp", "image/webp"));
    QVERIFY(!anyImage.matches("a.mp3", "audio/mpeg"));

    const Filter all{"All", {Rule{false, "*"}}};
    const Filter empty{"Empty", {}};
    QVERIFY(all.matchesAll());
    QVERIFY(empty.matchesAll());
    QVERIFY(!f.matchesAll());
}

void TestFileChooser::saveFilesPaths()
{
    ChooserRequest request;
    request.mode = ChooserRequest::Mode::SaveFiles;
    request.files = {"a.txt", "b.png"};
    QCOMPARE(request.saveFilesPaths("/tmp/out"), QStringList({"/tmp/out/a.txt", "/tmp/out/b.png"}));
}

QDBusPendingCall TestFileChooser::call(const QString &method, const QString &handle, const QString &title,
                                       const QVariantMap &options)
{
    // A separate connection, like xdg-desktop-portal; asynchronous, since the
    // portal answers on this same thread.
    QDBusConnection client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "portal-frontend");
    QDBusMessage message = QDBusMessage::createMethodCall(
        Portal, FileChooserPortal::ObjectPath, "org.freedesktop.impl.portal.FileChooser", method);
    message << QVariant::fromValue(QDBusObjectPath(handle)) << QStringLiteral("org.example.App")
            << QStringLiteral("x11:1a00004") << title << options;
    return client.asyncCall(message);
}

void TestFileChooser::decodesOptionsOverTheBus()
{
    if (!privateBus())
        QSKIP("runs only on a private bus (ctest uses dbus-run-session)");
    FileChooserPortal portal;
    QVERIFY(portal.registerOn(bus()));
    QSignalSpy requested(&portal, &FileChooserPortal::requested);

    QVariantMap options;
    options["accept_label"] = "_Open";
    options["multiple"] = true;
    options["filters"] = QVariant::fromValue(QList<WireFilter>{images(), {"All", {{0, "*"}}}});
    options["current_filter"] = QVariant::fromValue(images());
    options["choices"] = QVariant::fromValue(QList<WireChoice>{
        {"encoding", "Encoding", {{"utf8", "UTF-8"}, {"latin1", "Latin-1"}}, "latin1"},
        {"strip", "Remove metadata", {}, "true"}});
    options["current_folder"] = QByteArray("/tmp/My Folder\0", 15);

    QDBusPendingCall pending = call("OpenFile", "/org/freedesktop/portal/desktop/request/1_9/a", "Open Photos",
                                    options);
    QTRY_COMPARE(requested.count(), 1);
    const int id = requested.first().at(0).toInt();
    const ChooserRequest request = requested.first().at(1).value<ChooserRequest>();
    QCOMPARE(int(request.mode), int(ChooserRequest::Mode::Open));
    QCOMPARE(request.title, QStringLiteral("Open Photos"));
    QCOMPARE(request.appId, QStringLiteral("org.example.App"));
    QCOMPARE(request.parentWindow, QStringLiteral("x11:1a00004"));
    QCOMPARE(request.acceptLabel, QStringLiteral("Open"));
    QVERIFY(request.multiple);
    QVERIFY(!request.directory);
    QCOMPARE(request.filters.size(), 2);
    QCOMPARE(request.filters[0].name, QStringLiteral("Images"));
    QCOMPARE(request.filters[0].rules.size(), 2);
    QVERIFY(request.filters[0].rules[1].mime);
    QCOMPARE(request.currentFilter, 0);
    QCOMPARE(request.choices.size(), 2);
    QCOMPARE(request.choices[0].options.size(), 2);
    QCOMPARE(request.choices[0].initial, QStringLiteral("latin1"));
    QVERIFY(request.choices[1].options.isEmpty()); // a checkbox
    QCOMPARE(request.currentFolder, QStringLiteral("/tmp/My Folder"));
    QCOMPARE(portal.pendingCount(), 1);

    // The answer, as the window sends it.
    portal.finish(id, FileChooserPortal::Success,
                  request.results({"/tmp/My Folder/a b.png"}, {{"encoding", "utf8"}, {"strip", "false"}}, 0));
    QDBusPendingCallWatcher watcher(pending);
    QTRY_VERIFY(watcher.isFinished());
    QVERIFY2(!watcher.isError(), qPrintable(watcher.error().message()));
    const QList<QVariant> reply = watcher.reply().arguments();
    QCOMPARE(reply.size(), 2);
    QCOMPARE(reply[0].toUInt(), 0u);
    const QVariantMap results = qdbus_cast<QVariantMap>(reply[1]);
    QCOMPARE(results.value("uris").toStringList(), QStringList({"file:///tmp/My%20Folder/a%20b.png"}));
    QCOMPARE(results.value("writable").toBool(), false);
    const auto choices = qdbus_cast<QList<WirePair>>(results.value("choices").value<QDBusArgument>());
    QCOMPARE(choices.size(), 2);
    QCOMPARE(choices[0].a, QStringLiteral("encoding"));
    QCOMPARE(choices[0].b, QStringLiteral("utf8"));
    const auto filter = qdbus_cast<WireFilter>(results.value("current_filter").value<QDBusArgument>());
    QCOMPARE(filter.name, QStringLiteral("Images"));
    QCOMPARE(filter.rules.size(), 2);
    QCOMPARE(portal.pendingCount(), 0);
    QDBusConnection::disconnectFromBus("portal-frontend");
}

void TestFileChooser::closeCancels()
{
    if (!privateBus())
        QSKIP("runs only on a private bus (ctest uses dbus-run-session)");
    FileChooserPortal portal;
    QVERIFY(portal.registerOn(bus()));
    QSignalSpy requested(&portal, &FileChooserPortal::requested);
    QSignalSpy closed(&portal, &FileChooserPortal::closeRequested);

    const QString handle = "/org/freedesktop/portal/desktop/request/1_9/b";
    QDBusPendingCall pending = call("SaveFile", handle, "Save", {});
    QTRY_COMPARE(requested.count(), 1);

    // The app gave up: xdg-desktop-portal calls Request.Close on the handle.
    QDBusConnection client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "portal-frontend");
    client.asyncCall(QDBusMessage::createMethodCall(Portal, handle, "org.freedesktop.impl.portal.Request", "Close"));
    QTRY_COMPARE(closed.count(), 1);
    QCOMPARE(closed.first().at(0).toInt(), requested.first().at(0).toInt());
    QDBusPendingCallWatcher watcher(pending);
    QTRY_VERIFY(watcher.isFinished());
    QCOMPARE(watcher.reply().arguments().value(0).toUInt(), 1u); // cancelled
    QCOMPARE(portal.pendingCount(), 0);
    QDBusConnection::disconnectFromBus("portal-frontend");
}

void TestFileChooser::saveFileDefaults()
{
    if (!privateBus())
        QSKIP("runs only on a private bus (ctest uses dbus-run-session)");
    FileChooserPortal portal;
    QVERIFY(portal.registerOn(bus()));
    QSignalSpy requested(&portal, &FileChooserPortal::requested);
    QVariantMap options;
    options["current_file"] = QByteArray("/home/u/Documents/report.odt\0", 29);
    options["multiple"] = true; // ignored when saving
    QDBusPendingCall pending = call("SaveFile", "/org/freedesktop/portal/desktop/request/1_9/c", "Save As",
                                    options);
    QTRY_COMPARE(requested.count(), 1);
    const ChooserRequest request = requested.first().at(1).value<ChooserRequest>();
    QCOMPARE(int(request.mode), int(ChooserRequest::Mode::Save));
    QCOMPARE(request.currentName, QStringLiteral("report.odt"));
    QCOMPARE(request.currentFolder, QStringLiteral("/home/u/Documents"));
    QVERIFY(!request.multiple);
    QCOMPARE(request.currentFilter, -1);
    const QVariantMap results = request.results({"/home/u/Documents/report.odt"}, {}, -1);
    QCOMPARE(results.value("writable").toBool(), true);
    QVERIFY(!results.contains("current_filter"));
    portal.finish(requested.first().at(0).toInt(), FileChooserPortal::Success, results);
    QDBusPendingCallWatcher watcher(pending);
    QTRY_VERIFY(watcher.isFinished());
    QVERIFY(!watcher.isError());
    QDBusConnection::disconnectFromBus("portal-frontend");
}

void TestFileChooser::portalConfig()
{
    // A sandbox for every place the portal reads its settings from: the real
    // ~/.config/xdg-desktop-portal is never touched.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(root + "/config"));
    qputenv("XDG_CONFIG_DIRS", QFile::encodeName(root + "/etc"));
    qputenv("XDG_DATA_DIRS", QFile::encodeName(root + "/share"));
    qputenv("XDG_CURRENT_DESKTOP", "ubuntu:GNOME");
    qputenv("XDG_DESKTOP_PORTAL_DIR", QFile::encodeName(root + "/portals"));
    const QString log = root + "/systemctl.log";
    const QString fake = root + "/fake-systemctl";
    {
        QFile script(fake);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\necho \"$@\" >> '" + QFile::encodeName(log) + "'\n");
    }
    QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    qputenv("ARGONAUT_SYSTEMCTL_PROGRAM", QFile::encodeName(fake));

    auto write = [](const QString &path, const QByteArray &text) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
    };
    auto read = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    };

    // Ubuntu's system file, as shipped.
    QVERIFY(write(root + "/share/xdg-desktop-portal/gnome-portals.conf",
                  "[preferred]\ndefault=gnome;gtk;\norg.freedesktop.impl.portal.Secret=gnome-keyring;\n"));
    const QString ours = root + "/config/xdg-desktop-portal/ubuntu-portals.conf";
    QCOMPARE(PortalConfig::configPath(), ours);

    // Not installed (no argonaut.portal): refuse, write nothing.
    QString error;
    QVERIFY(!PortalConfig::isInstalled());
    QVERIFY(!PortalConfig::enable(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFile::exists(ours));

    QVERIFY(write(root + "/portals/argonaut.portal", "[portal]\nDBusName=org.freedesktop.impl.portal.desktop.argonaut\n"));
    QVERIFY(PortalConfig::isInstalled());
    QVERIFY(!PortalConfig::isEnabled());
    QVERIFY2(PortalConfig::enable(&error), qPrintable(error));
    QVERIFY(PortalConfig::isEnabled());
    const QString text = read(ours);
    QVERIFY(text.contains("default=gnome;gtk;")); // the other choices stay
    QVERIFY(text.contains("org.freedesktop.impl.portal.Secret=gnome-keyring;"));
    QVERIFY(text.contains("org.freedesktop.impl.portal.FileChooser=argonaut;"));
    QCOMPARE(text.count("FileChooser="), 1);

    // Enabling again keeps one line, not two.
    QVERIFY(PortalConfig::enable(&error));
    QCOMPARE(read(ours).count("FileChooser="), 1);

    PortalConfig::restartPortal();
    QTRY_VERIFY(read(log).contains("--user try-restart xdg-desktop-portal.service"));

    QVERIFY(PortalConfig::disable(&error));
    QVERIFY(!QFile::exists(ours));
    QVERIFY(!PortalConfig::isEnabled());

    // A file the user wrote: never changed, never removed.
    QVERIFY(write(ours, "[preferred]\ndefault=kde;\n"));
    QVERIFY(!PortalConfig::enable(&error));
    QVERIFY(error.contains(ours));
    QVERIFY(PortalConfig::disable(&error));
    QCOMPARE(read(ours), QStringLiteral("[preferred]\ndefault=kde;\n"));
}

QTEST_GUILESS_MAIN(TestFileChooser)
#include "tst_filechooser.moc"
