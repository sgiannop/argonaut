# Argonaut

A fast file manager built with C++ and Qt 6, inspired by GNOME Files (Nautilus). It is named after the Argonauts, who sailed with Jason on the *Argo* into the unknown, and after the *argonauta*, the small octopus whose shell is called the paper nautilus. It was called Kaftilous until version 0.2 and Kosmos in 0.3; settings, stars and the API key carry over by themselves.

> **Status:** preview. Used daily on Ubuntu 24.04 (X11). The code is written to be cross-platform (Linux, Windows, macOS), but only Linux has been tested so far, and the interface is in English only. [Bug reports and feedback](https://github.com/jgiannop/argonaut/issues) are welcome.

## Features

**Browsing**
- Grid view with thumbnails, and a list view with sortable **Name / Size / Type / Modified** columns, plus optional **Owner / Group / Permissions** (right-click the header)
- **Expandable folders** in the list view (Preferences → List View): open folders in place with ▸, → and ←
- Thumbnails for photos, **PDFs, videos, fonts, comics and more**, using the thumbnailers already installed on your system (the same ones Nautilus uses) and sharing their cache. They run **sandboxed** (no network, nothing visible but the one file), two at a time at low priority, only for files on screen, and by default only on the computer's own disks (Preferences)
- Folders first, in natural order ("file2" before "file10"); sort by name, size, type or date
- Sidebar with your folders, drives and the Trash; a breadcrumb path bar (Ctrl+L to type a path)
- **Recent**: what you opened lately, in Argonaut or any other app (the list GNOME apps share), newest first. Respects GNOME's "remember recent files" privacy switch. Right-click → Remove from Recent
- **Starred**: star files and folders (right-click → Star); they get a ★ and their own place in the sidebar. Stars follow items you rename or move
- Search (Ctrl+F) in the folder **and all its sub-folders**, with results as you type. Several words narrow it down, and case and accents don't matter ("θαλασσα" finds "Θάλασσα.jpg", "cafe" finds "Café"). Right-click → Open Item Location
- **Search inside files** ("Inside files" button): text files are read as the search walks, and documents such as PDFs come from GNOME's search index; matches show the text around them
- **Search filters** by type (documents, images, audio, video, archives, text and code, folders) and by date, also without typing a name
- Show hidden files (Ctrl+H)
- **Quick Look** (Space): a preview of the selected file without opening an app: photos (animated GIFs play), PDFs, videos, fonts, text and code, the contents of archives (nothing is extracted), folders. Arrow keys go to the next file, Enter opens it, Space or Esc closes. Programs disguised as documents are never previewed. Previews are made in the same sandbox as thumbnails
- **Split view** (F3): two folders side by side, each with its own history and view. F5 copies the selection to the other side, F6 moves it, or drag between them. Delete, rename and every other shortcut act on the side you're in
- Zoom (Ctrl+plus / Ctrl+minus / Ctrl+0, or Ctrl+mouse wheel): grid icons from 32 to 256 px, list icons from 16 to 48 px, remembered separately for each
- **Analyze view** (Ctrl+3, or the pie button): how much disk space every folder and sub-folder uses, as a tree with usage bars, biggest first. Sizes match `du` exactly (space on disk, hard links counted once; symlinks and other drives aren't followed). Folders appear while it measures; opening a folder inside what's been measured is instant. **Show Files When Analyzing** (View menu or right-click) also lists each folder's biggest files. Copying, moving, trashing, extracting and Undo update the sizes at once, without measuring everything again. Right-click to open, trash or scan again
- Live updates when files change on disk
- **Connect to Server** (sidebar, Go menu, or an address in the path bar): `sftp://`, `smb://`, `ftp://`, WebDAV and more through GVfs, with a login dialog; connected servers and phones appear in the sidebar with a disconnect button
- **Git repositories**: M / A / U / C badges on changed, added, new and conflicting files, a dot on folders with changes inside, and the branch in the path bar. Repositories whose own settings would make git run programs are never checked
- Fast with big folders: 20,000 files load in about 0.15 s (one system call per file)

**Files**
- Copy, cut and paste, which also work with other file managers' clipboards
- Drag and drop: same drive moves, another drive copies, Ctrl copies, Shift moves
- Progress for long operations, with time left and cancel
- Undo (Ctrl+Z) and Redo (Ctrl+Shift+Z or Ctrl+Y) for copy, move, trash, rename, new items, extract and compress. Redo brings back the very same items (for example, from the Trash), it never copies again
- Never overwrites: name clashes become "name (copy)"
- New Folder, New File, Rename (F2), and **New Document** from your Templates folder
- **Batch rename** (F2 on several items): a template with the original name and numbers, or find and replace; extensions are kept, a live preview shows clashes, one Ctrl+Z undoes it
- **Select Items Matching** (Ctrl+S, e.g. `*.jpg`) and **Invert Selection** (Ctrl+Shift+I)
- Hold a dragged item over a folder for a moment and it opens, so you can drop deeper
- USB sticks: Argonaut says "done" only after the data is really written to the drive

**Archives** (with libarchive)
- Right-click → **Extract Here** / **Extract to…** for zip, 7z, tar.gz/xz/bz2/zst, rar, iso, deb, plain .gz/.xz and more
- Smart like Nautilus: one item inside comes out as is; several go into a folder named after the archive
- Safe: never overwrites, and entries that try to escape the folder (`../`, absolute paths, symlink tricks) are skipped and reported
- Uses the real type: a `.zip` that is really 7z still extracts; a program named `.zip` is never treated as an archive
- **Compress…** any selection to .zip, .tar.xz, .tar.gz or .7z
- Progress, cancel (nothing half-finished is left behind) and Undo, like copying

**Real file types: safer than trusting names**

Most file managers, Nautilus included, believe a file's extension: `report.pdf` is shown and opened as a PDF without anyone looking inside. Argonaut checks what each file really is, from its first few kilobytes, and tells you when the name lies:
- An **amber ⚠ badge** marks files whose content doesn't match their name, for example a `.pdf` that is really an HTML error page from a failed download, or a `.zip` that is really a 7z archive. Hover for details. Right-click → **Rename to “report.html”** fixes it (and can be undone).
- A **red ⚠ badge** marks **programs disguised as documents** (`invoice.pdf` that is really an executable). Opening one shows a warning; Argonaut never runs it.
- Files are **opened with the app for what they really are**, so a mislabelled file still opens correctly.
- Files with no extension get their real type and icon.
- **Properties** shows both what the name says and what the file is.
- **It doesn't slow anything down.** The folder appears instantly from the names, the check runs in the background, and the results are remembered (in `~/.cache`), so each file is read only once until it changes. On USB sticks and network drives only the files on screen are checked.

**Trash** (Linux)
- Restore to the original location (or **Restore All**), Delete Permanently (Shift+Delete), Empty Trash
- Shows items trashed on all drives, including by other apps

**Extras**
- Embedded terminal (Ctrl+`) that opens in the current folder
- Themes: Follow System, Light, Dark, Midnight Blue, Rosé Dawn, Forest, Sunset (Preferences, Ctrl+,)
- Tabs (Ctrl+T), bookmarks shared with Nautilus (Ctrl+D), eject for USB drives, Open With…, Properties (Ctrl+I), type-to-find
- **Properties → Details** for photos, music, videos and PDFs (dimensions, camera and exposure, date taken, length, tags, pages), read with exiftool inside a sandbox
- 🔒 and ↗ badges on items you can't change and on links; Cut, Trash and Rename are off where the folder can't be changed
- **Send by Email…**, **Set as Background**, and **Scripts ▸** (runs your GNOME Files scripts unchanged)
- **Command palette** (Ctrl+Shift+P): type part of any command
- A built-in **User Guide** (F1) and a **Keyboard Shortcuts** page (Ctrl+?)
- Sizes in GNOME's units (kB, MB, GB), matching Nautilus and drive labels

**Organize with Claude** (optional, needs your own Anthropic API key)

File → **Organize with Claude…** asks Claude, Anthropic's AI, to propose folders, better names and a short report for a messy folder. You review the plan as a checklist and apply what you keep; Ctrl+Z puts everything back. See [Claude and your privacy](#claude-and-your-privacy).

### Keyboard shortcuts

| Action | Shortcut |
|---|---|
| Back / Forward / Up | Alt+← / Alt+→ / Alt+↑ |
| Home folder | Alt+Home |
| Type a path | Ctrl+L |
| Search (this folder and sub-folders) | Ctrl+F |
| Grid / list / analyze disk usage | Ctrl+1 / Ctrl+2 / Ctrl+3 |
| Quick Look (preview) | Space |
| Split view / copy to other side / move to other side | F3 / F5 / F6 |
| Show hidden files | Ctrl+H |
| Copy / Cut / Paste | Ctrl+C / Ctrl+X / Ctrl+V |
| Move to Trash / Delete permanently | Delete / Shift+Delete |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y |
| Zoom in / out / normal size | Ctrl++ / Ctrl+− / Ctrl+0 (or Ctrl+wheel) |
| Rename | F2 |
| New folder / New file | Ctrl+Shift+N / Ctrl+Alt+N |
| New tab / Close tab / Reopen closed tab | Ctrl+T / Ctrl+W / Ctrl+Shift+T |
| Next / previous tab | Ctrl+Tab / Ctrl+Shift+Tab |
| Bookmark this folder | Ctrl+D |
| Properties | Ctrl+I or Alt+Enter |
| Find a file by typing | just start typing in the folder |
| Terminal | Ctrl+` |
| Preferences | Ctrl+, |

## Claude and your privacy

The Claude features are off until you add an API key in **Preferences → Claude** (it is stored in your desktop keyring, or read from `ANTHROPIC_API_KEY`). Nothing is sent until you start a task yourself, and you see the estimated cost first.

What is sent to Anthropic's API when you organize a folder: the names, sizes, dates and types of its items (up to 500), and, only if you allow it, the beginning of some files (some text, the first two pages of a PDF, a small preview of an image). Programs are never read. **Preferences → Claude** sets the default: names only, ask before reading files (the default), or read files when needed. Requests are billed to your own Anthropic account.

## Building

### Requirements

- A C++17 compiler (GCC, Clang or MSVC)
- CMake 3.21 or newer, and Ninja
- Qt 6.4 or newer (Widgets; Test for the tests)
- Optional: **libvterm** for the embedded terminal (Linux/macOS). Without it, Argonaut builds without the terminal.
- Optional: **libarchive** for Compress / Extract. Without it, those menu items are hidden.

### Linux (Ubuntu / Debian)

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev qt6-wayland libvterm-dev libarchive-dev
```

### Windows and macOS

Install Qt 6 with the [Qt Online Installer](https://www.qt.io/download-qt-installer), plus CMake and Ninja. If CMake can't find Qt, point it at your installation with `-DCMAKE_PREFIX_PATH=<path-to-Qt>/6.x.x/<compiler>`.

### Build, run, test

```bash
cmake --preset debug
cmake --build build/debug
./build/debug/argonaut [folder]

ctest --preset debug      # run the tests
```

For an optimized build, use `release` instead of `debug`.

## Installing (Linux)

### From a .deb package (Ubuntu 24.04 and newer)

Download `argonaut-files_<version>_amd64.deb` from the [releases page](https://github.com/jgiannop/argonaut/releases) and install it with:

```bash
sudo apt install ./argonaut-files_0.4.2_amd64.deb
```

apt also offers the helpers Argonaut uses when they're there (GVfs for servers, bubblewrap for its sandbox, poppler-utils and exiftool for details, Tracker for searching inside documents).

To build the package yourself:

```bash
cmake --preset package
cmake --build build/package
cpack --config build/package/CPackConfig.cmake   # build/package/argonaut-files_<version>_amd64.deb
```

### From source, for your user only

Argonaut can also install for your user only, in `~/.local`: no `sudo`, nothing outside your home folder.

```bash
cmake --preset release
cmake --build build/release
cmake --install build/release      # ~/.local/bin/argonaut, menu entry, icon
```

It then appears in the app grid as **Argonaut**. To uninstall: `cmake --build build/release --target uninstall`.

### Using it instead of Nautilus

Open **Preferences → Default File Manager → Make Argonaut the Default**, or run `argonaut --make-default`. Argonaut then opens:
- folders opened from other apps, desktop icons, Super+E and "Open containing folder" (it becomes the default app for folders);
- "Show in folder" from Firefox, Chrome and others (it answers `org.freedesktop.FileManager1` on D-Bus).

To go back: **Preferences → Use Nautilus Again**, or `argonaut --restore-nautilus`, which works from any terminal even if Argonaut itself won't start. Nautilus is never removed or changed; switching only points these two hooks at one or the other, and the previous file manager is remembered.

Notes:
- If Nautilus is already running in the background, "Show in folder" keeps going to it until it stops (`nautilus -q`, or log out and in once).
- The dock's **Files** icon still starts Nautilus: right-click it → *Unpin*, then right-click Argonaut → *Pin to Dash*.

### Open and Save windows for other apps

Apps like Firefox, Signal and Chrome ask the desktop for their **Open** and **Save** windows. **Preferences → Open and Save Windows → Use Argonaut for Open and Save** makes those Argonaut windows: your sidebar, bookmarks, search and views, the app's file types and options at the bottom, and a question before a file is replaced. They also open faster than GNOME's own (about 0.1 s here).

It works with the `.deb` package (the desktop portal only looks in `/usr/share`), and it changes only your own `~/.config/xdg-desktop-portal/` settings; the same button switches back.

## Developing in VS Code

Open the folder in VS Code and install the recommended extensions when asked (C/C++, CMake Tools, Qt). Pick the **Debug** preset, then press **F7** to build or **F5** to run with the debugger. Tests appear in the Testing panel.

## Project layout

```
src/               Application source code
tests/             Qt Test suites (run with ctest)
CMakeLists.txt     Build definition
CMakePresets.json  Debug and release build and test presets
.vscode/           VS Code build, debug and formatting settings
CLAUDE.md          Architecture notes for contributors
```

## License

Copyright © 2026 johngiannop.

Argonaut is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; see [LICENSE](LICENSE) for the full text.
