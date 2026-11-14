cmake_minimum_required(VERSION 3.16)

project(argonaut VERSION 0.4.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest) # adds the BUILD_TESTING option (ON by default)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Xml Network)
if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup()

# Non-GUI core shared by the app and the tests: the folder model and the
# file operations engine (copy/move/trash/undo, clipboard).
qt_add_library(argonaut_core STATIC
    src/DirectoryModel.h
    src/DirectoryModel.cpp
    src/DiskUsageModel.cpp
    src/Preview.h
    src/Preview.cpp
    src/DefaultFileManager.h
    src/DefaultFileManager.cpp
    src/RecentFiles.h
    src/RecentFiles.cpp
    src/Starred.h
    src/Starred.cpp
    src/Thumbnailer.h
    src/Thumbnailer.cpp
    src/DiskUsageModel.h
    src/Format.h
    src/ContentType.h
    src/ContentType.cpp
    src/Trash.h
    src/Trash.cpp
    src/Bookmarks.h
    src/Bookmarks.cpp
    src/OpenWith.h
    src/OpenWith.cpp
    src/FileJob.h
    src/FileJob.cpp
    src/FileOperations.h
    src/FileOperations.cpp
    src/Naming.h
    src/Naming.cpp
    src/Access.h
    src/Access.cpp
    src/GitStatus.h
    src/GitStatus.cpp
    src/ContentSearch.h
    src/ContentSearch.cpp
    src/Remote.h
    src/Remote.cpp
    src/KeyStore.h
    src/KeyStore.cpp
    src/Claude.h
    src/Claude.cpp
    src/Organizer.h
    src/Organizer.cpp
    src/Sandbox.h
    src/Sandbox.cpp
    src/MediaInfo.h
    src/MediaInfo.cpp
)
target_include_directories(argonaut_core PUBLIC src)
target_link_libraries(argonaut_core PUBLIC Qt6::Widgets Qt6::Xml Qt6::Network)

# "Show in folder" for other apps (org.freedesktop.FileManager1), Linux only.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.4 REQUIRED COMPONENTS DBus)
    target_sources(argonaut_core PRIVATE
        src/FileManagerService.h src/FileManagerService.cpp
        src/FileChooserRequest.h src/FileChooserRequest.cpp
        src/FileChooserPortal.h src/FileChooserPortal.cpp
        src/PortalConfig.h src/PortalConfig.cpp)
    target_link_libraries(argonaut_core PUBLIC Qt6::DBus)
    target_compile_definitions(argonaut_core PUBLIC ARGONAUT_DBUS=1)
endif()

# Compress / Extract: needs libarchive (optional, like the terminal).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBARCHIVE IMPORTED_TARGET libarchive)
endif()
if(LIBARCHIVE_FOUND)
    target_sources(argonaut_core PRIVATE src/FileJobArchive.cpp)
    target_compile_definitions(argonaut_core PUBLIC ARGONAUT_ARCHIVES=1)
    target_link_libraries(argonaut_core PUBLIC PkgConfig::LIBARCHIVE)
    message(STATUS "Compress / Extract: enabled (libarchive ${LIBARCHIVE_VERSION})")
else()
    message(STATUS "Compress / Extract: disabled (install libarchive-dev to enable)")
endif()

qt_add_executable(argonaut
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/Theme.h
    src/Theme.cpp
    src/HeaderBar.h
    src/HeaderBar.cpp
    src/PathBar.h
    src/PathBar.cpp
    src/PreferencesDialog.h
    src/PreferencesDialog.cpp
    src/FileFilterProxy.h
    src/FileFilterProxy.cpp
    src/FileItemDelegate.h
    src/FileItemDelegate.cpp
    src/OperationsPanel.h
    src/OperationsPanel.cpp
    src/Toast.h
    src/Toast.cpp
    src/ConflictDialog.h
    src/ConflictDialog.cpp
    src/OpenWithDialog.h
    src/OpenWithDialog.cpp
    src/PropertiesDialog.h
    src/CompressDialog.cpp
    src/QuickLook.cpp
    src/QuickLook.h
    src/UsageDelegate.cpp
    src/UsageDelegate.h
    src/CompressDialog.h
    src/PropertiesDialog.cpp
    src/BatchRenameDialog.h
    src/BatchRenameDialog.cpp
    src/HelpDialog.h
    src/HelpDialog.cpp
    src/AboutDialog.h
    src/AboutDialog.cpp
    src/CommandPalette.h
    src/CommandPalette.cpp
    src/ConnectDialog.h
    src/ConnectDialog.cpp
    src/OrganizerDialog.h
    src/OrganizerDialog.cpp
)

target_link_libraries(argonaut PRIVATE argonaut_core Qt6::Widgets)
# Open/Save windows for other apps (xdg-desktop-portal FileChooser backend), Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(argonaut PRIVATE src/ChooserBar.h src/ChooserBar.cpp)
    # WM_TRANSIENT_FOR on those windows (X11): one xcb call Qt has no API for.
    if(PkgConfig_FOUND)
        pkg_check_modules(XCB IMPORTED_TARGET xcb)
    endif()
    if(XCB_FOUND)
        target_compile_definitions(argonaut PRIVATE ARGONAUT_XCB=1)
        target_link_libraries(argonaut PRIVATE PkgConfig::XCB)
    endif()
endif()
# One version number for the app, the About box and the installer.
target_compile_definitions(argonaut PRIVATE ARGONAUT_VERSION="${PROJECT_VERSION}")
if(LIBARCHIVE_FOUND)
    target_compile_definitions(argonaut PRIVATE ARGONAUT_LIBARCHIVE_VERSION="${LIBARCHIVE_VERSION}")
endif()

# Embedded terminal: needs libvterm and a Unix pseudo-terminal (forkpty).
# Without them the app builds normally, just without the terminal panel.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(VTERM IMPORTED_TARGET vterm)
endif()
if(VTERM_FOUND AND UNIX)
    target_sources(argonaut PRIVATE src/TerminalWidget.h src/TerminalWidget.cpp)
    target_compile_definitions(argonaut PRIVATE ARGONAUT_TERMINAL=1 ARGONAUT_VTERM_VERSION="${VTERM_VERSION}")
    target_link_libraries(argonaut PRIVATE PkgConfig::VTERM)
    find_library(UTIL_LIBRARY util)
    if(UTIL_LIBRARY)
        target_link_libraries(argonaut PRIVATE ${UTIL_LIBRARY})
    endif()
    message(STATUS "Embedded terminal: enabled (libvterm ${VTERM_VERSION})")
else()
    message(STATUS "Embedded terminal: disabled (install libvterm-dev to enable)")
endif()

# Windows: no console window. macOS: build a .app bundle.
set_target_properties(argonaut PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# ------------------------------------------------------------- install ---
# `cmake --install build/release` (the release preset installs into ~/.local):
# the program, its menu entry and icon. Uninstall: `cmake --build build/release
# --target uninstall` (run `argonaut --restore-nautilus` first if it's the default).
include(GNUInstallDirs)
install(TARGETS argonaut RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
    # Absolute path: D-Bus and the desktop start it without the user's PATH.
    set(ARGONAUT_EXE "${CMAKE_INSTALL_FULL_BINDIR}/argonaut")
    configure_file(data/argonaut.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/argonaut.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/argonaut.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES data/argonaut.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    # Open/Save windows for other apps. No UseIn in argonaut.portal: the
    # portal uses it only where the user chose it (Preferences writes their
    # portals.conf). xdg-desktop-portal reads only /usr/share, so this works
    # from the package, not from a ~/.local install.
    configure_file(data/org.freedesktop.impl.portal.desktop.argonaut.service.in
                   ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.argonaut.service @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.argonaut.service
            DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
    install(FILES data/argonaut.portal DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal/portals)
    # Let menus and "Open With" see the new entry right away.
    install(CODE [[
        find_program(UPDATE_DESKTOP_DATABASE update-desktop-database)
        if(UPDATE_DESKTOP_DATABASE AND "$ENV{DESTDIR}" STREQUAL "")
            execute_process(COMMAND ${UPDATE_DESKTOP_DATABASE} "${CMAKE_INSTALL_PREFIX}/share/applications")
        endif()
    ]])
endif()
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

# ------------------------------------------------------------- package ---
# A .deb for Ubuntu and Debian: `cmake --preset package && cmake --build
# build/package && cpack --config build/package/CPackConfig.cmake`.
# Library dependencies are worked out by dpkg-shlibdeps; the tools Argonaut
# uses when they're there are Recommends.
set(CPACK_PACKAGE_NAME "argonaut-files") # the plain name is taken by an unrelated tool
set(CPACK_PACKAGE_VENDOR "johngiannop")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/jgiannop/argonaut")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A fast file manager inspired by GNOME Files")
set(CPACK_PACKAGE_DESCRIPTION "Argonaut is a fast file manager written in C++ and Qt 6, with tabs, split view, \
Quick Look, disk usage analysis, archives, an embedded terminal, real file-type checks and \
undo for every file operation. It shares bookmarks, recent files, thumbnails and the Trash \
with GNOME Files.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
# Into the build folder, not wherever cpack is run from.
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "johngiannop")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/jgiannop/argonaut")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
    "qt6-wayland, gvfs-backends, gvfs-fuse, bubblewrap, poppler-utils, libimage-exiftool-perl, tracker, xdg-utils, git")
include(CPack)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -DMANIFEST=${CMAKE_CURRENT_BINARY_DIR}/install_manifest.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Uninstall.cmake
    COMMENT "Removing the installed files")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
