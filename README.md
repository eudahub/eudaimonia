# Eudaimonia

An integrated development environment in C++ and Qt 6. Editing, building,
language servers and debugging come from Qt plugins: shared ones from
[siplasplas-plugins](https://github.com/siplasplas/siplasplas-plugins), and
plugins of the IDE itself.

Work in progress: this first version shows a project directory and edits
its files in tabs. Building and running follow.

## Using it

- A project is a directory. Until one is opened, the left side lists the
  recent projects, most recently used first: activate one (double click or
  Enter) or use **Open Project Directory...** below the list. No project is
  opened automatically at start.
- **File → Open Project Directory** (Ctrl+Shift+O) shows a directory as a
  tree on the left. Activate a file (double click or Enter) to open it in a
  tab on the right; a file already open gets its tab back.
- **File → Open File** (Ctrl+O) opens one or more files, from the project or
  from anywhere.
- **File → Recent Projects** lists the last 20 project directories, newest
  first. They are kept in `recent-projects.json` in
  `~/.local/share/eudahub/eudaimonia`.
- Tabs are an MRU tab widget: Ctrl+Tab switches to the most recently used
  tab, tabs can be pinned and moved, Ctrl+W closes the current one.
- Files are edited in the editor plugin (`spp-editor` from
  siplasplas-plugins): opened in their encoding (detected; legacy code pages
  and UTF) and saved back the same way, with their line breaks, coloured by
  the Kate syntax definitions in `~/.local/share/kate-data` (shared with other
  programs using qcodeedit; without them, only C and C++ are coloured).
  When the encoding was guessed without confidence, the status bar says so.
  Files larger than 32 MiB are not opened.
- **File → Save** (Ctrl+S), **Save As** (Ctrl+Shift+S) and **Save All**
  (Ctrl+Alt+S). A tab with unsaved changes has `*` after its name; closing it,
  or the window, asks whether to save them. Trailing spaces are removed from
  the lines changed since the last save.
- When the window becomes active, files changed on disk are reloaded; a tab
  with unsaved changes asks first. A reload is one undo step: Ctrl+Z brings
  the text before it back.
- The status bar shows the cursor's line and column and the file's encoding.
- The window's size, position and the width of the tree are restored at the
  next start, from `~/.config/eudahub/eudaimonia.conf`.

Directories and files can also be given on the command line:

```sh
eudaimonia [directory] [file ...]
```

## Plugins

Plugins are Qt plugins searched for in these directories, in this order:
the directories in `EUDAIMONIA_PLUGIN_PATH` (separated by `:`),
`lib/eudaimonia/plugins` next to `bin` when installed, and `plugins` next to
the executable in the build tree. At start only the interface ID (IID) of
each plugin file is read; a plugin is loaded when its interface is first
needed. When two files provide the same interface, the first one found is
used.

A feature whose plugin is missing reports an error that lists the searched
directories; there are no built-in replacements. **Help → Plugins** shows the
searched directories and what was found in them, and so does
`eudaimonia --list-plugins` on the command line.

## Building

Requires a C++20 compiler, CMake 3.21+ and Qt 6.2+ (`qt6-base-dev`), and
[qt-extra](https://github.com/siplasplas/qt-extra) 2.7 or later for the tab
widget and the file dialog. An installed qt-extra is used; otherwise CMake
fetches it. CMake also fetches
[siplasplas-plugins](https://github.com/siplasplas/siplasplas-plugins) and
builds its editor plugin into `build/plugins`, with
[qcodeedit](https://github.com/siplasplas/qcodeedit) and its encodings
library [cpg](https://github.com/siplasplas/cpg) built in; that needs network
access on the first configuration and ICU (`libicu-dev`).

```sh
cmake -S . -B build
cmake --build build
./build/eudaimonia
```

The fast offline tests (Qt Test, `qt6-base-dev`) are built by default
(`-DEUDAIMONIA_BUILD_TESTS=OFF` leaves them out):

```sh
ctest --test-dir build --output-on-failure
```

## License

Eudaimonia is licensed under the GNU General Public License, version 3 only
(GPL-3.0-only); see [LICENSE](LICENSE). It uses Qt under the LGPL-3.0,
qt-extra under the LGPL-3.0, and loads the editor plugin of
siplasplas-plugins (LGPL-3.0) with qcodeedit (LGPL-3.0-or-later). As section 14 of the GPL-3.0 allows, its author,
Andrzej Borucki, or a person the author names publicly, is the proxy who may
accept a later version of the GPL for Eudaimonia;
[CONTRIBUTING.md](CONTRIBUTING.md) describes this and how contributions are
licensed.
