# Upstream sources

NotePadLX is ported from Notepad++. The upstream mirror lives (read‑only) in
`../notepad-plus-plus/`.

| Component | Upstream | Version | Vendored to |
|---|---|---|---|
| Notepad++ | `notepad-plus-plus/` | commit `cda2acf54` (2026‑10‑23, "Fix initially hidden WindowsDlg listview item selected") | ported progressively into `src/` |
| Scintilla | `notepad-plus-plus/scintilla/` (**N++‑patched**) | 5.6.6 | `third_party/scintilla/` (`include/`, `src/`, `call/`, `qt/ScintillaEditBase/`) |
| Lexilla | `notepad-plus-plus/lexilla/` (**N++‑patched**, plus `LexUser`, `LexSearchResult`, `LexObjC`) | 5.5.3 | `third_party/lexilla/` (`include/`, `lexlib/`, `lexers/`, `src/Lexilla.cxx`) |
| Boost.Regex search | `notepad-plus-plus/boostregex/` (header‑only Boost.Regex v5, standalone) | as in N++ | `third_party/boostregex/` |

Not vendored on purpose: `scintilla/win32`, `scintilla/gtk`, `scintilla/cocoa`, docs, tests,
and the `.mak`/`.pro`/`.vcxproj` build files.

All vendored text files were converted from CRLF to LF. Compare with upstream using
`diff -r --strip-trailing-cr`.

## Upstream Notepad++ patches (already in the vendored copy)

Find them by diffing against the pristine releases in
`notepad-plus-plus/PowerEditor/scintilla.original.forUpdating/{scintilla566,lexilla553}.zip`:

- Scintilla: `src/Editor.cxx`, `src/EditView.cxx`, `src/Indicator.{h,cxx}`,
  `include/{Scintilla.h, ScintillaMessages.h, ScintillaStructures.h, ScintillaTypes.h, Sci_Position.h}`,
  and the added `include/BoostRegexSearch.h` (`SCI_GETBOOSTREGEXERRMSG` = 5000,
  `INDIC_EXPLORERLINK`, 32‑bit range APIs removed in favour of `*FULL`).
- Lexilla: `lexlib/StyleContext.{h,cxx}`, `lexlib/WordList.h`, `src/Lexilla.cxx`
  (registers the custom lexers), `include/{SciLexer.h, LexicalStyles.iface, Lexilla.h}`.
- `win32/ScintillaWin.cxx`: Boost regex error‑message hook, charset fix (#5671), and MinGW
  defines. **Only the regex hook applies on Qt and is ported; see below.**

## NotePadLX patches on top of the vendored code

Every change is tagged `NPLX` in the source. Re‑apply them after each upstream update.

| File | Change |
|---|---|
| `scintilla/include/Sci_Position.h` | `#include <stdint.h>` (N++'s `Sci_PositionCR` uses `intptr_t`; MSVC pulled it in implicitly) |
| `scintilla/qt/ScintillaEditBase/ScintillaQt.cpp` | `#include "BoostRegexSearch.h"` and a `Message::GetBoostRegexErrmsg` case in `WndProc`, copied from N++ `ScintillaWin.cxx` |
| `lexilla/lexers/LexUser.cxx` | `<windows.h>` only on `_WIN32`; add `<string.h>`/`<stdio.h>`; `_itoa` shim (writes only the digits: callers pass the tail of a small buffer) |
| `scintilla/qt/ScintillaEditBase/ScintillaEditBase.pro`, `boostregex/*.mak` | removed (CMake replaces them) |

Build flags that replace N++'s makefiles: `SCI_OWNREGEX` (public, use Boost instead of the
built‑in regex), `BOOST_REGEX_STANDALONE`, `SCINTILLA_QT=1`, `MAKING_LIBRARY=1`.

## Updating to a newer Notepad++

1. Pull upstream in `../notepad-plus-plus`. Note the new commit here.
2. If the Scintilla or Lexilla version changed: re‑copy the directories listed above,
   convert CRLF to LF, and re‑apply the NPLX patches from the table.
3. For ported app code: `git -C ../notepad-plus-plus diff <old>..<new> -- PowerEditor/src`
   and merge the changes into the matching files under `src/`.
