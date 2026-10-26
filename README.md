<div align="center">

<img src="resources/icons/notepadlx-256.png" alt="NotePadLX" width="96">

# NotePadLX

**Notepad++, ported to Linux** — a native Qt 6 editor built from the Notepad++ sources.

[![Version](https://img.shields.io/badge/version-0.9-2563eb)](https://github.com/ClaudiuJitea/NotePadLX/releases)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue)](LICENSE)
[![Qt 6](https://img.shields.io/badge/Qt-6-41cd52?logo=qt&logoColor=white)](https://www.qt.io/)
[![Platform: Linux](https://img.shields.io/badge/platform-Linux-fcc624?logo=linux&logoColor=black)](#install)

<img src="docs/screenshots/main-dark.png" alt="NotePadLX with the Solarized theme and the function list" width="100%">

<img src="docs/screenshots/finder-dark.png" alt="Find in Files results" width="32%">
<img src="docs/screenshots/menu-dark.png" alt="The View menu" width="32%">
<img src="docs/screenshots/main-light-ui.png" alt="The light interface" width="32%">

</div>

NotePadLX **ports the Notepad++ code itself** rather than imitating it: the same Scintilla engine, the same
commands, and the same `config.xml`, `stylers.xml`, `shortcuts.xml` and `langs.xml` files. Your Notepad++
themes, user defined languages and macros work as they are.

## Features

- **Editing:** multi-selection, column mode, folding, auto-completion, smart highlighting, bookmarks, line tools
- **Languages and themes:** 80+ languages, User Defined Languages, all Notepad++ themes (Solarized dark by default)
- **Search:** Find / Replace / Mark, Find in Files, Find in Projects, the Finder panel, regular expressions
- **Panels:** Function List, Document Map, Document List, Folder as Workspace, Project Panels, Clipboard History
- **Automation:** macros, the Run menu with `$(FULL_CURRENT_PATH)`-style variables, the Shortcut Mapper
- **Tools:** Column Editor, MD5 / SHA-1 / SHA-256 / SHA-512, sessions, file monitoring, encodings, Wayland and X11

## Install

Download from [Releases](https://github.com/ClaudiuJitea/NotePadLX/releases):

```sh
chmod +x NotePadLX-0.9-x86_64.AppImage && ./NotePadLX-0.9-x86_64.AppImage   # any distro, glibc 2.43+
sudo apt install ./notepadlx_0.9_amd64.deb                                   # Ubuntu 26.04 (Qt 6.10)
```

On older distributions, build from source. Settings live in `~/.config/notepadlx/`; to bring your Notepad++
setup over, copy the files from `%APPDATA%\Notepad++`. Set `NPLX_THEME=light` for a light interface.

## Build from source

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-5compat-dev qt6-svg-plugins libgl-dev
# Fedora: gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qt5compat-devel qt6-qtsvg mesa-libGL-devel

cmake -S . -B build -G Ninja && cmake --build build
build/src/notepadlx                       # run from the build tree
cd build && ctest                         # 16 test suites, every Notepad++ default key
packaging/build-packages.sh               # dist/: .deb and AppImage
```

All other dependencies (Scintilla, Lexilla, Boost.Regex, pugixml, json, uchardet) are in `third_party/`.

## Status

**0.9** — phases 0–8 done (core, main window, files and sessions, search with incremental search, languages, panels,
dialogs and tools with **Preferences**, printing, Move / Open in New Instance, Pin Tab, the tray icon, and the interface
**translated, switched without a restart**). Files that need admin rights are saved through pkexec. Next: Find in
Finder, a Linux plugin API (9), and Flatpak / CI (10). Unported commands are greyed out;
Windows-only ones (PowerShell, registry, the WinGUp proxy) are removed. Details: [PORTING_STATUS.md](PORTING_STATUS.md),
plan: [PROMPT.md](PROMPT.md).

## Contributing

Issues and pull requests are welcome. Port upstream code rather than rewriting it, mark Linux changes with
`// NPLX:`, and add a test for each ported feature.

## License

[GPL v3](LICENSE). A port of [Notepad++](https://notepad-plus-plus.org/) by Don HO and contributors, using
[Scintilla / Lexilla](https://www.scintilla.org/), Boost.Regex, pugixml, nlohmann/json and uchardet.
Not affiliated with the Notepad++ project.
