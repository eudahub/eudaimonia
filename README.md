# Eudaimonia

An integrated development environment in C++ and Qt 6. Editing, building,
language servers and debugging come from Qt plugins: shared ones from
[siplasplas-plugins](https://github.com/siplasplas/siplasplas-plugins), and
plugins of the IDE itself.

Work in progress: this first version shows a project directory and opens its
files in tabs, read only. The editor plugin, building and running follow.

## Using it

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
- For now a file is shown as plain UTF-8 text, read only; files larger than
  32 MiB are not opened.
- The window's size, position and the width of the tree are restored at the
  next start, from `~/.config/eudahub/eudaimonia.conf`.

Directories and files can also be given on the command line:

```sh
eudaimonia [directory] [file ...]
```

## Building

Requires a C++20 compiler, CMake 3.21+ and Qt 6.2+ (`qt6-base-dev`), and
[qt-extra](https://github.com/siplasplas/qt-extra) 2.7 or later for the tab
widget and the file dialog. An installed qt-extra is used; otherwise CMake
fetches it.

```sh
cmake -S . -B build
cmake --build build
./build/eudaimonia
```

## License

Eudaimonia is licensed under the GNU General Public License, version 3 only
(GPL-3.0-only); see [LICENSE](LICENSE). It uses Qt under the LGPL-3.0 and
qt-extra under the LGPL-3.0. As section 14 of the GPL-3.0 allows, its author,
Andrzej Borucki, or a person the author names publicly, is the proxy who may
accept a later version of the GPL for Eudaimonia;
[CONTRIBUTING.md](CONTRIBUTING.md) describes this and how contributions are
licensed.
