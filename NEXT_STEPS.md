# NotePadLX: what is left after 1.0 (pick-up point)

State on 2026‑10‑27: **version 1.0**, tag `v1.0`, release with the AppImage and `.deb`. Phases 0–10 of
[PROMPT.md](PROMPT.md) §5 are done, 19/19 test suites pass. Details per session: [PORTING_STATUS.md](PORTING_STATUS.md).

To find the remaining stubs: `grep -rn NPLX_TODO src`.

## 1. Not ported yet (optional, after 1.0)

- **Lexer plugins** (`src/plugins/PluginsManager.cpp`, `NPLX_TODO(Phase 9+)`): upstream `GetLexerCount` /
  `GetLexerName` / `CreateLexer` exports and the external lexer entries in the Language menu.
- **Plugins Admin**: needs a Linux plugin list (a JSON index of `.so` builds with SHA‑256 sums).
- Recompile popular plugins against the Linux ABI (JSON Viewer, Compare): see [docs/PLUGINS.md](docs/PLUGINS.md).

## 2. Not verified by hand yet (do once in the real GUI)

- The polkit password dialog of the elevated save (pkexec), with a root-owned file.
- A really hung NFS/SMB mount (the 3 s timeouts).
- Language switch in Preferences, right button + wheel, `-monitor` in the running app.

## Release checklist (for the next version)

Bump the version in `CMakeLists.txt`, `README.md` (badge + install lines), `packaging/notepadlx.1`, the metainfo
`<release>`, and `PORTING_STATUS.md`; run the tests (`QT_QPA_PLATFORM=offscreen ctest --test-dir build`);
`packaging/build-packages.sh all`; `sha256sum` into `dist/SHA256SUMS`; write
`dist/RELEASE_NOTES.md`; commit, tag, push; check that CI is green; `gh release create vX --target main` with the
AppImage, `.deb` and SHA256SUMS.
