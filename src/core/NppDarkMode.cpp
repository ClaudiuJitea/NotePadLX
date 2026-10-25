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

#include <cmath>
#include "NppDarkMode.h"
#include "Parameters.h"

#include "UiServices.h"

static constexpr COLORREF HEXRGB(DWORD rrggbb) {
	// from 0xRRGGBB like natural #RRGGBB
	// to the little-endian 0xBBGGRR
	return
		((rrggbb & 0xFF0000) >> 16) |
		((rrggbb & 0x00FF00)) |
		((rrggbb & 0x0000FF) << 16);
}

namespace NppDarkMode
{
	// black (default)
	static constexpr Colors darkColors{
		HEXRGB(0x202020),   // background
		HEXRGB(0x383838),   // softerBackground
		HEXRGB(0x454545),   // hotBackground
		HEXRGB(0x202020),   // pureBackground
		HEXRGB(0xB00000),   // errorBackground
		HEXRGB(0xE0E0E0),   // textColor
		HEXRGB(0xC0C0C0),   // darkerTextColor
		HEXRGB(0x808080),   // disabledTextColor
		HEXRGB(0xFFFF00),   // linkTextColor
		HEXRGB(0x646464),   // edgeColor
		HEXRGB(0x9B9B9B),   // hotEdgeColor
		HEXRGB(0x484848)    // disabledEdgeColor
	};

	static constexpr int offsetEdge = HEXRGB(0x1C1C1C);

	static constexpr Colors toneColors(int offset)
	{
		return Colors{
			darkColors.background + offset,
			darkColors.softerBackground + offset,
			darkColors.hotBackground + offset,
			darkColors.pureBackground + offset,
			darkColors.errorBackground,
			darkColors.text,
			darkColors.darkerText,
			darkColors.disabledText,
			darkColors.linkText,
			darkColors.edge + offsetEdge + offset,
			darkColors.hotEdge + offset,
			darkColors.disabledEdge + offset
		};
	}

	// red, green, blue, purple, cyan, olive tones (same offsets as upstream)
	static constexpr Colors darkRedColors = toneColors(HEXRGB(0x100000));
	static constexpr Colors darkGreenColors = toneColors(HEXRGB(0x001000));
	static constexpr Colors darkBlueColors = toneColors(HEXRGB(0x000020));
	static constexpr Colors darkPurpleColors = toneColors(HEXRGB(0x100020));
	static constexpr Colors darkCyanColors = toneColors(HEXRGB(0x001020));
	static constexpr Colors darkOliveColors = toneColors(HEXRGB(0x101000));

	static bool g_isEnabled = false;

	bool isEnabled()
	{
		return g_isEnabled;
	}

	void setEnabled(bool enable)
	{
		g_isEnabled = enable;
	}

	bool isEnabledForPlugins()
	{
		return g_isEnabled;
	}

	void setAdvancedOptions()
	{
	}

	Colors getDarkModeDefaultColors(ColorTone colorTone)
	{
		switch (colorTone)
		{
			case NppDarkMode::ColorTone::redTone:
				return darkRedColors;

			case NppDarkMode::ColorTone::greenTone:
				return darkGreenColors;

			case NppDarkMode::ColorTone::blueTone:
				return darkBlueColors;

			case NppDarkMode::ColorTone::purpleTone:
				return darkPurpleColors;

			case NppDarkMode::ColorTone::cyanTone:
				return darkCyanColors;

			case NppDarkMode::ColorTone::oliveTone:
				return darkOliveColors;

			case NppDarkMode::ColorTone::customizedTone:
			case NppDarkMode::ColorTone::blackTone:
			default:
				return darkColors;
		}
	}

	int darkMessageBoxW(const wchar_t* lpText, const wchar_t* lpCaption, UINT uType)
	{
		return nplx::messageBox(lpText ? lpText : L"", lpCaption ? lpCaption : L"", uType);
	}

	double calculatePerceivedLightness(COLORREF c)
	{
		auto linearValue = [](double colorChannel) -> double
		{
			colorChannel /= 255.0;
			if (colorChannel <= 0.04045)
				return colorChannel / 12.92;
			return std::pow(((colorChannel + 0.055) / 1.055), 2.4);
		};

		double r = linearValue(static_cast<double>(GetRValue(c)));
		double g = linearValue(static_cast<double>(GetGValue(c)));
		double b = linearValue(static_cast<double>(GetBValue(c)));

		double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;

		double lightness = (luminance <= 216.0 / 24389.0) ? (luminance * 24389.0 / 27.0) : (std::pow(luminance, (1.0 / 3.0)) * 116.0 - 16.0);
		return lightness;
	}

	bool isThemeDark()
	{
		constexpr double g_middleGrayRange = 2.0;
		return calculatePerceivedLightness(NppParameters::getInstance().getCurrentDefaultBgColor()) < (50.0 - g_middleGrayRange);
	}
}
