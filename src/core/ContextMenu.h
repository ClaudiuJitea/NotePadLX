// This file is part of NotePadLX, a Linux port of Notepad++.
// Copyright (C)2021 Don HO <don.h@free.fr>
// Modified for NotePadLX (Linux port).
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

// NPLX: data part of upstream WinControls/ContextMenu/ContextMenu.h (the menu item model read
// from contextMenu.xml / tabContextMenu.xml). The popup menu itself is a QMenu in ui/.

#pragma once

#include <string>

#include "NppConstants.h"
#include "Common.h"

struct MenuItemUnit final
{
	unsigned long _cmdID = 0;
	std::wstring _itemName;
	std::wstring _parentFolderName;

	MenuItemUnit() = default;
	MenuItemUnit(unsigned long cmdID, const std::wstring& itemName, const std::wstring& parentFolderName = std::wstring())
		: _cmdID(cmdID), _itemName(itemName), _parentFolderName(parentFolderName) {}
	MenuItemUnit(unsigned long cmdID, const wchar_t* itemName, const wchar_t* parentFolderName = nullptr) : _cmdID(cmdID)
	{
		if (!itemName)
			_itemName.clear();
		else
			_itemName = itemName;

		if (!parentFolderName)
			_parentFolderName.clear();
		else
			_parentFolderName = parentFolderName;
	}
};
