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

// NPLX: the NotePadLX look (no upstream equivalent: upstream draws Win32 controls and its own dark mode).
// - a palette of design tokens, light and dark, following the system colour scheme (or upstream dark mode),
// - an application style (Fusion based) without the stock icons on dialog buttons,
// - a stylesheet built from the tokens (buttons, inputs, menus, tabs, toolbar, scrollbars...),
// - the NotePadLX icons (resources/nplx-icons): SVG templates recoloured for the theme and the icon state.

#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace nplx::style
{
	struct Tokens
	{
		bool _dark = false;
		QColor _window, _base, _alt, _text, _muted, _border, _borderSoft, _borderStrong;
		QColor _hover, _pressed, _button;
		QColor _accent, _accentHover, _accentText, _accentSoft, _accentBorder;
		QColor _danger, _dangerSoft, _warn, _disabled;
		QColor _tooltip, _tooltipText, _scrollHandle, _scrollHandleHover;
		QColor _menu, _menuHover, _menuBorder; // popup menus: an elevated surface
		// icon colours
		QColor _iconFg, _iconMuted, _iconAccent;
	};

	// the tokens of the current theme
	const Tokens& tokens();
	bool isDark();

	// upstream NppDarkMode::initDarkMode(): dark or light from config.xml DarkMode (enable / enableWindowsMode)
	void initDarkMode();

	// installs the style, palette and stylesheet on the application, dark or light after NppDarkMode::isEnabled();
	// re-applies them when the desktop colour scheme changes. NPLX_THEME=light|dark overrides.
	void apply(QApplication& app);

	// re-reads the scheme (e.g. after the upstream dark mode setting changed)
	void refresh();

	// a NotePadLX icon by name (resources/nplx-icons/<name>.svg), drawn for the current theme
	QIcon icon(const QString& name);

	// a toolbar icon: drawn with the style of setToolbarIconStyle (Preferences > Toolbar)
	QIcon toolbarIcon(const QString& name);

	// a document tab icon; the alternate set has coloured documents (Preferences > Tab Bar > Alternate icons)
	QIcon tabIcon(const QString& name, bool alternate);

	// upstream Fluent colorization: tint (invalid for the theme colours), the whole icon (mono) or its accents, and
	// thicker strokes for the "filled" sets
	void setToolbarIconStyle(const QColor& tint, bool mono, bool bold);

	// the stylesheet text for the given tokens (exposed for tests)
	QString styleSheet(const Tokens& t);
}
