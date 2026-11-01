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
- **Language servers** check the code as it is typed (the language support
  plugin, below): clangd for C and C++, neocmakelsp for CMake files,
  rust-analyzer for Rust, pylsp for Python, and those of
  `language-servers.json`. When a project opens, a window lists the servers
  its files need that are not installed, with the command installing each —
  once a session for each server, since another project may need others.
  Their errors, warnings and hints are underlined in the editor (the message
  shows when the mouse rests on one) and listed in **Navigate → Problems**,
  where a click opens the place; an edit removes the underlines until the
  server checked the new text. **Navigate → Go to Definition** (F12) opens
  the definition of the symbol at the cursor; so does a Ctrl+click on a
  link: with Ctrl held, a word under the mouse whose definition the language
  server knows looks like a link (blue, underlined, under a pointing hand).
  Words it knows nothing of — comments, plain text, files without a server —
  stay as they are, and a Ctrl+click on them is an ordinary click.
- **Navigate → Back** (Ctrl+Alt+Left) returns to where one was before, across
  files, and **Forward** (Ctrl+Alt+Right) undoes it: places are kept when
  another tab is shown (also for a file opened), before a jump (Go to
  Definition, a problem or a compiler's message clicked) and when the cursor
  moves ten lines or more at once; one a line, at most 100. A file whose tab
  was closed opens again.
- **Building and running** a CMake project (one with `CMakeLists.txt`): when
  the project opens, it is configured in the background into
  `<project>/build` (`cmake -S <project> -B <project>/build`, keeping the
  project's defaults: of its own only the build tool, the link pool and
  `compile_commands.json` for clangd); a failure shows CMake's output. The
  list on the toolbar holds the project's executable targets; the one chosen
  is remembered for the project. **Build** (hammer, Ctrl+F9) saves the
  modified files and builds the chosen target (`cmake --build build
  --parallel <N> --target <name>`, N being the number of logical processors,
  unless `CMAKE_BUILD_PARALLEL_LEVEL` is set; everything when there is no
  target), **Run** (Shift+F10) builds it and, when the build succeeded,
  starts the program in its build directory, in the **Run panel**: its
  output (errors in red), line by line as in a terminal (through `stdbuf`,
  when installed, since C's stdio would otherwise hold back the output of a
  program writing into a pipe), a line for its standard input (Enter sends
  it, **End Input** closes the input), and at the end its exit code, how
  long it ran and when it finished. **Stop** (Ctrl+F2) ends the program and
  what it started; Run while it runs stops it and starts again.
  **Build → Run in Terminal** runs programs in a terminal window instead,
  e.g. full-screen ones — gnome-terminal when installed, else the desktop's
  (konsole, xfce4-terminal, mate-terminal, qterminal, xterm); the setting
  `run/terminal` names another — which stays open with the exit status until
  Enter is pressed. **Build → Configure Project** configures again; the
  targets are read again whenever the window becomes active and after a
  build (which reconfigures by itself after `CMakeLists.txt` changed).
- The **Build panel** below the editors shows the build's output, how long
  it has been running, the memory the build takes now and at most (all its
  processes: cmake, ninja, compilers, linkers) and the memory the computer
  has left (and the swap in use, when there is swap). At the end it says how
  long the build took, when it finished and the most memory it took, and how
  many errors and warnings there were. **Stop** (Ctrl+F2) stops the whole
  build.
- **Compilers' messages** in the panel — GCC's and Clang's `file:line:column:
  error|warning|note:`, `In file included from file:line`, and the linker's
  `file:line:(...): undefined reference` — are coloured (errors red,
  warnings amber, notes muted) and underlined; a click on one opens its file
  at that line and column. Relative paths are taken from the build
  directory.
- **A build is stopped at once when memory runs short**, before the
  computer swaps — a swapping build makes the computer useless for long and
  wears the disk: when less memory is available than 5 % of it (at least
  2 GiB; 9.6 GiB of 192 GiB, 2 GiB of 20 GiB), or when the swap in use grows
  by more than 256 MiB during the build. Memory is looked at four times a
  second, and the build's whole process group is killed; the panel says why.
  Fewer links at once (below) keep a big link from getting there.
- **Build → Build Tool** chooses what builds a new `build/`: **Ninja** (the
  default when installed), **Unix Makefiles**, or CMake's own default (make,
  or `CMAKE_GENERATOR` from the environment). A `build/` configured already
  keeps its tool (the status bar says when it differs from the choice);
  choosing another asks whether to configure it afresh: the cache and the
  old generator's files go, also those of FetchContent's sub-builds
  (`_deps/*-subbuild`), while the fetched sources stay; then everything is
  built again. A `build/` left half switched (e.g. by `cmake --fresh` with
  another generator, which fails on such sub-builds) is mended by
  configuring. The menu shows the tool `build/` uses.
- **Links at once** (in the same menu: 1, 2 — the default —, 4, or as many
  as compiling) lets compiling use every processor while fewer links, which
  take much memory, run together — with Ninja only (a job pool:
  `CMAKE_JOB_POOLS`, `CMAKE_JOB_POOL_LINK`); make's `-j` counts every job
  alike. "As many as compiling" removes only that pool.
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
  the generator and the link pool the IDE asks for, and
  `CMAKE_EXPORT_COMPILE_COMMANDS`, whose `compile_commands.json` tells
  clangd how each file is compiled) and building it (`cmake --build <build> --parallel
  <logical processors>`, or one target), and the project's targets with their type and the program each
  executable builds, read from CMake's
  [file API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html)
  after configuring (CMake 3.14 or later), not by parsing `CMakeLists.txt`.

- `eudaimonia-lsp` (`eudaimonia::LanguageSupport`): language servers (the
  [Language Server Protocol](https://microsoft.github.io/language-server-protocol/)),
  one client for any number of them, chosen by the documents' file names:
  clangd for C and C++ (with the build directory's `compile_commands.json`
  and a background index), neocmakelsp for `CMakeLists.txt` and `*.cmake`,
  rust-analyzer for Rust, pylsp for Python. It finds which of them a
  project needs by its files (build directories, `_deps` and hidden
  directories left out) and whether they are installed, keeps the servers
  told of the open documents (the whole text, sent once typing pauses for
  300 ms), and passes on their diagnostics and the places of definitions.
  `language-servers.json` in `~/.config/eudahub/eudaimonia` changes or adds
  servers without rebuilding: a JSON array of objects with `name`,
  `languages`, `program`, `arguments` (`${buildDir}` and `${projectDir}` are
  replaced), `suffixes`, `fileNames`, `languageId`, `languageIds`
  (`"suffix=id"`) and `installHint`; one with a known name replaces that
  server, and `"program": ""` removes it.

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
configures and builds a small project, so it needs cmake and a C++ compiler,
and the language support plugin's test runs a language server written in
Python (python3) for it:

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
