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

// NPLX: upstream WinControls/Grid/BabyGridWrapper (the BabyGrid control of the Shortcut Mapper) on a QTableWidget.
// Same API and the same coordinates: row 0 is the column titles, column 0 the row numbers, the data starts at
// (1, 1). The BGN_* notifications are callbacks.

#pragma once

#include <cstddef>
#include <functional>

#include <QTableWidget>

class BabyGridWrapper : public QTableWidget
{
	Q_OBJECT

public:
	explicit BabyGridWrapper(QWidget* parent = nullptr);

	// BGN_CELLDBCLICKED, BGN_CELLRCLICKED, BGN_DELETECELL (VK_DELETE), BGN_ROWCHANGED (row: 1-based)
	std::function<void(int row)> _onCellDoubleClicked;
	std::function<void(int row, const QPoint& globalPos)> _onCellRightClicked;
	std::function<void()> _onDeleteCell;
	std::function<void(int row)> _onRowChanged;

	void setLineColNumber(size_t nbRow, size_t nbCol);
	void hideCursor() {}
	void setColsNumbered(bool isNumbered = true) { (void)isNumbered; }
	void setText(size_t row, size_t col, const wchar_t* text);
	void makeColAutoWidth(bool autoWidth = true) { _autoWidth = autoWidth; }
	int getSelectedRow() const; // 1-based, 0 when nothing is selected
	void clear();
	int getNumberRows() const { return rowCount(); }
	int getHomeRow() const; // the first visible row, 1-based
	void setLastView(size_t homeRow, size_t cursorRow);
	void updateView() { viewport()->update(); }
	bool setMarker(bool isMarker) { _isMarker = isMarker; return isMarker; } // BGM_SETPROTECT: a conflicting row
	void setAutoRow(bool isAutoRow) { (void)isAutoRow; }
	void setInitialContent(bool isInitialContent);

protected:
	void keyPressEvent(QKeyEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	bool _isMarker = false;
	bool _autoWidth = true;
	bool _isFilling = false;
};
