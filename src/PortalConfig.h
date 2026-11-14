#pragma once

#include <QString>

// Choosing Argonaut for other apps' Open and Save windows (Linux). The
// desktop portal (xdg-desktop-portal ≥ 1.17) picks a backend per interface
// from portals.conf; we write the user's own copy,
//   ~/.config/xdg-desktop-portal/<desktop>-portals.conf
// with the [preferred] lines of the file in effect now, plus
//   org.freedesktop.impl.portal.FileChooser=argonaut;
// and restart the portal. Nothing is written into /usr; a file we didn't
// write is never changed or removed (Marker). The backend itself is
// `argonaut --portal` (FileChooserPortal), found by the portal through
// argonaut.portal, which only the package installs (the portal reads /usr only).
namespace PortalConfig {

QString portalFile();  // our argonaut.portal where xdg-desktop-portal looks
bool isInstalled();    // portalFile() exists
QString configPath();  // the user file we write
bool isEnabled();      // our file is there and picks Argonaut

bool enable(QString *error = nullptr);
bool disable(QString *error = nullptr);

// `systemctl --user try-restart xdg-desktop-portal.service`, so the choice
// applies now (ARGONAUT_SYSTEMCTL_PROGRAM replaces systemctl in tests).
void restartPortal();

} // namespace PortalConfig
