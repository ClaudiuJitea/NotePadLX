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

// NPLX: the desktop services upstream gets from the Windows shell (Explorer, cmd.exe, ShellExecute).

#pragma once

#include <QString>

namespace nplx::desktop
{
	// upstream openInExplorerAndSelect: the file manager showing the file selected (freedesktop FileManager1 D-Bus
	// interface), or its folder
	void showFileInFileManager(const QString& filePath);

	// upstream ShellExecute "explore" on a folder
	void openFolder(const QString& folderPath);

	// upstream Command(L"%COMSPEC%").run(nullptr, dir): a terminal in the folder ($TERMINAL, x-terminal-emulator, then
	// the usual desktop terminals). Returns false when no terminal was found.
	bool openTerminal(const QString& folderPath);

	// upstream ShellExecute "open": the file's default application
	bool openWithSystem(const QString& filePath);
}
