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

// NPLX: the language table of upstream ScintillaEditView (ScintillaEditView::_langNameInfoArray),
// moved to core because NppParameters needs it. ScintillaEditView refers to the same array.

#pragma once

#include "Notepad_plus_msgs.h"

struct LanguageNameInfo {
	const wchar_t* _langName = nullptr;
	const wchar_t* _shortName = nullptr;
	const wchar_t* _longName = nullptr;
	LangType _langID = L_TEXT;
	const char* _lexerID = nullptr;
};

extern LanguageNameInfo g_langNameInfoArray[L_EXTERNAL + 1];
