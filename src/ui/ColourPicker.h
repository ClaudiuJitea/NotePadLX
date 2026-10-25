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

// NPLX: port of upstream WinControls/ColourPicker on a QWidget: left click picks a colour (QColorDialog in
// place of ColourPopup), right click toggles "enabled" (a disabled colour is drawn hatched), and both emit
// colourPicked (upstream CPN_COLOURPICKED).

#pragma once

#include <QWidget>

#include "win32compat.h"

class ColourPicker : public QWidget
{
	Q_OBJECT

public:
	explicit ColourPicker(QWidget* parent = nullptr);

	void setColour(COLORREF c) { _currentColour = c; update(); }
	COLORREF getColour() const { return _currentColour; }
	bool isEnabled() const { return _isEnabled; }
	void setEnabled(bool enabled) { _isEnabled = enabled; update(); }
	void disableRightClick() { _disableRightClick = true; }

	QSize sizeHint() const override { return QSize(25, 25); }

signals:
	void colourPicked();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;

private:
	COLORREF _currentColour = RGB(0xFF, 0x00, 0x00);
	bool _isEnabled = true;
	bool _disableRightClick = false;
};
