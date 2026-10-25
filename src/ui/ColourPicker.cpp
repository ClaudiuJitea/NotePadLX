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

#include "ColourPicker.h"

#include <QColorDialog>
#include <QMouseEvent>
#include <QPainter>

ColourPicker::ColourPicker(QWidget* parent)
	: QWidget(parent)
{
	setFixedSize(25, 25);
	setCursor(Qt::PointingHandCursor);
}

void ColourPicker::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRect r = rect().adjusted(1, 1, -2, -2);
	const QColor colour(GetRValue(_currentColour), GetGValue(_currentColour), GetBValue(_currentColour));
	const bool usable = QWidget::isEnabled();
	p.fillRect(r, usable ? colour : palette().color(QPalette::Window));
	if (!_isEnabled && usable)
	{
		// upstream drawBackground: a hatched brush over a disabled colour
		p.fillRect(r, QBrush(palette().color(QPalette::WindowText), Qt::FDiagPattern));
	}
	p.setPen(palette().color(usable ? QPalette::WindowText : QPalette::Mid));
	p.drawRect(r);
}

void ColourPicker::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		const QColor initial(GetRValue(_currentColour), GetGValue(_currentColour), GetBValue(_currentColour));
		const QColor c = QColorDialog::getColor(initial, this);
		if (c.isValid())
		{
			_currentColour = RGB(c.red(), c.green(), c.blue());
			_isEnabled = true;
			update();
			emit colourPicked();
		}
	}
	else if (event->button() == Qt::RightButton && !_disableRightClick)
	{
		_isEnabled = !_isEnabled;
		update();
		emit colourPicked();
	}
}
