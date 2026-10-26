# NotePadLX: what is left (pick-up point)

State on 2026‑10‑26: **version 0.9**, commit `9e8283b` on `main`, tag `v0.9`, release with AppImage + `.deb`.
Phases 0–8 done, 18/18 test suites pass. Details per session: [PORTING_STATUS.md](PORTING_STATUS.md); full plan:
[PROMPT.md](PROMPT.md) §5.

To find every stub in the code: `grep -rn NPLX_TODO src` (2 × Phase 4, 14 × Phase 9).

## 1. Find in Finder (Phase 4, the last item) — small

The Search results panel's context menu item "Find in these search results…" does nothing yet.

- Port upstream `FindInFinderDlg` (`../notepad-plus-plus/PowerEditor/src/ScintillaComponent/FindReplaceDlg.h:260`,
  its `.cpp` and the `IDD_FINDINFINDER_DLG` resource in `FindReplaceDlg.rc`) as an `RcDialog` like the other dialogs.
- `src/ui/FindReplaceDlg.cpp:1350` (`Finder::runCommand`, `NPPM_INTERNAL_FINDINFINDERDLG`): open the dialog.
- `src/ui/FindReplaceDlg.cpp:2480` (`FindReplaceDlg::createFinder`): create an extra, closable Finder
  (`_canBeVolatiled`) docked as its own panel, add it to `_findersOfFinder`. `FindersInfo`, `processAll(FindersInfo)`
  (`ProcessFindInFinder`) and `removeFinder` already exist.
- Localize it (`<FindInFinder>` in nativeLang, `changeDlgLang`) and register it for `retranslateDialogs()`.
- Test in `tests/tst_findreplace.cpp`: Find All → Find in these results → a new finder with the subset; close it.

## 2. Phase 9: plugin API for Linux — big

Plan in PROMPT.md §5 Phase 9. Steps:

1. `src/plugins/PluginInterface.h`: C ABI (`setInfo(NppData)`, `getName`, `getFuncsArray`, `beNotified`,
   `messageProc`), `NppData` with opaque handles + `nppSend` / `sciSend` function pointers. Strings are UTF‑8,
   `NPPM_*` keep their numbers, `NPPM_*_UTF8` variants where upstream passes `wchar_t*`.
2. Port `PluginsManager` (dlopen/dlsym): load `$XDG_DATA_HOME/notepadlx/plugins/<Name>/<Name>.so` and the system dir.
3. Plugins menu (the `nullptr` passed at `src/ui/Notepad_plus.cpp:607` and `:821`), plugin commands in the
   Shortcut Mapper (`NppShortcuts.cpp:131`, `ShortcutMapper.cpp:847`, `:986`).
4. Notifications (the `NPLX_TODO(Phase 9)` sites): NPPN_DARKMODECHANGED, NPPN_GLOBALMODIFIED, NPPN_FILEBEFORERENAME /
   NPPN_FILERENAMED, NPPN_LANGCHANGED, NPPN_DOCORDERCHANGED, macro relay (`NppMacro.cpp:278`), plus the standard
   ones (READY, FILEOPENED, FILESAVED, BUFFERACTIVATED, SHUTDOWN…).
5. Docking for plugins (`NPPM_DMMREGASDCKDLG`) through a Qt extension header that takes a `QWidget*`;
   toolbar icons (`Notepad_plus_msgs.h:322`, `:803`).
6. Menu items now greyed out: Plugins › Open Plugins Folder…, Settings › Import › Import plugin(s)….
   Debug Info lists the loaded plugins (`AboutDlg.cpp:335`).
7. Two sample plugins (Hello World, a docking one), a `tst_plugins` suite, a doc on recompiling popular plugins.
- **Accept:** the samples load, add menu items, receive notifications and edit text.

## 3. Phase 10: packaging and CI — the rest

Done: `.deb`, AppImage, `.desktop`, AppStream metainfo, man page (`packaging/build-packages.sh all` → `dist/`).
Left:
- Flatpak manifest (portal file dialogs must work).
- `.rpm` (CPack) and a Fedora build check (only the package names are listed).
- `MimeType=` in `notepadlx.desktop`, generated from the `langs.model.xml` extensions (file associations).
- GitHub Actions: build + tests on Ubuntu LTS and Fedora, clang‑tidy, an ASan/UBSan job.

## 4. Not verified by hand yet (do once in the real GUI)

- The polkit password dialog of the elevated save (pkexec), with a root-owned file.
- A really hung NFS/SMB mount (the 3 s timeouts).
- 0.9 features in the running app: language switch in Preferences, right button + wheel, `-monitor`.

## Release checklist (for 1.0 or the next version)

Bump the version in `CMakeLists.txt`, `README.md` (badge + install lines), `packaging/notepadlx.1`, the metainfo
`<release>`, and `PORTING_STATUS.md`; run the tests (`QT_QPA_PLATFORM=offscreen ctest --test-dir build`);
`packaging/build-packages.sh all`; `sha256sum` into `dist/SHA256SUMS`; write `dist/RELEASE_NOTES.md`;
commit, tag, push; `gh release create vX --target main` with the AppImage, `.deb` and SHA256SUMS.
