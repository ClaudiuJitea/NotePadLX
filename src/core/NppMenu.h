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

// NPLX: a menu as seen by ported core code, in place of a Win32 HMENU. Positions and texts
// follow Win32 conventions (0-based positions, '&' mnemonics, "\t" before the shortcut text),
// so upstream algorithms (DynamicMenu, context menu lookup by menu path) port unchanged.
// ui/ implements it on QMenu / QMenuBar.

#pragma once

#include <string>

class NppMenu
{
public:
	virtual ~NppMenu() = default;

	virtual int itemCount() const = 0;
	virtual std::wstring itemText(int pos) const = 0;
	virtual NppMenu* subMenu(int pos) const = 0;  // nullptr if the item is not a sub-menu
	virtual int itemId(int pos) const = 0;        // command id; -1 for separators and sub-menus

	// pos == -1 appends. Win32: InsertMenu(MF_BYPOSITION ...)
	virtual NppMenu* insertSubMenu(int pos, const std::wstring& text) = 0;
	virtual void insertItem(int pos, int cmdId, const std::wstring& text) = 0;
	virtual void insertSeparator(int pos) = 0;
	virtual void removeItem(int pos) = 0;          // Win32: DeleteMenu(MF_BYPOSITION)
};
