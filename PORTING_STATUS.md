# NotePadLX Porting Status

Plan: [PROMPT.md](PROMPT.md). Upstream versions and patches: [UPSTREAM.md](UPSTREAM.md).

| Phase | Status |
|---|---|
| 0. Bootstrap | ✅ Done (2026‑10‑24) |
| 1. Portable core | ✅ Done (2026‑10‑24) |
| 2. Main window | 🚧 Mostly done (menus, toolbar, context menus, 362 commands ported) |
| 3. Files, sessions, monitoring | 🚧 Mostly done (see session 3) |
| 4. Find / Replace | 🚧 Mostly done (dialog, Finder, Find in Files, Mark, smart highlight; incremental search open) |
| 5. Languages and styling | ✅ Done except dialog localization (Phase 8) |
| 6. Docking panels | ✅ Done (2026‑10‑25): all panels, Windows dialog, Ctrl+Tab, dock layout persistence |
| 7. Dialogs and tools | 🚧 In progress: macros, Run menu, Shortcut / Run / Run Macro dialogs, Column Editor, MD5/SHA hashes done; Shortcut Mapper and Preferences next |
| 8. Polish and platform integration | ⏳ Planned (dark/light themes and the NotePadLX look done early, in sessions 6–7b) |
| 9. Plugin API for Linux | ⏳ Planned |
| 10. Packaging and CI | 🚧 Started (2026‑10‑25): `.deb` and AppImage (version 0.1); Flatpak, `.rpm` and CI next |

**Current version: 0.1** (2026‑10‑25), the first public version. Repository: https://github.com/ClaudiuJitea/NotePadLX

## Session 9 (2026‑10‑25): version 0.1, packages, README

- **Version 0.1** (`project(VERSION 0.1)`, shown in About), with a GPLv3 `LICENSE` naming Notepad++ and NotePadLX.
- **Packages:** `packaging/build-packages.sh [deb|appimage|all]` makes a Release build (`build-release/`, with
  `-DNPLX_USE_SOURCE_DATA_DIR=OFF` so the binary only reads `<prefix>/share/notepadlx`) and writes to `dist/`:
  - `notepadlx_0.1_amd64.deb` (CPack): dependencies from `dpkg-shlibdeps` plus `qt6-svg-plugins`, recommends
    `qt6-wayland`. It contains `/usr/bin/notepadlx`, the data, the `.desktop` file, hicolor icons 16–512 from
    `resources/icons`, the AppStream metainfo, the man page `notepadlx(1)`, README and copyright.
  - `NotePadLX-0.1-x86_64.AppImage` (linuxdeploy + linuxdeploy-plugin-qt, downloaded into `build-release/tools`):
    Qt with the xcb and Wayland platforms and the SVG plugins.
  - Both were smoke-tested with a fresh config (the `.deb` unpacked, the AppImage on the Wayland desktop): they start
    and write the dark Solarized default `config.xml`.
  - The 0.1 binaries are built on Ubuntu 26.04: glibc ≥ 2.43, and Qt ≥ 6.10 for the `.deb`. Building on an older
    distribution (CI) would widen this.
- **Desktop metadata:** `packaging/io.github.ClaudiuJitea.NotePadLX.metainfo.xml` (passes `appstreamcli validate`),
  `packaging/notepadlx.1`, and the `.desktop` categories fixed (`desktop-file-validate`).
- **README.md** with screenshots in `docs/screenshots/`, rendered from the real app by `tests/tst_screenshots.cpp`.
  Regenerate them with
  `NPLX_README_SCREENSHOTS_DIR=$PWD/docs/screenshots QT_QPA_PLATFORM=offscreen build/tests/tst_screenshots`.
- **Self-contained repository:**
  - a copy of only this folder configures, builds and passes all 14 test suites (checked);
  - the sibling `notepad-plus-plus/` checkout is only an argument of the `tools/gen_*.py` generators, whose outputs
    are committed;
  - `.gitignore` excludes `build*/` and `dist/` (attach the packages to a GitHub release).
- **Fixes found by the release build's warnings:**
  - `drawDocumentMapColoursFromStylerArray` compared `static_cast<long>(COLORREF) != -1`, which is never false
    where `long` is 64-bit;
  - a misleading indentation in `FindReplaceDlg::changeEvent`.

## Session 8 (2026‑10‑25): Phase 7 — Tools hashes and the Column Editor

- **Help menu:** the Notepad++ Online User Manual and Community (Forum) entries are removed (`isRemovedOnLinux`).
- **Tools › MD5 / SHA‑1 / SHA‑256 / SHA‑512** (12 commands): Generate…, Generate from files…, and Generate from
  selection into clipboard.
  - The dialogs are a port of upstream `MISC/md5/md5Dlgs` (`ui/md5Dlgs.*`, from IDD_HASHFROMTEXT_DLG /
    IDD_HASHFROMFILES_DLG).
  - The digests use upstream's portable code in `core/hash`: `md5.h`, `sha1.*`, `calc_sha1.*`, `sha-256.*`.
  - Linux fixes in that code: MD5's `UINT4` is now `uint32_t` (it was `unsigned long`, 64-bit on LP64); SHA-1's
    `_wfopen`/`_snwprintf` became portable calls.
  - SHA-512 replaces upstream's CryptoAPI call with a portable FIPS 180-4 implementation (`core/hash/sha512.cpp`).
  - Every algorithm is checked against `QCryptographicHash` (empty, block-boundary, multi-block and UTF-8 inputs).
  - Files are hashed binary-safe. MD5 now uses `digestMemory`; upstream's `digestString` stops at the first NUL,
    which the new test caught.
  - Fixed upstream's title typo: the SHA-512 "from files" dialog said "SHA-1".
- **Edit › Column Editor…:** a port of upstream `ScintillaComponent/columnEditor` (`ui/columnEditor.*`, from
  IDD_COLUMNEDIT).
  - Covers text and number modes, Dec/Hex/Oct/Bin, A‑F case, leading None/Zeros/Spaces, increase and repeat, on a
    column or multi-selection or from the caret down; one undo action.
  - Settings are kept in `config.xml`.
  - An invalid entry gets a tooltip (the balloon tip) and a 250 ms red flash.
  - NPLX fix: in number mode without a selection, upstream offsets its wide string by a byte offset; it is now
    counted in characters, so lines with multi-byte UTF-8 before the column insert at the right place.
- **Tests:** `tests/tst_tools.cpp` (9 cases); 13/13 suites pass.

## Remaining work (every menu item that is still disabled)

The menu shows each unported command greyed out with the tooltip "Not yet ported to NotePadLX". This is where each
one is planned:

| Phase | Menu items |
|---|---|
| 3/4 leftovers | File › Load Session…, Save Session…; Search › Incremental Search; View › Tab › Apply Color 1–5, Remove Color |
| 7 | Settings › Preferences…, Shortcut Mapper… (also Macro/Run › Modify Shortcut/Delete…), Edit Popup ContextMenu, Import › Import style theme(s)…; Search › Find characters in range…; Edit › Paste Special › Paste HTML/RTF Content, Copy/Cut/Paste Binary Content; Edit › On Selection › Open File, Open Containing Folder in File Manager, Redact Selection, Change Search Engine… |
| 8 | File › Print…, Print Now; View › Toggle Full Screen Mode, Post‑It, Distraction Free Mode; View › Move/Clone › Move to / Open in New Instance; ? › Command Line Arguments…, Debug Info…; the About dialog (port of AboutDlg); localization of the dialogs |
| 9 | Plugins › Open Plugins Folder…; Settings › Import › Import plugin(s)… (Linux `.so` plugins) |
| 10 | Packaging and CI (no menu items) |

## Not ported: removed from the menus

These are Windows-only features with no Linux equivalent (PROMPT.md §6). They are removed at menu construction by
`isRemovedOnLinux` in `ui/Notepad_plus.cpp`, not shown greyed out:

- **File › Open Containing Folder › PowerShell** (also in the tab context menu): the Terminal item opens the
  desktop's terminal.
- **? › Update Notepad++**, **? › Set Updater Proxy…**: the WinGUp auto-updater; updates come from the package
  manager. Upstream also removes these items when no updater is installed.

Renamed for Linux (`linuxMenuText`): Explorer → File Manager, cmd → Terminal, Move to Recycle Bin → Move to Trash,
Read‑Only Attribute in Windows → in File System, Read‑Only in Notepad++ → in NotePadLX, About Notepad++ → About
NotePadLX. Help menu: "NotePadLX on GitHub" (https://github.com/ClaudiuJitea/NotePadLX, the CMake `HOMEPAGE_URL`)
and "Report an Issue" (its `/issues`) replace the Notepad++ Home and Project Page entries. The Notepad++ Online User
Manual and Community (Forum) entries are removed. The `winKeyDefs` table keeps the removed command IDs so that
`shortcuts.xml` files written by Notepad++ still load.

## Session 7b (2026‑10‑25): defaults and polish

- **NotePadLX defaults** (`data/config.model.xml`, copied on the first run like upstream's `config.model.xml`):
  - dark mode with the Solarized theme;
  - no session snapshot, so quitting with unsaved changes asks to save (upstream default: snapshot mode, which
    silently backs the text up).
- **Dark mode setting:**
  - `nplx::style::initDarkMode()` ports upstream `NppDarkMode::initDarkMode()`: the UI follows config.xml
    `DarkMode enable`;
  - `enableWindowsMode` follows the desktop colour scheme; `NPLX_THEME=light|dark` still overrides.
- **Panel colours:** `Notepad_plus::init` now sets the current default fg/bg from the theme's Default Style, as
  upstream does, so the panels follow the editor theme from the start.
- **Menus:**
  - an elevated surface, 10px radius and a subtle hover in place of the accent bar;
  - roomier rows and a 12px left margin;
  - the check-mark gutter only appears in menus that have checkable items (the hidden toolbar icons no longer
    reserve a column);
  - menus taller than the screen scroll instead of splitting into columns.
- **Dialogs:**
  - RcDialog uses fractional dialog units (the integer ones made dialogs too small for their text) and adds a 6px
    outer padding to top-level dialogs;
  - narrow `.rc` buttons (`...`, `+`) get a compact style;
  - message boxes are wider.
- **Tests:** a new close-with-unsaved-changes prompt test; the theme expectations are now Solarized; the look test
  also screenshots the File/Edit menus and the Run and Go To dialogs.

## Session 7 (2026‑10‑25): Phase 7 — macros and the Run menu

- **Macro recording and playback** (`ui/NppMacro.cpp`):
  - ported from upstream: `macroPlayback` (one undo action per affected document, closed through
    `_invisibleEditView`), `checkMacroState`, `addCurrentMacro`, the Start/Stop/Toggle/Playback/Save/Run-multiple
    commands, and `WM_MACRODLGRUNMACRO` (run N times, or until the end of the file);
  - the recording switch at the end of `command()` is taken verbatim; `SCN_MACRORECORD` keeps the EOL
    normalisation (`SCI_NEWLINE`);
  - `recordedMacroStep::PlayBack` became `Notepad_plus::playBackMacroStep`. The `SCN_CHARADDED` it sends upstream only
    reaches plugins (`NPLX_TODO(Phase 9)`);
  - `SCN_CHARADDED` auto-indent and auto-completion are now skipped while recording or playing back, as upstream;
  - Find/Replace steps are recorded through `WM_FRSAVE_INT/STR` and replayed by `FindReplaceDlg::execSavedCommand`;
    all 14 upstream `saveInMacro` call sites are restored.
- **Dialogs** (all built by RcDialog from the upstream `.rc` files; `ui/RcShortcutRunDialogs.inc`):
  - `ShortcutDlg`: the dialog half of upstream `Shortcut` (`IDD_SHORTCUT_DLG`). It shows the conflict warning and
    uses the upstream named-key list (`namedKeyCount/Name/Id` in `core/shortcut`);
  - `RunMacroDlg`: modeless, as upstream;
  - `RunDlg` + `Command` (Run > Run..., Save..., the `$(VAR)` "+" menu):
    - Linux execution: `extractArgs` and `expandNppEnvironmentStrs` are ported; `%VAR%` plus POSIX `$VAR`/`${VAR}`
      replace `ExpandEnvironmentStrings`;
    - `ShellExecute` becomes a detached `QProcess` for an executable (path or `PATH`), else `QDesktopServices`
      for a file or URL;
    - the "ShellExecute - ERROR" message is kept on failure.
- **Dynamic Macro and Run menus:**
  - `DynamicMenu::attach` works on the `NppMenu` view of the Macro and Run menus (posBase 6 and 4, as upstream);
  - `updateDynamicMenuShortcuts` replaces `_accelerator.updateShortcuts()`;
  - "Modify Shortcut/Delete ..." stays disabled until the Shortcut Mapper is ported.
- **shortcuts.xml security:**
  - the HMAC check runs before ID_MACRO / ID_USER_CMD / Run-multiple (`isShortcutsXmlSafe`);
  - Run > Validate shortcuts.xml is ported;
  - `saveShortcuts()` runs at exit before config.xml is written, so the new HMAC is saved.
- **nppMessage:** NPPM_GETFULLCURRENTPATH, GETCURRENTDIRECTORY, GETFILENAME, GETNAMEPART, GETEXTPART, GETCURRENTWORD,
  GETCURRENTLINESTR, GETNPPDIRECTORY, GETNPPFULLFILEPATH, GETCURRENTLINE, GETCURRENTCOLUMN, GETCURRENTMACROSTATUS,
  INTERNAL_FINDKEYCONFLICTS.
- **Key conflicts:** `findKeyConflicts` searches menu commands, macros, Run commands and Scintilla keys until
  `ShortcutMapper::findKeyConflicts` is ported (`NPLX_TODO(Phase 7)`).
- **Tests:** `tests/tst_macro.cpp` (14 cases): record/playback/undo, EOL normalisation, Find steps, Shortcut dialog
  conflict, saving a macro and running it from the menu, run ×3 and until EOF, `$(VAR)` and env expansion, running
  commands, Run dialog + Save to the Run menu, the tampered shortcuts.xml check, shortcuts.xml + HMAC written.
  12/12 suites pass.
- **Next (Phase 7):** Shortcut Mapper, Preferences, column editor, hash tools (MD5/SHA), Base64, and the remaining
  Edit/Tools dialogs.

## Session 6 (2026‑10‑25): NotePadLX look

- **Own icon set** (`resources/nplx-icons`, 51 SVG line icons drawn for NotePadLX, same names as upstream toolbar
  icons): toolbar, document tab states, message boxes, widget glyphs. `NplxIconEngine` recolours the templates for
  the light/dark theme and the disabled state and renders them crisp at any size / HiDPI.
- **Toolbar**: 24 px icons (18 px when upstream's small toolbar setting is chosen), flat rounded buttons, checked
  state tinted, overflow chevron.
- **App style** (`src/ui/NplxStyle.*`): Fusion-based proxy style without the stock dialog-button icons (no green ✓
  on OK), design tokens for light and dark, and a stylesheet for buttons (accent default button), inputs, combos,
  check/radio boxes, menus (rounded, accent selection), tabs (accent underline, hover close button), group boxes,
  sliders, scrollbars (thin), status bar, dock widgets and tooltips. Follows the system colour scheme (live), or
  upstream dark mode; `NPLX_THEME=light|dark` forces one.
- The editor colours still come from the selected upstream theme (Settings → Style Configurator), as upstream.
- **Tests**: `tst_look` (icons render, stylesheet complete, no stock button icons, screenshots in both themes).

### Phase 6: docking panels (done)
- **Docking**: `DockingPanel` (QDockWidget) replaces DockingDlgInterface/DockingManager; panels tab together per
  side, float, and close through their menu command. Open panels are saved in config.xml exactly like upstream
  (`DockingManager/PluginDlg`), the exact Qt layout in `dockingLayout.dat`; both are restored at start-up.
- **Function List** (`src/editor/functionParser.*` upstream code, `src/ui/FunctionListPanel.*`, `TreeView.*` adapter):
  tree, sort, search, mark entry at the caret, open entry, per-file folding state, preferences menu, theme colours,
  `-export=functionList`. **All 47 upstream `Test/FunctionList` cases match the upstream expected results**
  (`tst_functionlist`, run on CRLF copies like upstream's Windows CI).
- **Document List** (`VerticalFileSwitcher.*`): columns, groups per view, sort, activate / close / multi-select menu.
- **Document Map** (`DocumentMap.*`): zoomed-out view of the document, the view zone (click, drag, wheel, keys),
  folding and wrapping follow the editor, theme colours (Style Configurator "Document map").
- **Clipboard History** (`ClipboardHistoryPanel.*`): QClipboard::dataChanged in place of the clipboard viewer chain;
  paste converts to the document's code page; encoding conversions are not recorded (upstream trackClipboardOps).
- **Character Panel** (`AnsiCharPanel.*`): the 256 characters with HTML names / numbers; ANSI = Windows-1252.
- **Folder as Workspace** (`FileBrowser.*`): roots, folders first, hidden (dot) folders skipped, symlinks skipped
  unless allowed, QFileSystemWatcher updates, locate / fold / expand, context menus (file manager, terminal, copy
  path / name, Find in Files, run by system), saved roots and expanded folders, `-openFoldersAsWorkspace`,
  Open (Containing) Folder as Workspace, dropped folders (new mode), user session roots.
- **Project Panels 1–3** (`ProjectPanel.*`): workspaces in the upstream XML format (Windows workspaces with `\` load),
  projects / folders / files, in-place rename, drag and drop, move up / down, add files / from directory, relocate,
  save / save as / reload, dirty mark, save prompt at exit. **Find / Replace in Projects** in the Find dialog.
- **Windows dialog** (`WindowsDlg.*`): numeric-aware sort by name / path / type / size / date, activate, save,
  close, sort tabs, type-ahead, copy names / paths; **Window menu** sort commands and document list.
- **Ctrl+Tab document switcher** (`TaskListDlg.*`, MRU order) and Ctrl+Tab / Ctrl+Shift+Tab next / previous;
  shortcuts of commands without a menu item now work (hidden window actions). Closing a tab activates the most
  recently used document (`_styleMRU`).
- **UDL dialog docking**: Dock / Undock puts the dialog in a dock on the right; status saved like upstream.
- **Desktop integration** (`DesktopIntegration.*`): file manager (FileManager1 D-Bus), terminal ($TERMINAL,
  x-terminal-emulator, GNOME/KDE/... terminals), default application; "Open Containing Folder > Terminal" ported.
- **Fixes**: User Defined Languages were not saved at exit; `TreeView::selectItem` now reveals the item (Win32
  TVM_SELECTITEM); the ported-commands generator also counts `IDC_*` commands.
- **Tests**: 11 suites, all pass (`tst_panels` 14 cases, `tst_functionlist` 52, `tst_styles` 11...).
- **Next**: Phase 7 (Preferences, Shortcut Mapper, macros, Run, column editor, hash tools), then the Phase 3/4
  leftovers (incremental search, Find in Finder, elevated save, tab colours).

## Session 5 (2026‑10‑25): Phase 5 started, Phase 3/4 leftovers

- **Go to Line** dialog (Ctrl+G, line and offset modes), **Summary**, **Monitoring** ("tail -f": QFileSystemWatcher
  in place of the ReadDirectoryChanges thread, reloads at most every 250 ms and scrolls to the end),
  **synchronized scrolling** (vertical/horizontal, driven by SCN_PAINTED like upstream).
- **Auto-completion and call tips** (`src/editor/AutoCompletion.*`, `FunctionCallTip.*`, upstream code): word,
  API (data/autoCompletion/*.xml), path (Unix paths) completion, function call tips, matched pairs, and
  **auto-indent** (`maintainIndentation`), all on SCN_CHARADDED as upstream.
- **Clickable links** (`addHotSpot`, `src/editor/UrlDetection.*`): QUrl replaces InternetCrackUrl. The 148 cases of
  upstream `Test/UrlDetection` run as `tst_urldetection` and match upstream's own expected results (including its
  2 known failures). Double-click opens a link; Ctrl+double-click selects between delimiters.
- **Style Configurator** (`src/ui/WordStyleDlg.*`, `ColourPicker.*`): themes, languages, styles, colours (left click
  picks, right click toggles like upstream), fonts, bold/italic/underline, user keywords and extensions, global
  override, Save & Close, Cancel restores. Themes are discovered from `data/themes` and the user's `themes` folder
  (upstream Notepad_plus_Window::init); **all 22 bundled themes load and render** (`tst_styles`).
- **Fixes found on the way**: `string2wstring`/`wstring2string` converted past the first NUL (upstream stops there);
  checkable menu items toggled twice (QAction toggles before the command; now Win32 semantics).
- **User Defined Language dialog** (`src/ui/UserDefineDialog.*`, upstream code): the 4 pages (Folder & Default,
  Keywords Lists, Comment & Number, Operators & Delimiters), Styler popup (fonts, colours, transparency, nesting;
  Cancel restores), Create new / Save as / Rename / Remove / Import / Export, ignore case, transparency. The Language
  menu follows (add, rename, remove), and open documents in a renamed or removed UDL are updated
  (WM_RENAME_USERLANG / WM_REMOVE_USERLANG). The bundled markdown UDLs are installed on first run.
  Not ported yet: docking the dialog in the main window (Dock button disabled, Phase 6), localization (Phase 8).
- **RcDialog** (`src/ui/RcDialog.*`, `tools/gen_rcdialogs.py`): builds Qt dialogs from the upstream `.rc` DIALOGEX
  resources (same controls, IDs and positions; notifications reach `onCommand(id, code)` like WM_COMMAND). The UDL
  dialogs use it; Preferences (Phase 7) will too. Regenerate with
  `tools/gen_rcdialogs.py ../notepad-plus-plus/PowerEditor/src src/ui/RcDialogs.inc IDD_...`.
- **Fixes found on the way**: the `_itoa` shim in the vendored `LexUser.cxx` aborted (`_FORTIFY_SOURCE` buffer
  overflow) on the first styling of any UDL document with text; it now writes only the digits.
- **Tests**: 8 suites, all pass (commands 24, findreplace 15, mainwindow 16, styles 10, urldetection 150...).
- **Next**: incremental search, Find in Finder, then Phase 6 (docking panels: document map, function list, folder as
  workspace, docked UDL dialog).

## Session 4 (2026‑10‑25): Find/Replace, context menus, ~200 more commands

- **Find / Replace** (`src/ui/FindReplaceDlg.*`, port of upstream FindReplaceDlg): the search engine
  (`Searching`, `processFindNext`, `processReplace`, `processAll`, `processRange`) is upstream code; the dialog is a
  QDialog whose controls are kept in a map by their upstream control IDs, so the `WM_COMMAND` handlers port almost
  line for line (`FindReplaceDlg::command(id)`). Tabs Find / Replace / Find in Files / Mark, history combos, search
  modes, in-selection, wrap, backward, 2-buttons mode, swap button, transparency, coloured status bar with tooltips.
  Find in Projects is hidden until project panels exist (Phase 6).
- **Finder** (search results): QDockWidget with a ScintillaEditView using the upstream `searchResult` lexer
  (`@MarkingsStruct`), folding, F4/Shift+F4, double-click / Enter to open a result, Delete, upstream context menu.
- **Search in documents and files** (`src/ui/NppSearch.cpp`): findInCurrentFile, findInOpenedFiles, findInFiles,
  replaceInOpenedFiles, replaceInFiles, getMatchedFileNames (QDir; hidden = dot files; symlinked folders not followed),
  upstream filter syntax (`*.txt !\sub !+\log*`), QProgressDialog in place of the upstream Progress window.
- **Smart highlighting** (`SmartHighlighter`), **XML tag matching** (`xmlMatchedTagsHighlighter`), **brace matching**,
  Mark styles 1–5, jump to marks, copy styled text: wired on SCN_UPDATEUI like upstream.
- **Context menus**: editor menu from `contextMenu.xml` (resolved against the menu bar through `QtNppMenu`, the
  NppMenu adapter), tab menu with the upstream default items and states; `ContextMenu` ported on QMenu.
- **Commands** (`NppCommands.cpp`, helpers in `NppEdit.cpp`): sort lines (all upstream sorters; locale sort via
  QCollator in `SortLocale`), trim / tab↔space / EOL→space, remove empty / duplicate lines, block and stream comments,
  bookmarked lines (cut/copy/paste/delete/inverse), multi-select, Close Multiple Tabs, tab navigation and moving,
  fold levels, hide lines, date/time insert, read-only for all docs, file read-only attribute (chmod),
  character sets and Convert to…, search on Internet, copy all names/paths, change history navigation, RTL/LTR,
  View in browser (looked up in PATH), Help links, always on top.
- **The menu enables exactly the ported commands**: `cmake/GenPortedCommands.cmake` collects the `case IDM_*` labels of
  `Notepad_plus::command` at build time (293 commands now). Undo/Redo/Cut/Copy/Paste follow the document state
  (upstream checkClipboard / checkUndoState).
- **Fixes**: multiple selection was never enabled (upstream sends NPPM_INTERNAL_SETMULTISELECTION in init); the
  multi-document save prompt now goes through UiServices.
- **Tests**: 6 suites, all pass. New: `tst_findreplace` (15) and `tst_commands` (17); `tst_mainwindow` 16.
- **Next**: Go to Line dialog, incremental search, Summary, monitoring (tail -f), synchronized scrolling, full screen /
  post-it / distraction free, theme switching, auto-completion and function call tips (Phase 5), Find in Finder.

## Session 3 (2026‑10‑25): Phase 3 + toolbar

- **Sessions** (`src/ui/NppSession.cpp`, port of loadSession / getCurrentOpenedFiles / saveSession): the last
  session is restored at start and saved at exit with positions, bookmarks, languages, encodings, fold states;
  snapshot mode backs up dirty documents every `_snapshotBackupTiming` ms (QTimer in place of the
  backupDocument thread) and restores unsaved/untitled documents from `~/.config/notepadlx/backup/`.
- **Exit** follows upstream WM_CLOSE: session captured before closing, recent list locked, config + history saved.
- **Command line** (`src/app/CmdLine.*`): upstream options (-multiInst, -nosession, -ro, -n -c -p -l -L -x -y,
  -udl=, -titleAdd=, -settingsDir=, -openSession, -r, -fullReadOnly...), `--help`, `--version`;
  upstream loadCommandlineParams applies them.
- **Single instance** (`src/app/SingleInstance.*`): QLocalServer per user + settings dir; a second launch
  passes its files/options as JSON and exits (verified with two processes: handed over in 40 ms).
- **File changes outside the app**: checked on application activation (upstream WM_ACTIVATEAPP →
  NPPM_INTERNAL_CHECKDOCSTATUS): reload prompt, deleted-file prompt.
- **Recent files** (`src/ui/LastRecentFileList.*`): list logic as upstream; entries before "Exit" or in a sub-menu;
  Restore Recent Closed File / Open All / Empty list.
- **Rename** (file or untitled tab), **Move to Trash** (freedesktop trash), **drag and drop** of files.
- **Toolbar**: upstream button order with the Fluent icons (light/dark), sharing the menu actions.
- **SCN_MODIFIED** handling (needed by the backup system), change-history margin shown.
- **Tests**: 4 suites, all pass (mainwindow now 15 tests).
- **Still open in Phase 3**: monitoring ("tail -f"), elevated save (pkexec), network-path timeouts,
  -notabbar / -systemtray / -alwaysOnTop on Wayland, individual tab colours, pinned tabs.

## Session 2 (2026‑10‑24): Phase 1 done, Phase 2 started

- **Core (`src/core`, no Qt, no windows.h):** ported upstream Parameters (full), Common (portable subset),
  Utf8_16, EncodingMapper, shortcut data, localization lookups, NppDarkMode colours, hmac (machine-id),
  plus Linux replacements: iconv encodings (+ built-in CP720 table), XDG paths and portable mode,
  POSIX file I/O (in-place save keeps mode and inode), file attributes, NppMenu interface, UiServices.
- **Editor (`src/editor`):** full port of upstream ScintillaEditView (Qt events replace ScintillaProc),
  Buffer/FileManager (trash via QFile::moveToTrash), Sorters.
- **UI (`src/ui`):** Qt Notepad_plus (two views, DocTabView tabs with upstream icons, status bar,
  notifications, bookmarks), NppIO (open/save/save as/close/close all/reload), NppCommands
  (~80 commands), menus generated from upstream Notepad_plus.rc by `tools/gen_mainmenu.py`
  (581 commands; unported ones are disabled), default shortcuts from shortcuts.xml, interim FindBar.
- **App icon:** resources/icons (from icons/NotePadLX.jpeg), .desktop file, install rules.
- **Tests:** 4 suites pass (phase0 9, core 17, filemanager 15, mainwindow 10).
- **Next (Phase 2 continued):** session save/restore, recent files, command line + single instance
  (Phase 3 items), toolbar, the rest of NppCommands, context menus (NppMenu on QMenu), theme loading
  (ThemeSwitcher), brace/tag matching, smart highlighting.
- **Porting traps found:** wide `%s` must become `%ls`; `%lX` with DWORD; `WM_UNDO/WM_CUT...` must be
  `SCI_*`; `isTransparentAvailable()`/winVer checks; change-history margin must be shown or its
  markers draw as line backgrounds; `EolType::osdefault` set to unix.

---

## Build

### Dependencies

Ubuntu 24.04+ / Debian 13+ (verified on Ubuntu 26.04: Qt 6.10.2, GCC 15.2, CMake 4.4):

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-5compat-dev qt6-svg-plugins libgl-dev
```

Fedora 41+ (package names only; not yet verified by a build):

```sh
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qt5compat-devel qt6-qtsvg mesa-libGL-devel
```

`qt6-5compat-dev` / `qt6-qt5compat-devel` is required: Scintilla's Qt backend uses `QTextCodec`.
`qt6-svg-plugins` / `qt6-qtsvg` (runtime only) renders the NotePadLX SVG icons; `tst_look` fails without it.
Boost is **not** a system dependency; the header‑only Boost.Regex in `third_party/boostregex` is used.

### Commands

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/notepadlx tests/data/sample.cpp
```

Options: `-DNPLX_BUILD_TESTS=OFF`.

---

## Phase 0: what was done

- **CMake skeleton** (C++20, Qt ≥ 6.5): `third_party/` → static `lexilla` and `scintilla_qt`
  (Scintilla core + `ScintillaEditBase` + Boost regex), `src/` → `nplx_editor` lib + `notepadlx`
  app, `tests/`.
- **Vendored** the N++‑patched Scintilla 5.6.6, Lexilla 5.5.3 (with the custom lexers `user`,
  `searchResult`, `objc`), and Boost regex. Normalised to LF. The three small NPLX patches are
  listed in UPSTREAM.md.
- **Boost regex hooked into Qt Scintilla:** built with `SCI_OWNREGEX`, plus
  `SCI_GETBOOSTREGEXERRMSG` ported from `ScintillaWin.cxx` into `ScintillaQt.cpp`.
- `src/editor/ScintillaEditView`: Phase‑0 subset of the upstream class with the same
  `execute()` direct‑function entry point, lexer selection via `CreateLexer`, upstream C++
  keywords and colours, file loading, and regex search with error messages.
- `src/ui/MainWindow`: File → Open/Exit, Search → Find (Ctrl+F) / Find Next (F3) with a
  regex find bar (wraps around; invalid patterns show the Boost error in the status bar),
  and a status bar with language, Ln/Col/Sel.
- `src/app/main.cpp`: `notepadlx [file]`, `--help`, `--version`.

### Verification (2026‑10‑24)

- Build: all 197 targets compile with no errors. Our code (`-Wall -Wextra`) has no warnings.
  Vendored code is compiled with `-w`.
- `ctest`: **1/1 passed**. `tst_phase0`: **9 passed, 0 failed**:
  Lexilla has ≥128 lexers including `user`/`searchResult`/`objc`; C++ styles (keyword, type,
  number, operator, comment, preprocessor, string); `.cpp` load picks the `cpp` lexer;
  lookbehind `(?<=foo)bar` matches (proves Boost, since Scintilla's built‑in engine lacks it);
  `\b`, `[[:xdigit:]]{2}`, backreference `(\w+) \1`, match case / ignore case, `^` anchor;
  UTF‑8 (`ü` = 2 bytes, `.` matches one code point); invalid `(abc` returns ‑2 with the message
  "Unmatched marking parenthesis ( or \(…" and the next valid search clears it.
- Manual: the app runs on Wayland (Ubuntu 26.04). An offscreen render showed upstream colours
  and a regex find‑bar match.

### Known issues / notes

- On launch, Qt logs `qt.qpa.services: Failed to register with host portal … Connection already
  associated with an application ID`. It appears when started from a terminal whose D‑Bus
  connection already has an app ID. Harmless; revisit with the `.desktop` file in Phase 10.
- No `.desktop` file is installed yet, so `setDesktopFileName("notepadlx")` has no icon to match.

### Stubs (tagged `NPLX_TODO` in code)

- `ScintillaEditView::applyCppStyles` holds hard‑coded C++ keywords and colours → Phase 1/5
  (`langs.xml`, `stylers.xml`).
- `lexerNameForFile` holds a hard‑coded C/C++ extension list → Phase 1 (`langs.xml` `ext=`).
- `loadFile` reads raw bytes as UTF‑8 → Phase 1 (`FileManager::loadFileData`: encodings, BOM, EOL).
- `MainWindow` menus and find bar are minimal → Phase 2 (`IDM_*` menu tree) and Phase 4 (`FindReplaceDlg`).
- Command line → Phase 3. No save yet → Phase 1/3.

### Next step

Phase 1: port `Parameters` (pugixml, XDG paths, portable mode), `Buffer`/`FileManager`,
`Utf8_16`, `EncodingMapper` (iconv), and `localization`. Vendor pugixml, json, and uchardet
into `third_party/`. Copy the `data/` files from `installer/`.
