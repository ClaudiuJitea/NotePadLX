// This file is part of NotePadLX, a Linux port of Notepad++.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// NPLX: Linux locations, in place of GetModuleFileName / SHGetKnownFolderPath(%APPDATA%).
//
// Data directory (upstream "_nppPath": model XML files, themes, localization, ...), first match:
//   1. $NPLX_DATA_DIR
//   2. <exe dir>/data                     (portable / AppImage layout)
//   3. <exe dir>/../share/notepadlx       (installed: /usr/bin -> /usr/share/notepadlx)
//   4. the source tree data/ dir          (developer builds, set by CMake)
// User settings directory (upstream "%APPDATA%\Notepad++"): $XDG_CONFIG_HOME/notepadlx
// (~/.config/notepadlx), unless doLocalConf.xml is next to the executable (portable mode).

#pragma once

#include <string>

namespace nplx
{
	std::wstring executablePath();
	std::wstring executableDirectory();
	std::wstring dataDirectory();
	std::wstring currentDirectory();

	std::wstring xdgConfigHome();   // $XDG_CONFIG_HOME or ~/.config
	std::wstring xdgDataHome();     // $XDG_DATA_HOME or ~/.local/share
	std::wstring xdgCacheHome();    // $XDG_CACHE_HOME or ~/.cache
	std::wstring userConfigDirectory(); // xdgConfigHome()/notepadlx

	// Windows code page number of the locale charset (65001 for UTF-8 locales)
	int currentSystemCodepage();
}
