# NotePadLX: Notepad++ → Linux Porting Prompt

> Reusable prompt for an AI coding agent (or a human team). Paste it whole, or run it as
> "Read `NotePadLX/PROMPT.md` and continue the port from the current state in
> `NotePadLX/PORTING_STATUS.md`."
>
> Analysis is of the upstream tree at `notepad-plus-plus/` (commit `cda2acf54`, 2026‑10‑23,
> Scintilla 5.6.6, Lexilla 5.5.3).

---

## 0. Your role and the goal

You are porting **Notepad++** (Win32/C++, GPLv3) to a **native Linux desktop application**
called **NotePadLX**. Keep as much of the upstream behaviour, file formats, and code as Linux
allows, and replace only what is truly Windows‑specific.

- **Source (read‑only reference):** `../notepad-plus-plus/` (a checkout of the Notepad++ sources next to this repository)
  Never modify it. It is the upstream mirror we diff against when Notepad++ changes.
- **Target:** this repository (`NotePadLX/`)
- **Progress log:** `NotePadLX/PORTING_STATUS.md`. Create it on the first run. Update it at the
  end of every session: phase, what was done, what is stubbed, known bugs, next step.
  Read it first on every later run.
- **License:** NotePadLX is a derivative work, so it stays **GPLv3**. Keep upstream copyright
  headers on every file you port and add a "Modified for NotePadLX (Linux port)" line.
  Do not use the "Notepad++" name or logo as the product brand. Credit upstream in About.

---

## 1. What the upstream code actually is (analysis)

| Area | Location | Size / facts | Portability |
|---|---|---|---|
| Editing component | `scintilla/` | Scintilla 5.6.6 **with Notepad++ patches** in `src/Editor.cxx`, `src/EditView.cxx`, `src/Indicator.*`, `include/Scintilla.h` (e.g. `INDIC_EXPLORERLINK`, 32‑bit range APIs removed in favour of `*FULL`), `ScintillaWin.cxx` | Core is portable. Ships official **Qt** (`scintilla/qt/ScintillaEditBase`) and **GTK** (`scintilla/gtk`) backends. |
| Lexers | `lexilla/` | 128 lexers. N++ adds `LexUser.cxx` (User Defined Language), `LexSearchResult.cxx`, `LexObjC.cxx`, and patches `StyleContext`/`WordList` | 100 % portable |
| Regex | `boostregex/` | Boost.Regex search engine plugged into Scintilla through `BoostRegexSearch.h` (hooked in `ScintillaWin.cxx`) | Portable. Must be hooked into the Qt backend the same way. |
| XML / JSON / charset detection | `PowerEditor/src/pugixml`, `json`, `uchardet` | Third‑party | Portable |
| Hashes | `MISC/md5`, `sha1`, `sha2`, `sha512`, `crc16`, `hmac` | Algorithms are portable; dialogs are Win32 | Logic portable |
| App core and UI | `PowerEditor/src/**` | ≈138k lines (excluding 3rd party), 123 `.cpp` files, 26 `.rc` files with ~70 dialogs | Mostly Win32 |
| Main window | `Notepad_plus.cpp` (9.6k), `NppBigSwitch.cpp` (4.5k, `WM_*` and `NPPM_*` dispatcher), `NppCommands.cpp` (4.6k, 711 `IDM_*` cases), `NppNotification.cpp`, `NppIO.cpp` (3k) | Logic tangled with `HWND`/`SendMessage` | Rework |
| Settings | `Parameters.cpp/.h` (11.6k) | XML via pugixml: `config.xml`, `langs.xml`, `stylers.xml`, `shortcuts.xml`, `session.xml`, `contextMenu.xml`, `tabContextMenu.xml`, `toolbarButtonsConf.xml`, `userDefineLang.xml`, themes, ~100 `nativeLang` localisation files | Logic portable. Paths and registry are not. |
| Documents | `ScintillaComponent/Buffer.cpp` (`Buffer`, `FileManager`, load/save, encodings, backup/snapshots), `Utf8_16.cpp`, `EncodingMapper.cpp` | Uses `CreateFile`/`ReadFile`, `WideCharToMultiByte` | Port with care |
| Editor view | `ScintillaComponent/ScintillaEditView.cpp` (5k) | Calls Scintilla through its direct function pointer `_pScintillaFunc(_pScintillaPtr, msg, w, l)`. This is a large advantage: the Qt widget exposes the same entry point. | High reuse |
| Find/Replace | `FindReplaceDlg.cpp` (7.2k), Find in Files, mark, incremental search, search‑results panel | Logic plus Win32 dialog | Split logic from UI |
| Custom Win32 controls | `WinControls/*`: DockingWnd (docking manager, gripper, containers), TabBar, ToolBar, StatusBar, SplitterContainer, TreeView, Grid/BabyGrid, Preference (7.4k), ShortcutMapper, WordStyleDlg, FunctionList, DocumentMap, ProjectPanel, FileBrowser, ClipboardHistory, AnsiCharPanel, VerticalFileSwitcher, WindowsDlg, PluginsAdmin, TaskList, TrayIcon, AboutDlg, RunDlg, ColourPicker, ContextMenu, CustomFileDialog (IFileDialog COM), ReadDirectoryChanges | **Rewrite** with toolkit widgets |
| Dark mode | `NppDarkMode.cpp` (5k) and `DarkMode/` (uxtheme hacks, 1279 references in 74 files) | Pure Win32 hack | **Drop**. Use toolkit theming. |
| Plugins | `MISC/PluginsManager` with a DLL ABI (`setInfo`, `getName`, `getFuncsArray`, `beNotified`, `messageProc`, `__declspec(dllexport)`, `HWND` in `NppData`), 947 `NPPM_*` references | Windows plugins **cannot** run on Linux | Recreate the API as a `.so` ABI |
| Windows‑only features | `RegExt` (file associations in the registry), `verifySignedfile` (WinVerifyTrust), `MiniDumper`/`Win32Exception`, WinGUp auto‑updater, `dpiManagerV2`, NSIS installer, shell context menu, jump lists | No Linux equivalent, or a different one | Replace or drop (see §6) |

Scale of Win32 coupling in `PowerEditor/src`: about 1,200 `SendMessage`, about 1,000 `HWND`,
about 1,500 `WM_*`, 2,382 `wchar_t`/`std::wstring`/`TCHAR`/`generic_string`, and 85
`WideCharToMultiByte`/`MultiByteToWideChar`/`sizeof(wchar_t)`.

The app also builds with MinGW (`PowerEditor/gcc/makefile`), so the non‑UI code already
compiles with GCC. That makes the core much easier to port.

Upstream tests worth reusing: `PowerEditor/Test/FunctionList`, `Test/UrlDetection`,
`Test/xmlValidator`.

---

## 2. Toolkit decision: **Qt 6** (Widgets), C++20, CMake

### Options compared

| Criterion | **Qt 6 Widgets** | GTK (3/4, C or gtkmm) | Rust egui/eframe (+ winit, wgpu, portals) |
|---|---|---|---|
| Scintilla backend | ✅ Official `ScintillaEditBase` in the tree. Proven by **NotepadNext**, a Qt6 + Scintilla + Lexilla Notepad++ re‑implementation. | ✅ Official, but **GTK3 only**. No GTK4 backend. GTK3 is in maintenance mode. | ❌ None. You would write a new text editor (rope buffer, styling, folding, multi‑caret, column mode, IME, BiDi, 2 GB files) and drop all 128 Lexilla lexers plus UDL, or wrap C++ Scintilla in an unsupported custom wgpu surface. |
| Reuse of 138k lines of C++ | ✅ Same language. Core files move over almost unchanged. | ⚠️ Possible from C++, but gtkmm adds friction. | ❌ Full rewrite in Rust. The code becomes reference material only. |
| Docking panels (Function List, Doc Map, Project, Folder as Workspace…) | ✅ `QDockWidget`, or KDDockWidgets / Qt‑Advanced‑Docking‑System for N++‑style tabbed docks | ⚠️ No built‑in docking (libpanel/gdl are niche) | ⚠️ `egui_dock` exists; you build the rest |
| Dialog‑heavy UI (~70 dialogs, huge Preferences) | ✅ Qt Designer `.ui` files, layouts, validators | ✅ Builder/Blueprint | ⚠️ Immediate‑mode UI; every dialog hand‑coded |
| Native look, accessibility, IME, printing | ✅ All built in (`QPrinter`, AT‑SPI, input methods, HiDPI, dark palettes) | ✅ Good on GNOME | ⚠️ AccessKit is improving. IME and printing are weak. |
| Wayland/X11, portals, tray, single‑instance | ✅ Native (`QFileDialog` portal, `QSystemTrayIcon`, `QLocalServer`) | ✅ | ✅ via `ashpd`/`rfd` |
| Plugin ABI from C/C++ | ✅ Natural | ✅ | ⚠️ Needs a C FFI layer |
| Effort to reach feature parity | **Lowest** | Medium (and GTK3 is a dead end) | **Highest**, many person‑years |

### Verdict

**Use Qt 6 Widgets.** The core of Notepad++ is Scintilla plus Lexilla plus C++ logic that
drives Scintilla through `SCI_*` messages. Qt keeps all of that and Scintilla officially
supports it. egui/eframe works well for *new* Rust apps such as PdfCraft or PhotoCraft,
where the UI is canvas‑like and built from scratch. For an editor whose value sits in a
C++ text engine it would mean rewriting the product, not porting it. GTK is workable but
ties you to GTK3. Use Qt **Widgets**, not QML: the app is dialog‑ and control‑heavy, and
Scintilla's Qt backend is a `QAbstractScrollArea`.

Recommended stack:

- Qt ≥ 6.5 LTS (Core, Gui, Widgets, Network, PrintSupport, DBus, and **Core5Compat**, which
  Scintilla's Qt backend requires for `QTextCodec`), **C++20**, **CMake ≥ 3.24**, Ninja,
  GCC ≥ 12 or Clang ≥ 15.
- Scintilla (Qt backend) plus Lexilla, built as **static libraries** from the **Notepad++‑patched**
  sources (copy them; never use distro Scintilla).
- Boost.Regex: use the bundled copy from `boostregex/`, or system `libboost-regex` if it is
  header‑compatible.
- pugixml, nlohmann/json, uchardet: bundled copies, or system packages behind a CMake option.
- **ICU** or **iconv** for legacy code‑page encodings. Qt 6's `QStringConverter` covers only
  UTF‑* and Latin‑1. Prefer iconv (glibc) with ICU as an option.
- Docking: start with `QDockWidget`. Move to **KDDockWidgets** (GPL/commercial) or
  **Qt‑Advanced‑Docking‑System** (LGPL) if N++‑style tabbed and floating docks are required.
- Tests: Qt Test or Catch2. Packaging: AppImage plus Flatpak, then .deb and .rpm.

Look at **NotepadNext** (github.com/dail8859/NotepadNext) for proven Qt solutions:
ScintillaNext wrapper, docking, Lua. Do **not** copy its code blindly. NotepadNext is GPLv3
too, so code is license‑compatible, but keep attribution, and our goal is to port *upstream*
Notepad++ code, not to fork NotepadNext.

Rejected alternatives, for the record: running Notepad++ under **Wine** (works today with
zero effort, but it is not native) and **Winelib** (compiles Win32 code on Linux, but you
keep the Win32 look and the Wine runtime).

---

## 3. Target architecture

```
NotePadLX/
├── CMakeLists.txt
├── PROMPT.md                 ← this file
├── PORTING_STATUS.md         ← running log (create on first run)
├── UPSTREAM.md               ← upstream commit/versions ported from, sync notes
├── third_party/
│   ├── scintilla/            ← copied from notepad-plus-plus/scintilla (N++ patches incl.)
│   │                            build: src/ + qt/ScintillaEditBase  (NOT win32/, gtk/)
│   ├── lexilla/              ← copied incl. LexUser, LexSearchResult, LexObjC
│   ├── boostregex/
│   ├── pugixml/  json/  uchardet/
├── src/
│   ├── core/                 ← toolkit-free C++ (NO Qt Widgets, NO windows.h)
│   │   ├── platform/         ← win32compat.h, paths (XDG), encoding (iconv), fs, process
│   │   ├── Parameters.*      ← ported from PowerEditor/src/Parameters.*
│   │   ├── Buffer.* FileManager  Utf8_16.*  EncodingMapper.*
│   │   ├── Lang/UDL/Styles, FunctionList parser, AutoCompletion data, Macro model
│   │   ├── Search engine (find/replace/mark/find-in-files logic)
│   │   └── Localization (nativeLang XML loader, keyed by the same control/menu IDs)
│   ├── editor/               ← ScintillaEditView port on top of ScintillaEditBase
│   ├── ui/                   ← Qt Widgets: MainWindow, TabBar, docks, dialogs (.ui files)
│   ├── app/                  ← main.cpp, command line, single instance, session restore
│   └── plugins/              ← Linux plugin host (.so) + NPPM_* dispatcher
├── resources/                ← icons (freedesktop names + upstream SVG/PNG), qrc
├── data/                     ← langs.model.xml, stylers.model.xml, themes/, APIs/,
│                               functionList/, autoCompletion/, nativeLang/ (from installer/)
├── packaging/                ← .desktop, AppStream metainfo, MIME, Flatpak, AppImage
└── tests/                    ← unit tests + ported PowerEditor/Test suites
```

### Key design rules

1. **Keep numeric IDs.** Reuse `menuCmdID.h` (`IDM_*`), `resource.h`, and the dialog and control
   IDs from the `.rc` files exactly. `shortcuts.xml`, macros, `nativeLang/*.xml`,
   `contextMenu.xml`, toolbar config, and the plugin API all reference these numbers. Each
   `QAction` stores its `IDM_*` in `data()`, and **one** `Notepad_plus::command(int id)` (ported
   `NppCommands.cpp`) executes it.
2. **Keep the message dispatcher.** Port `NppBigSwitch.cpp` into a toolkit‑independent
   `LRESULT NppCore::message(UINT msg, WPARAM w, LPARAM l)` that handles every `NPPM_*`.
   Internal callers and plugins use it. Window messages (`WM_SIZE`, `WM_COPYDATA`,
   `WM_DROPFILES`, …) become Qt events and signals. Do not emulate them.
3. **Scintilla calls stay `SCI_*` calls.** `ScintillaEditView::execute(msg, w, l)` already
   uses the direct function. Bind it to `ScintillaEditBase::send()` (or `sends()`), or to the
   direct pointer from `SCI_GETDIRECTFUNCTION`/`SCI_GETDIRECTPOINTER`, so most of the 5k
   lines port with little change. `SCNotification` arrives via the Qt `notify(Scintilla::NotificationData*)`
   signal and is forwarded to the ported `NppNotification.cpp` logic.
4. **Layering:** `core/` must compile without Qt Widgets and without `<windows.h>`.
   QtCore is allowed only if a feature truly needs it; prefer std/C++20. `ui/` is the only
   place that knows about widgets.
5. **Win32 type shim, not a Win32 emulator.** `core/platform/win32compat.h` may define
   `UINT, WPARAM, LPARAM, LRESULT, BOOL, TRUE/FALSE, COLORREF, RGB(), LOWORD/HIWORD, MAX_PATH`
   (Scintilla's own `uptr_t`/`sptr_t` cover WPARAM/LPARAM) so ported logic compiles with
   minimal edits. **Never** fake `HWND`, `SendMessage`, `CreateWindow`, or the registry. Each
   such call site gets real Linux or Qt code.
6. **Upstream‑sync friendliness.** Keep upstream file and class names where the logic is
   ported (`Buffer.cpp`, `Parameters.cpp`, `FindReplaceDlg` → `FindReplaceDlg` (Qt), …).
   Mark Linux deviations with `// NPLX:` comments so future upstream diffs can be merged
   one by one.

---

## 4. Win32 → Linux/Qt mapping cheat‑sheet

| Win32 / upstream | NotePadLX replacement |
|---|---|
| `WinMain`, `GetMessage`/`DispatchMessage`, `TranslateAccelerator` | `QApplication::exec()`, `QAction`/`QShortcut` built from `shortcuts.xml` |
| `HWND` windows, `CreateWindowEx`, `.rc` dialogs, `StaticDialog` | `QWidget`/`QDialog` + Qt Designer `.ui` (one per upstream dialog, same control IDs as `objectName` or a property) |
| `Scintilla_RegisterClasses`, `ScintillaWin` | `ScintillaEditBase` (Qt). Apply the N++ `ScintillaWin.cxx` patches that matter on Qt: Boost regex hook (`#include "BoostRegexSearch.h"`, `SCI_OWNREGEX`), the ANSI/charset code‑page fix (#5671), and wheel and DPI behaviour where relevant |
| `TabBar` (owner‑drawn) | `QTabBar` subclass: close buttons, drag‑reorder, drag to the other view, colours per tab, pinned and read‑only icons |
| `SplitterContainer` (two views) | `QSplitter` holding two `ScintillaEditView`s |
| `DockingManager`/`DockingCont`/`Gripper` | `QDockWidget` (or KDDockWidgets / ADS). Persist the layout in `config.xml` like upstream |
| `ToolBar`, `ReBar`, `StatusBar` | `QToolBar` (configurable via `toolbarButtonsConf.xml`), `QStatusBar` with the same parts |
| `TreeView`, `ListView`, `BabyGrid` | `QTreeView`/`QTreeWidget`, `QTableView` + models |
| `GetOpenFileName`/`IFileDialog` (`CustomFileDialog`) | `QFileDialog` (uses the XDG portal on Flatpak/Wayland). Map the extra checkbox and encoding combo to non‑native dialog options, or drop them |
| `ChooseColor`, `ColourPicker` | `QColorDialog`, `QToolButton` swatches |
| `ChooseFont` | `QFontDialog` / `QFontComboBox` |
| Registry (`RegOpenKey…`, `RegExt` file associations) | XDG MIME: `.desktop` with `MimeType=`, `xdg-mime default`. "Register extensions" dialog → writes `~/.local/share/applications/mimeapps.list` or is removed |
| `%APPDATA%\Notepad++`, `doLocalConf.xml` (portable) | `$XDG_CONFIG_HOME/notepadlx/` (config), `$XDG_DATA_HOME/notepadlx/` (backups, plugins), `$XDG_CACHE_HOME`. System data in `/usr/share/notepadlx`. Keep **portable mode**: `doLocalConf.xml` next to the binary or AppImage → config beside the executable |
| `CreateFile`/`ReadFile`/`WriteFile`, `GetFileAttributes`, `FILETIME` | `std::filesystem` + POSIX (`open`, `fstat`, `fsync`, `rename`). **Atomic save** (write a temp file, `fsync`, `rename`) that keeps mode, owner, xattrs, and symlink targets. Handle EACCES with an "elevate" path (`pkexec` helper, optional) |
| `ReadDirectoryChangesW` (file monitoring, Folder as Workspace) | `QFileSystemWatcher` (inotify). Add a fallback stat poll for network filesystems |
| `wchar_t` paths and strings | ⚠️ **`wchar_t` is 32‑bit (UTF‑32) on Linux.** Audit every `sizeof(wchar_t)`, every surrogate‑pair loop, and `Utf8_16`. Strategy: (a) Phase 1 keeps `std::wstring` in the core with a conversion shim (`wmemcpy` semantics, `std::filesystem::path` for I/O). (b) Long term, migrate the core to UTF‑8 `std::string`, since Scintilla is UTF‑8 internally. Convert to `QString` only at the UI boundary |
| `WideCharToMultiByte`/`MultiByteToWideChar`, Windows code pages (`CP_ACP`, 1250–1258, 932, 936, 949, 950, …) | `core/platform/encoding.cpp` on iconv (or ICU), with the same code‑page numbers as `EncodingMapper` so `config.xml` and the encoding menu are unchanged. Linux "ANSI" defaults to the locale charset, usually UTF‑8 |
| uchardet detection | Unchanged |
| `ShellExecute`, `Processus`, `RunDlg`, `$(FULL_CURRENT_PATH)` variables | `QProcess::startDetached`, `QDesktopServices::openUrl`, `xdg-open`. "Open containing folder" → `org.freedesktop.FileManager1.ShowItems` over D‑Bus, else `xdg-open dir`. "Open in cmd" → `$TERMINAL`, `x-terminal-emulator`, or a user setting |
| Single instance, `WM_COPYDATA`, `-multiInst` | `QLocalServer`/`QLocalSocket` (per user and session). Forward argv and cwd. Keep the upstream CLI flags (`-n`, `-c`, `-l`, `-x/-y`, `-ro`, `-nosession`, `-multiInst`, `-openSession`, …) |
| Tray (`TrayIcon`) | `QSystemTrayIcon` (StatusNotifierItem). Degrade gracefully on GNOME without the extension |
| Printing (`Printer.cpp`, `SCI_FORMATRANGEFULL`) | `QPrinter`/`QPrintDialog`/`QPrintPreviewDialog` + Scintilla Qt `FormatRange` with a `QPainter` (verify `ScintillaEditBase` print support; implement a `Surface` on `QPrinter` if needed). Keep header and footer variables |
| `NppDarkMode`, `DarkMode/` | Delete. Follow the system colour scheme (`QStyleHints::colorScheme()`, Qt ≥ 6.5), offer a Light/Dark/System preference, and set a dark `QPalette`/Fusion. Editor colours still come from `stylers.xml` and themes (DarkModeDefault.xml) |
| `dpiManagerV2`, `WM_DPICHANGED` | Delete. Qt high‑DPI handles it. Use SVG icons |
| Clipboard formats (`clipboardFormats.h`, MSDEVCOLUMNSELECT, binary) | `QClipboard`/`QMimeData` with custom MIME types. Also support X11/Wayland **primary selection** (middle‑click paste) via Scintilla Qt |
| Drag & drop files (`WM_DROPFILES`) | `dragEnterEvent`/`dropEvent` with `text/uri-list` |
| `MiniDumper`, `Win32Exception` | Drop. Optionally use `signal()` + `backtrace()`, or KCrash/drkonqi, plus an emergency save of dirty buffers to the backup dir |
| `verifySignedfile` (WinVerifyTrust), WinGUp updater, `PluginsAdmin` downloads | Updater: **disabled**; distro, Flatpak, or AppImage updates own it. PluginsAdmin: phase 2, against a Linux plugin list. Verify with SHA‑256 (and optionally GPG/minisign) |
| `GetLocaleInfo`, `SortLocale` | `std::locale`/ICU collation, `QCollator` |
| Taskbar progress, jump lists, `IShellLink` | Drop. Optionally use the Unity LauncherEntry D‑Bus API |
| Alt+drag column selection | ⚠️ Many WMs grab Alt+drag. Also support Ctrl+Alt+drag, Alt+Shift+arrows, and a "column mode" toggle |
| Windows‑specific menu entries | Hide or adapt: "Open in cmd", "Explorer", "Set as default for…", Windows Explorer context integration → Nautilus/Dolphin "Open with" via .desktop |

---

## 5. Phased plan (each phase must build, run, and pass its acceptance checks)

Work strictly phase by phase. Inside a phase, take small, compiling steps. After each phase,
update `PORTING_STATUS.md` and, if the user asked for commits, commit (`git init` NotePadLX
first if it is not a repo).

### Phase 0: Bootstrap
- Create the CMake project. Copy the patched `scintilla/` (src, include, qt/ScintillaEditBase,
  call), `lexilla/`, `boostregex/`, `pugixml`, `json`, `uchardet` into `third_party/` and
  record the versions and upstream commit in `UPSTREAM.md`.
- Build Scintilla Qt and Lexilla as static libraries. Wire up **Boost regex** (the
  `SCI_OWNREGEX` define and `BoostRegExSearch.cxx`) and confirm that the N++ custom lexers
  (UDL, SearchResult) are registered in `Lexilla.cxx`.
- A `MainWindow` with one `ScintillaEditBase` that loads a file with C++ highlighting through
  `CreateLexer("cpp")`.
- **Accept:** `cmake -B build -G Ninja && cmake --build build` works on a clean Ubuntu 24.04
  or Fedora 41+. The app opens a file and highlights it. A regex search with `\b\w+\b`
  works through Boost.

### Phase 1: Portable core
- Port `Parameters` (pugixml; XDG paths; portable mode), `Buffer`/`FileManager`
  (load/save, EOL detection, BOMs, UTF‑8/16, legacy code pages via iconv, uchardet, 2 GB+
  files, read‑only, backup/snapshot), `Utf8_16`, `EncodingMapper`, `lastRecentFileList`,
  `localization` (nativeLang XML), `Common.cpp` helpers (split them: portable to `core`,
  UI to `ui`).
- Copy `langs.model.xml`, `stylers.model.xml`, `installer/themes`, `installer/APIs`,
  `installer/functionList`, `installer/nativeLang`, and `bin/userDefineLangs` into `data/`.
- **Accept:** a headless unit test loads `langs.xml`, `stylers.xml`, and `config.xml`, opens
  files in each encoding listed in `installer/filesForTesting`, then saves and reloads them
  byte‑identically (BOM, EOL, encoding). `core/` builds with `-DQT_NO_WIDGETS` and contains
  no `windows.h`.

### Phase 2: Main window skeleton
- Port `ScintillaEditView` (styling from stylers and themes, folding, margins, line numbers,
  bookmarks, wrap, whitespace and EOL display, indent guides, multi‑caret, column mode, zoom).
- Main and secondary views in a `QSplitter`, `DocTabView` on `QTabBar`, status bar (line/col,
  sel, length, EOL, encoding, INS/OVR, language), toolbar, and a **menu built from the same
  `IDM_*` tree** as `Notepad_plus.rc`, localised via nativeLang.
- Port `NppCommands.cpp` command dispatch: File, Edit, View, and Language menus first.
- **Accept:** open, edit, save, and close many files in tabs. Clone or move a document to the
  other view. Change the language. Every `IDM_*` that is not ported yet appears greyed out
  with a tooltip saying "not yet ported" and is listed in `PORTING_STATUS.md`.

### Phase 3: Files, sessions, monitoring
- Recent files, sessions (`session.xml` format unchanged), restore on startup, periodic
  backup and snapshot (unsaved changes survive a crash), and external‑change detection with
  reload prompts (`QFileSystemWatcher`).
- Single instance plus all upstream command‑line options.
- Drag & drop, "Open containing folder", rename, delete‑to‑trash (`QFile::moveToTrash`).
- **Accept:** kill ‑9 the app with unsaved edits, restart, and the edits are restored. Editing
  a file in another process triggers a reload prompt. `notepadlx file.txt` from a second
  terminal opens a tab in the running instance.

### Phase 4: Search
- Port the `FindReplaceDlg` logic (Find, Replace, Find in Files, Find in Projects, Mark,
  style marks 1–5, bookmarks of matches, count, in‑selection, wrap, backward, extended `\n\t`
  mode, Boost regex), the Incremental Search bar, the Search Results dock with its custom
  lexer, Go To line, and Find Characters in Range.
- Find in Files runs in a worker thread (`QtConcurrent` or `std::jthread`) with cancel, filters,
  and hidden‑folder options.
- **Accept:** results on a sample corpus match upstream on Windows (or Wine) for 20
  representative patterns, including multiline regex and backreferences.

### Phase 5: Languages and styling
- Style Configurator (`WordStyleDlg`), theme switching, global overrides.
- **User Defined Language** dialog (`UserDefineDialog`, 2.1k lines) driving `LexUser`.
- Auto‑completion and call tips (`AutoCompletion`, `FunctionCallTip`, from `APIs/*.xml`),
  smart highlighting, XML/HTML matched‑tag highlighting, brace matching, URL detection
  (port the `Test/UrlDetection` suite).
- **Accept:** every theme in `data/themes` renders. Importing upstream UDL files from
  `userDefineLangs` works. The URL‑detection tests pass.

### Phase 6: Docking panels
- Function List (port the parser and run `Test/FunctionList` as tests), Document Map,
  Document List (VerticalFileSwitcher), Project Panels 1–3, Folder as Workspace,
  Clipboard History, ASCII/Character Panel, Windows dialog (sort/close/activate), and
  Ctrl+Tab document switcher (TaskList).
- **Accept:** the dock layout persists across restarts. The Function List tests match
  upstream expected outputs.

### Phase 7: Preferences, shortcuts, macros, run
- Preferences (all pages; hide only the Windows‑only options and list them), Shortcut Mapper
  (`shortcuts.xml` compatible, conflict detection), macro record, playback, save, and
  run‑multiple (macros are recorded `SCI_*`/`IDM_*` sequences, which are portable), Run
  dialog with N++ variables, column editor, Edit → Line ops / Convert case / Blank ops /
  Comment, Sort lines (locale‑aware), Hash dialogs (MD5/SHA‑1/SHA‑256/SHA‑512), Base64,
  and similar tools.
- **Accept:** an upstream `shortcuts.xml` (with macros and user commands) loads and works
  unchanged.

### Phase 8: Polish and platform integration
- Dark/light/system theme, tray icon, printing with preview, About/Debug Info (Qt, distro,
  WM/compositor, Wayland/X11), localisation switch at runtime, distraction‑free and
  full‑screen modes, Always on Top (X11 only; on Wayland, document the limitation), and
  accessibility pass.
- **Accept:** the app runs under GNOME Wayland, KDE Wayland, and X11 without warnings in
  the console. All `nativeLang` files load.

### Phase 9: Plugin API for Linux
- Define `NotePadLX/src/plugins/PluginInterface.h`, a C ABI that mirrors upstream:
  `setInfo(NppData)`, `getName()`, `getFuncsArray(int*)`, `beNotified(SCNotification*)`,
  `messageProc(UINT, WPARAM, LPARAM)`, exported with `__attribute__((visibility("default")))`.
  `NppData` replaces the `HWND`s with opaque handles **plus function pointers**:
  `nppSend(handle, msg, w, l)` (routes into `NppCore::message`, so all `NPPM_*` keep their
  numbers and semantics) and `sciSend(view, msg, w, l)`. Docking for plugins
  (`NPPM_DMMREGASDCKDLG`) receives a `QWidget*` via a Qt‑specific extension header.
- Strings across the ABI: UTF‑8 `char*` (document this; do not put Linux `wchar_t` in the ABI).
  Keep the `NPPM_*` numeric values and add `NPPM_*_UTF8` variants where upstream uses `wchar_t*`.
- Load from `$XDG_DATA_HOME/notepadlx/plugins/<Name>/<Name>.so` and the system dir.
- Ship two sample plugins (Hello World, and one that uses docking), plus a mention of how
  popular plugins could be recompiled (e.g. JSON Viewer, Compare).
- **Accept:** the sample plugins load, add menu items, receive notifications, and edit text.

### Phase 10: Packaging and CI
- `.desktop` (with `MimeType=` list derived from `langs.model.xml` extensions), AppStream
  `metainfo.xml`, hicolor icons, man page, `install()` rules, AppImage, Flatpak manifest
  (`org.notepadlx.NotePadLX`), and CPack `.deb`/`.rpm`.
- GitHub Actions or other CI: build plus tests on Ubuntu LTS and Fedora, clang‑tidy, ASan/UBSan job.
- **Accept:** installing the package registers file associations. The Flatpak works with
  portal file dialogs.

---

## 6. What cannot or should not be ported (document each in PORTING_STATUS.md)

- **Binary Windows plugins (.dll)** cannot load. Only source‑recompiled plugins against
  the Linux ABI will work.
- **WinGUp auto‑update, NSIS/MSI installer, Authenticode checks:** replaced by package managers.
- **Registry‑based file association dialog and shell extension (context menu):** replaced by
  .desktop/MIME integration.
- **Win32 dark‑mode hacks, DPI manager, MiniDumper:** replaced by Qt equivalents or dropped.
- **Windows‑only options in Preferences** (e.g. "Use Windows' native file dialog", jump
  lists, taskbar, "Run as admin" save fallback via UAC → offer `pkexec` helper or a
  "Save as root" instruction).
- **Always‑on‑top and window positioning on Wayland:** limited by the compositor.
- **Alt+drag** can be stolen by the window manager. Document the workarounds.

---

## 7. Working rules for the agent

1. **Start of every session:** read `PORTING_STATUS.md` and `UPSTREAM.md`, build, and run the
   tests. Fix a broken build before you add features.
2. **Port, don't paraphrase.** For each upstream file: copy it, strip the Win32 parts, and
   keep the algorithms, names, and comments. Rewrite from scratch only the Win32 controls
   listed in §1.
3. **Never** edit `notepad-plus-plus/`. To see what Notepad++ patched in Scintilla or Lexilla,
   diff against the pristine zips in
   `notepad-plus-plus/PowerEditor/scintilla.original.forUpdating/` (use
   `diff -r --strip-trailing-cr`; upstream files use CRLF).
4. **Normalise line endings to LF** in NotePadLX. Add a `.gitattributes`.
5. **No silent stubs:** every unimplemented feature is either greyed out in the UI or logs
   `NPLX_TODO("…")` once, and it is listed in `PORTING_STATUS.md`.
6. **Compatibility first:** config XML, session, shortcuts, themes, UDL, and localisation
   files from Windows Notepad++ must load unchanged (convert Windows paths in sessions
   on a best‑effort basis).
7. Build with `-Wall -Wextra -Werror=return-type`. Fix every warning in code you ported.
   Use RAII; no new raw `new`/`delete` in new code.
8. Write tests alongside the core: encoding round‑trips, EOL, search, Function List,
   URL detection, and the settings loader.
9. Ask the user before large irreversible choices: switching the docking library, changing
   the plugin ABI, or migrating the core from `std::wstring` to UTF‑8.
10. End each session by summarising what changed, what was verified (paste test results), and
    the next step, and record all of it in `PORTING_STATUS.md`.

---

## 8. Kick‑off instruction (first run)

> Execute **Phase 0** from `NotePadLX/PROMPT.md`. Create `PORTING_STATUS.md`, `UPSTREAM.md`,
> and the CMake skeleton. Vendor Scintilla (Qt backend, N++‑patched), Lexilla (with the N++
> custom lexers), and Boost regex. Get a Qt 6 window to open and highlight a C++ file and run
> a Boost regex search. Report the build commands, the exact dependency package names for
> Ubuntu and Fedora, and the test results.
