#include "PortalConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

namespace {

constexpr const char *Marker = "# Written by Argonaut (Preferences → Use Argonaut for Open and Save).";
constexpr const char *ChooserKey = "org.freedesktop.impl.portal.FileChooser";

QString tr(const char *text)
{
    return QCoreApplication::translate("PortalConfig", text);
}

// "ubuntu:GNOME" -> {"ubuntu", "gnome"}: the names portals.conf files carry.
QStringList desktops()
{
    QStringList names;
    for (const QString &name : qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'), Qt::SkipEmptyParts))
        names.append(name.toLower());
    return names;
}

QStringList splitDirs(const char *variable, const QString &fallback)
{
    QStringList dirs = qEnvironmentVariable(variable).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (dirs.isEmpty())
        dirs = fallback.split(QLatin1Char(':'));
    return dirs;
}

// The portals.conf in effect, the way xdg-desktop-portal looks for it: the
// user's config, then the system's config and data folders; in each, the
// desktop's own file first.
QString effectiveConfig()
{
    QStringList dirs{QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)};
    dirs += splitDirs("XDG_CONFIG_DIRS", QStringLiteral("/etc/xdg"));
    dirs += QStringLiteral("/etc"); // sysconfdir
    dirs += splitDirs("XDG_DATA_DIRS", QStringLiteral("/usr/local/share:/usr/share"));
    QStringList names;
    for (const QString &desktop : desktops())
        names.append(desktop + QStringLiteral("-portals.conf"));
    names.append(QStringLiteral("portals.conf"));
    for (const QString &dir : std::as_const(dirs))
        for (const QString &name : std::as_const(names)) {
            const QString path = dir + QStringLiteral("/xdg-desktop-portal/") + name;
            if (QFileInfo(path).isFile())
                return path;
        }
    return {};
}

QString readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool isOurs(const QString &path)
{
    return readAll(path).contains(QString::fromUtf8(Marker));
}

// key=value lines of the [preferred] group, without our key.
QStringList preferredLines(const QString &text)
{
    QStringList lines;
    bool inPreferred = false;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inPreferred = line == QLatin1String("[preferred]");
            continue;
        }
        if (!inPreferred || line.isEmpty() || line.startsWith(QLatin1Char('#')) || !line.contains(QLatin1Char('=')))
            continue;
        if (line.section(QLatin1Char('='), 0, 0).trimmed() == QLatin1String(ChooserKey))
            continue;
        lines.append(line);
    }
    return lines;
}

} // namespace

namespace PortalConfig {

QString portalFile()
{
    QString dir = qEnvironmentVariable("XDG_DESKTOP_PORTAL_DIR");
    if (dir.isEmpty())
        dir = QStringLiteral("/usr/share/xdg-desktop-portal/portals");
    return dir + QStringLiteral("/argonaut.portal");
}

bool isInstalled()
{
    return QFileInfo(portalFile()).isFile();
}

QString configPath()
{
    const QStringList names = desktops();
    const QString file = names.isEmpty() ? QStringLiteral("portals.conf") : names.first() + QStringLiteral("-portals.conf");
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/xdg-desktop-portal/")
           + file;
}

bool isEnabled()
{
    const QString path = configPath();
    if (!isOurs(path))
        return false;
    for (const QString &line : readAll(path).split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1String(ChooserKey) + QLatin1Char('='))
            && line.contains(QLatin1String("argonaut")))
            return true;
    return false;
}

bool enable(QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!isInstalled())
        return fail(tr("Argonaut's Open and Save windows need the installed package (argonaut-files)."));
    const QString path = configPath();
    if (QFileInfo::exists(path) && !isOurs(path))
        return fail(tr("You already have your own portal settings (%1). Add “%2=argonaut;” to its [preferred] group.")
                        .arg(path, QString::fromLatin1(ChooserKey)));

    // Keep every other choice in effect now (the keyring, screenshots…).
    const QString current = effectiveConfig();
    QStringList lines = (current.isEmpty() || current == path) ? QStringList() : preferredLines(readAll(current));
    if (current == path) // ours already: keep its lines
        lines = preferredLines(readAll(path));
    QByteArray text = QByteArray(Marker) + "\n# Undo: Preferences, or delete this file and log out.\n";
    if (!current.isEmpty() && current != path)
        text += "# Copied from " + QFile::encodeName(current) + "\n";
    text += "[preferred]\n";
    for (const QString &line : std::as_const(lines))
        text += line.toUtf8() + '\n';
    text += QByteArray(ChooserKey) + "=argonaut;\n";

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(tr("Couldn't write %1").arg(path));
    file.write(text);
    if (!file.commit())
        return fail(tr("Couldn't write %1").arg(path));
    return true;
}

bool disable(QString *error)
{
    const QString path = configPath();
    if (!QFileInfo::exists(path) || !isOurs(path))
        return true; // nothing of ours to remove
    if (!QFile::remove(path)) {
        if (error)
            *error = tr("Couldn't remove %1").arg(path);
        return false;
    }
    return true;
}

void restartPortal()
{
    QString program = qEnvironmentVariable("ARGONAUT_SYSTEMCTL_PROGRAM");
    if (program.isEmpty())
        program = QStringLiteral("systemctl");
    QProcess::startDetached(program, {QStringLiteral("--user"), QStringLiteral("try-restart"),
                                      QStringLiteral("xdg-desktop-portal.service")});
}

} // namespace PortalConfig
