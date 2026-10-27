# Writing NotePadLX plugins

NotePadLX loads plugins built for Linux: shared libraries with the same entry points as a Notepad++ plugin. A
Windows plugin (`.dll`) cannot load; its source can be rebuilt against the NotePadLX headers, usually with small
changes (strings in UTF-8, no `HWND`/`SendMessage`, Qt in place of Win32 dialogs).

## Where plugins live

```
~/.local/share/notepadlx/plugins/<Name>/<Name>.so     the user's plugins ($XDG_DATA_HOME/notepadlx/plugins)
<prefix>/lib/notepadlx/plugins/<Name>/<Name>.so       system-wide (e.g. /usr/lib/notepadlx/plugins)
```

`NPLX_PLUGIN_PATH=dir1:dir2` adds roots (useful while developing). When two roots have the same `<Name>.so`, the
user's wins. Plugin settings go to `NPPM_GETPLUGINSCONFIGDIR` (`~/.local/share/notepadlx/plugins/Config`).

**Settings › Import › Import plugin(s)…** copies a `.so` into the user folder; **Plugins › Open Plugins Folder…**
opens it. `notepadlx -noPlugin` starts without plugins. **? › Debug Info** lists the loaded plugins.

## The ABI

Include `PluginInterface.h` (installed in `<prefix>/include/notepadlx`, or `src/plugins` in this repository) and
export six functions:

```cpp
#include "PluginInterface.h"

NPLX_PLUGIN_EXPORT int getAbiVersion() { return NPLX_PLUGIN_ABI_VERSION; }
NPLX_PLUGIN_EXPORT void setInfo(NppData data);          // keep data: the handles and the message functions
NPLX_PLUGIN_EXPORT const char* getName();               // UTF-8, the name of the Plugins sub-menu
NPLX_PLUGIN_EXPORT FuncItem* getFuncsArray(int* nb);    // the menu items
NPLX_PLUGIN_EXPORT void beNotified(SCNotification* n);  // NPPN_* and SCN_* notifications
NPLX_PLUGIN_EXPORT LRESULT messageProc(UINT msg, WPARAM w, LPARAM l);
```

Differences from the Notepad++ ABI:

| Notepad++ | NotePadLX |
|---|---|
| `NppData` holds three `HWND`s | three opaque `NppHandle`s plus `_nppSend` and `_sciSend` |
| `::SendMessage(nppData._nppHandle, NPPM_X, w, l)` | `nppData._nppSend(nppData._nppHandle, NPPM_X, w, l)` |
| `::SendMessage(nppData._scintillaMainHandle, SCI_X, w, l)` | `nppData._sciSend(nppData._scintillaMainHandle, SCI_X, w, l)` (`_nppSend` also accepts a Scintilla handle) |
| `getName()` and `FuncItem::_itemName` are `wchar_t` | UTF-8 `char` |
| `isUnicode()` | `getAbiVersion()` |
| messages with `wchar_t*` strings | still work with the Linux (32-bit) `wchar_t`; each has a `NPPM_*_UTF8` variant (end of `Notepad_plus_msgs.h`) |
| `NPPM_DMMREGASDCKDLG` with an `HWND` dialog | a `QWidget*` (`PluginDockingQt.h`) |
| `NPPM_ADDTOOLBARICON_FORDARKMODE` with `HBITMAP`/`HICON` | image file paths (SVG or PNG) |
| `NPPM_GETMENUHANDLE` returns an `HMENU` | a `QMenu*` (Plugins menu) or the `QMenuBar*` |
| `NPPN_CMDLINEPLUGINMSG`: `idFrom` is a `wchar_t*` | a UTF-8 `char*` |

The `NPPM_*` message numbers and meanings are those of Notepad++ (the API of the version in
`NPPM_GETNPPVERSION`, 8.9.8). The notifications `NPPN_READY`, `NPPN_TBMODIFICATION`, `NPPN_FILE*` (open, save,
close, rename, delete), `NPPN_BUFFERACTIVATED`, `NPPN_LANGCHANGED`, `NPPN_READONLYCHANGED`, `NPPN_DOCORDERCHANGED`,
`NPPN_WORDSTYLESUPDATED`, `NPPN_SHORTCUTREMAPPED`, `NPPN_DARKMODECHANGED`, `NPPN_GLOBALMODIFIED`,
`NPPN_NATIVELANGCHANGED`, `NPPN_TOOLBARICONSETCHANGED`, `NPPN_BEFORESHUTDOWN`, `NPPN_CANCELSHUTDOWN`,
`NPPN_SHUTDOWN` and every Scintilla notification of the two views (`hwndFrom` is the view's `NppHandle`) are sent.
All calls happen on the GUI thread.

A compatibility shim for code that calls `SendMessage`:

```cpp
#define SendMessage(h, m, w, l) nppData._nppSend((h), (m), (WPARAM)(w), (LPARAM)(l))
```

Not supported in 1.0: lexer plugins (the Lexilla exports of a plugin are ignored), Plugins Admin (no Linux plugin
list), `NPPM_DARKMODESUBCLASSANDTHEME` (Qt widgets follow the NotePadLX palette by themselves). A plugin that
crashes (SIGSEGV) takes NotePadLX down: there is no structured exception handling on Linux; a C++ exception thrown
by a plugin is caught and reported, as in Notepad++.

## Docking panels

```cpp
#include "PluginDockingQt.h"

static DockedWidgetData data;
data.hClient = new MyPanel();          // a QWidget without a parent
data.pszName = "My panel";
data.dlgID = 0;                        // index in the FuncItem array of the command that shows it
data.uMask = DWS_DF_CONT_RIGHT;        // or LEFT / TOP / BOTTOM, DWS_DF_FLOATING, | DWS_ADDINFO, | DWS_ICONTAB
data.pszModuleName = "MyPlugin.so";
nppData._nppSend(nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&data);
nppData._nppSend(nppData._nppHandle, NPPM_DMMSHOW, 0, (LPARAM)data.hClient);
```

The plugin links Qt 6 Widgets, the same major version NotePadLX runs with (the AppImage bundles its own Qt: build
against Qt 6.5 or later and install the plugin for the distribution package, or match the AppImage's Qt). When the
user closes the dock, the widget is hidden and the check mark of the `dlgID` menu item is removed. The side, the
floating state and the visibility are saved; at the next start NotePadLX runs the `dlgID` command again if the
panel was open.

## Building

CMake, out of the NotePadLX tree:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyPlugin CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Widgets)   # only for a docking panel
add_library(MyPlugin MODULE MyPlugin.cpp)
set_target_properties(MyPlugin PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
target_include_directories(MyPlugin PRIVATE /usr/include/notepadlx)
target_link_libraries(MyPlugin PRIVATE Qt6::Widgets)
```

Then copy `MyPlugin.so` to `~/.local/share/notepadlx/plugins/MyPlugin/`. In this repository the samples are built
with the rest (`-DNPLX_BUILD_SAMPLE_PLUGINS=ON`, the default) into `build/sample-plugins`:

```sh
NPLX_PLUGIN_PATH=$PWD/build/sample-plugins ./build/src/notepadlx
```

## The samples

- `plugins/samples/HelloWorld`: menu items with a default shortcut (Ctrl+Alt+Shift+H), a toolbar button with light
  and dark SVG icons, text editing through Scintilla, the UTF-8 messages, notification counters it reports to other
  plugins with `NPPM_MSGTOPLUGIN`. No Qt.
- `plugins/samples/DockingSample`: a Qt docking panel that shows the current file and its size, follows the
  document (`NPPN_BUFFERACTIVATED`, `SCN_UPDATEUI`) and converts the selection to upper case.

`tests/tst_plugins.cpp` loads both and checks the menus, shortcuts, toolbar, notifications, messages and the panel.

## Porting popular plugins

Most plugins are a thin Win32 shell around portable C++:

- **JSON Viewer** (`kapilratnani/JSON-Viewer`): the parser (RapidJSON) and formatter are portable; the tree view
  dialog becomes a Qt docking panel with a `QTreeWidget`; the menu commands (format, compress) only need
  `SCI_GETTEXT` / `SCI_SETTEXT`, which work as they are through `_sciSend`.
- **Compare** (`pnedev/comparePlus`): the diff engine is portable; it drives two Scintilla views with markers and
  indicators (`NPPM_ALLOCATEMARKER`, `NPPM_ALLOCATEINDICATOR` work), the navigation bar becomes a `QWidget`, its
  settings dialog a `QDialog`.
- **NppExec**: the console needs `QProcess` in place of the Windows pipes; the script engine is portable.

In each case: replace `wchar_t` plugin strings by UTF-8 (`getName`, `_itemName`), `SendMessage` by `_nppSend`, the
`.rc` dialogs by Qt widgets, `LoadImage` icons by image files, and export `getAbiVersion`.
