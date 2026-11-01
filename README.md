# Eudaimonia

An integrated development environment in C++ and Qt 6. Editing, building,
language servers and debugging come from Qt plugins: shared ones from
[siplasplas-plugins](https://github.com/siplasplas/siplasplas-plugins), and
plugins of the IDE itself.

Work in progress: this first version shows a project directory, edits its
files in tabs, and builds and runs CMake projects in a terminal window.

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
- **Building and running** a CMake project (one with `CMakeLists.txt`): when
  the project opens, it is configured in the background into
  `<project>/build` (`cmake -S <project> -B <project>/build`, with no options
  of its own: the project's defaults); a failure shows CMake's output. The
  list on the toolbar holds the project's executable targets; the one chosen
  is remembered for the project. **Build** (hammer, Ctrl+F9) saves the
  modified files and builds the chosen target (`cmake --build build
  --parallel <N> --target <name>`, N being the number of logical processors,
  unless `CMAKE_BUILD_PARALLEL_LEVEL` is set; everything when there is no
  target), **Run** (Shift+F10) builds it and
  then starts the program in its build directory. Both run in a terminal
  window — gnome-terminal when installed, else the desktop's (konsole,
  xfce4-terminal, mate-terminal, qterminal, xterm); the setting
  `run/terminal` names another — which stays open with the exit status until
  Enter is pressed. **Build → Configure Project** configures again; the
  targets are read again whenever the window becomes active (a build
  reconfigures by itself after `CMakeLists.txt` changed).
- **Build → Build Tool** chooses what builds a new `build/`: CMake's default
  (make, or `CMAKE_GENERATOR` from the environment), **Ninja** or **Unix
  Makefiles** (those installed). A `build/` configured already keeps its
  tool (the status bar says when it differs from the choice); choosing
  another asks whether to configure it afresh (`cmake --fresh`, CMake 3.24 or
  later), after which everything is built again. The menu shows the tool
  `build/` uses.
- **Link One at a Time** (in the same menu, on by default) lets compiling use
  every processor while links, which take much memory, run one at a time —
  with Ninja only (a job pool: `CMAKE_JOB_POOLS`, `CMAKE_JOB_POOL_LINK`);
  make's `-j` counts every job alike. Turning it off removes only that pool.
- The window's size, position and the width of the tree are restored at the
  next start, from `~/.config/eudahub/eudaimonia.conf`.
- **Help → About** names the commit built and its time (with `+changes`
  when built with changes not committed yet), and the components built in:
  their tag or installed version, commit and its time (qt-extra,
  siplasplas-plugins, qcodeedit, cpg), so that it is plain which versions a
  build uses. `eudaimonia --version` gives the commit too.

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

Eudaimonia's own plugin interfaces are headers in `api/` (CMake target
`eudaimonia::plugin-api`), with IIDs `io.github.eudahub.eudaimonia.<Interface>/<n>`;
its plugins are in `plugins/`:

- `eudaimonia-cmake` (`eudaimonia::BuildSystem`): CMake projects — the
  command configuring a project (`cmake -S <project> -B <build>`, with only
  the generator and the link pool the IDE asks for) and building it (`cmake --build <build> --parallel
  <logical processors>`, or one target), and the project's targets with their type and the program each
  executable builds, read from CMake's
  [file API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html)
  after configuring (CMake 3.14 or later), not by parsing `CMakeLists.txt`.

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

Installing puts `bin/eudaimonia`, the editor plugin in
`lib/eudaimonia/plugins` (with its encoding detection data), the launcher
`share/applications/eudaimonia.desktop` and the icon into the prefix
(`/usr/local` by default):

```sh
sudo cmake --install build
```

The fast offline tests (Qt Test, `qt6-base-dev`) are built by default
(`-DEUDAIMONIA_BUILD_TESTS=OFF` leaves them out); the CMake plugin's test
configures and builds a small project, so it needs cmake and a C++ compiler:

```sh
ctest --test-dir build --output-on-failure
```

## License

Eudaimonia is licensed under the GNU General Public License, version 3 only
(GPL-3.0-only); see [LICENSE](LICENSE). It uses Qt under the LGPL-3.0,
qt-extra under the LGPL-3.0, and loads the editor plugin of
siplasplas-plugins (LGPL-3.0) with qcodeedit (LGPL-3.0-or-later). The icons
have their own licenses and authors, listed in
[resources/ATTRIBUTIONS.md](resources/ATTRIBUTIONS.md). As section 14 of the GPL-3.0 allows, its author,
Andrzej Borucki, or a person the author names publicly, is the proxy who may
accept a later version of the GPL for Eudaimonia;
[CONTRIBUTING.md](CONTRIBUTING.md) describes this and how contributions are
licensed.
