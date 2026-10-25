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

#include "BabyGridWrapper.h"

#include <algorithm>

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QScrollBar>

#include "NplxStyle.h"

BabyGridWrapper::BabyGridWrapper(QWidget* parent)
	: QTableWidget(parent)
{
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setEditTriggers(QAbstractItemView::NoEditTriggers);
	setAlternatingRowColors(true);
	setShowGrid(false);
	setWordWrap(false);
	verticalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
	verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8); // upstream setRowHeight(font height)
	horizontalHeader()->setHighlightSections(false);
	horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	connect(this, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
		if (_onCellDoubleClicked)
			_onCellDoubleClicked(row + 1);
	});
	connect(this, &QTableWidget::currentCellChanged, this, [this](int row, int, int prevRow, int) {
		if (!_isFilling && row >= 0 && row != prevRow && _onRowChanged)
			_onRowChanged(row + 1);
	});
}

void BabyGridWrapper::setLineColNumber(size_t nbRow, size_t nbCol)
{
	// BGM_SETGRIDDIM: nbCol data columns after the row-number column
	setRowCount(static_cast<int>(nbRow));
	if (columnCount() != static_cast<int>(nbCol))
		setColumnCount(static_cast<int>(nbCol));
	if (_autoWidth && nbCol > 0)
	{
		for (int c = 0; c + 1 < static_cast<int>(nbCol); ++c)
			horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
		horizontalHeader()->setSectionResizeMode(static_cast<int>(nbCol) - 1, QHeaderView::Stretch);
	}
}

void BabyGridWrapper::setText(size_t row, size_t col, const wchar_t* text)
{
	if (col == 0)
		return; // the row numbers are the vertical header
	const int c = static_cast<int>(col) - 1;
	const QString str = QString::fromWCharArray(text);
	if (row == 0)
	{
		if (c >= columnCount())
			setColumnCount(c + 1);
		setHorizontalHeaderItem(c, new QTableWidgetItem(str));
		return;
	}
	const int r = static_cast<int>(row) - 1;
	if (r >= rowCount())
		setRowCount(r + 1);
	if (c >= columnCount())
		setColumnCount(c + 1);
	auto* item = new QTableWidgetItem(str);
	if (_isMarker)
	{
		// BabyGrid draws a protected (conflicting) cell in the protect colour
		const bool dark = nplx::style::isDark();
		item->setBackground(dark ? QColor(0x6b, 0x24, 0x2a) : QColor(0xff, 0x82, 0x78));
		item->setForeground(dark ? QColor(0xff, 0xd7, 0xd7) : QColor(0x20, 0x00, 0x00));
	}
	setItem(r, c, item);
}

int BabyGridWrapper::getSelectedRow() const
{
	return currentRow() + 1;
}

void BabyGridWrapper::clear()
{
	// BGM_CLEARGRID
	clearContents();
	setRowCount(0);
}

int BabyGridWrapper::getHomeRow() const
{
	const int row = rowAt(0);
	return row < 0 ? 1 : row + 1;
}

void BabyGridWrapper::setLastView(size_t homeRow, size_t cursorRow)
{
	if (rowCount() == 0)
		return;
	const int home = std::clamp(static_cast<int>(homeRow) - 1, 0, rowCount() - 1);
	const int cursor = std::clamp(static_cast<int>(cursorRow) - 1, 0, rowCount() - 1);
	verticalScrollBar()->setValue(home);
	setCurrentCell(cursor, 0);
	scrollToItem(item(cursor, 0));
	if (_onRowChanged)
		_onRowChanged(cursor + 1);
}

void BabyGridWrapper::setInitialContent(bool isInitialContent)
{
	// upstream BGM_SETINITIALCONTENT: no row notification while the grid is being filled
	_isFilling = isInitialContent;
	setUpdatesEnabled(!isInitialContent);
}

void BabyGridWrapper::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier)
	{
		if (_onDeleteCell)
			_onDeleteCell();
		return;
	}
	if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && _onCellDoubleClicked && currentRow() >= 0)
	{
		_onCellDoubleClicked(currentRow() + 1); // NPLX: Enter modifies, like a double click
		return;
	}
	QTableWidget::keyPressEvent(event);
}

void BabyGridWrapper::contextMenuEvent(QContextMenuEvent* event)
{
	const int row = rowAt(event->pos().y());
	if (row >= 0)
		setCurrentCell(row, 0);
	if (_onCellRightClicked)
		_onCellRightClicked(row + 1, event->globalPos());
}
