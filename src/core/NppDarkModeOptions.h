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

// NPLX: the advanced options part of upstream NppDarkMode.cpp (getThemeName, getToolbarIconInfo, setToolbarIconSet,
// setToolbarFluentColor, setToolbarFluentMonochrome, setToolbarFluentCustomColor, setTabIconSet, getTabIconSet,
// setWindowsMode...). Upstream keeps a copy of NppGUI::_darkmode._advOptions (g_advOptions); here they work on
// NppGUI directly, so config.xml always has the current values.

#pragma once

#include <string>

#include "NppDarkMode.h"
#include "Parameters.h"

namespace NppDarkMode
{
	inline AdvancedOptions& advOptions()
	{
		return NppParameters::getInstance().getNppGUI()._darkmode._advOptions;
	}

	inline AdvOptDefaults& advDefaults(bool useDark)
	{
		return useDark ? advOptions()._darkDefaults : advOptions()._lightDefaults;
	}

	inline bool isWindowsModeEnabled() { return advOptions()._enableWindowsMode; }
	inline void setWindowsMode(bool enable) { advOptions()._enableWindowsMode = enable; }

	// the editor theme of the current mode; empty for stylers.xml
	inline std::wstring getThemeName()
	{
		const std::wstring& theme = advDefaults(isEnabled())._xmlFileName;
		return (theme == L"stylers.xml") ? std::wstring() : theme;
	}

	// NPLX: a custom colour of 0 means the accent colour of the desktop (upstream getAccentColor), resolved by the UI
	inline TbIconInfo getToolbarIconInfo(bool useDark) { return advDefaults(useDark)._tbIconInfo; }
	inline TbIconInfo getToolbarIconInfo() { return getToolbarIconInfo(isEnabled()); }

	inline void setToolbarIconSet(int state2Set, bool useDark)
	{
		advDefaults(useDark)._tbIconInfo._tbIconSet = static_cast<toolBarStatusType>(state2Set);
	}
	inline void setToolbarFluentColor(FluentColor color2Set, bool useDark) { advDefaults(useDark)._tbIconInfo._tbColor = color2Set; }
	inline void setToolbarFluentColor(FluentColor color2Set) { setToolbarFluentColor(color2Set, isEnabled()); }
	inline void setToolbarFluentMonochrome(bool setMonochrome, bool useDark) { advDefaults(useDark)._tbIconInfo._tbUseMono = setMonochrome; }
	inline void setToolbarFluentMonochrome(bool setMonochrome) { setToolbarFluentMonochrome(setMonochrome, isEnabled()); }
	inline void setToolbarFluentCustomColor(COLORREF color, bool useDark) { advDefaults(useDark)._tbIconInfo._tbCustomColor = color; }
	inline void setToolbarFluentCustomColor(COLORREF color) { setToolbarFluentCustomColor(color, isEnabled()); }

	inline void setTabIconSet(bool useAltIcons, bool useDark)
	{
		if (useDark)
			advOptions()._darkDefaults._tabIconSet = useAltIcons ? 1 : 2;
		else
			advOptions()._lightDefaults._tabIconSet = useAltIcons ? 1 : 0;
	}
	inline int getTabIconSet(bool useDark) { return advDefaults(useDark)._tabIconSet; }
}
