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

// NPLX: port of upstream WinControls/AnsiCharPanel (ansiCharPanel + asciiListView): the "ASCII Codes Insertion
// Panel". The list view is a QTreeWidget with the same 6 columns.

#pragma once

#include <string>

#include <QColor>

#include "DockingPanel.h"

class QTreeWidget;
class ScintillaEditView;

class AnsiCharPanel : public DockingPanel
{
	Q_OBJECT

public:
	explicit AnsiCharPanel(QWidget* parent = nullptr);

	void init(ScintillaEditView **ppEditView);
	void switchEncoding();
	void insertChar(unsigned char char2insert) const;
	void insertString(const std::wstring& string2insert) const;

	void setBackgroundColor(COLORREF bgColour);
	void setForegroundColor(COLORREF fgColour);

	// upstream AsciiListView
	void setValues(int codepage = 0);
	void resetValues(int codepage);
	std::wstring getAscii(unsigned char value);
	std::wstring getHtmlName(unsigned char value);
	int getHtmlNumber(unsigned char value);

	QTreeWidget* listView() const { return _listView; } // NPLX: tests

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	ScintillaEditView **_ppEditView = nullptr;
	QTreeWidget* _listView = nullptr;
	int _codepage = -1;
	QColor _bgColour;
	QColor _fgColour;

	void updateColours();
};
