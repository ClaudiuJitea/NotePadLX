// This file is part of NotePadLX, a Linux port of Notepad++.
// Copyright (c)2021 adzm / Adam D. Walling
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

// NPLX: data part of upstream NppDarkMode.h: the colour model saved in config.xml and the
// enabled state. Applying dark mode to widgets is done with a Qt palette in ui/
// (the upstream uxtheme/subclassing code is not ported).

#pragma once

#include <string>

#include "win32compat.h"

namespace NppDarkMode
{
	struct Colors
	{
		COLORREF background = 0;
		COLORREF softerBackground = 0; // ctrl background color
		COLORREF hotBackground = 0;
		COLORREF pureBackground = 0;   // dlg background color
		COLORREF errorBackground = 0;
		COLORREF text = 0;
		COLORREF darkerText = 0;
		COLORREF disabledText = 0;
		COLORREF linkText = 0;
		COLORREF edge = 0;
		COLORREF hotEdge = 0;
		COLORREF disabledEdge = 0;
	};

	struct Options
	{
		bool enable = false;
		bool enablePlugin = false;
	};

	enum ColorTone {
		blackTone  = 0,
		redTone    = 1,
		greenTone  = 2,
		blueTone   = 3,
		purpleTone = 4,
		cyanTone   = 5,
		oliveTone  = 6,
		customizedTone = 32
	};

	enum class TreeViewStyle
	{
		classic = 0,
		light = 1,
		dark = 2
	};

	// NPLX: the enabled state is owned by core; ui/ applies it and keeps it in sync
	bool isEnabled();
	double calculatePerceivedLightness(COLORREF c);
	// NPLX: upstream isThemeDark() reads the tree view style computed from the default background (calculateTreeViewStyle)
	bool isThemeDark();
	void setEnabled(bool enable);
	bool isEnabledForPlugins();

	// NPLX: no-op in core (upstream copies NppGUI advanced options into the dark mode module)
	void setAdvancedOptions();

	// NPLX: no-op; the auto-completion list follows the Qt palette
	inline void setDarkAutoCompletion() {}

	Colors getDarkModeDefaultColors(ColorTone colorTone = ColorTone::blackTone);

	// NPLX: upstream signature without the HWND owner; see nplx::messageBox (UiServices.h)
	int darkMessageBoxW(const wchar_t* lpText, const wchar_t* lpCaption, UINT uType);
}
