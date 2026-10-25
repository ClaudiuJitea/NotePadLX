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

// NPLX: port of upstream WinControls/WindowsDlg (Window > Windows... and the Window > Sort By commands). The
// owner-data list view is a QTreeWidget refilled from _idxMap; WDN_NOTIFY (NMWINDLG) reaches Notepad_plus through
// a callback.

#pragma once

#include <functional>
#include <vector>

#include <QDialog>

class Buffer;
class DocTabView;
class QPushButton;
class QTreeWidget;

typedef enum {
	WDT_ACTIVATE = 1,
	WDT_SAVE = 2,
	WDT_CLOSE = 3,
	WDT_SORT = 4,
} WinDlgNotifyType;

struct NMWINDLG {
	bool processed = false;
	WinDlgNotifyType type = WDT_ACTIVATE;
	unsigned int curSel = 0;
	unsigned int nItems = 0;
	unsigned int* Items = nullptr;
};

class WindowsDlg : public QDialog
{
	Q_OBJECT

public :
	explicit WindowsDlg(QWidget* parent = nullptr);

	// pTab: the documents of the current view; notify: upstream ::SendMessage(_hParent, WDN_NOTIFY, 0, &nmdlg)
	void init(DocTabView *pTab, std::function<void(NMWINDLG&)> notify);
	int doDialog();

	void doSortToTabs();
	void doSort();
	void sort(int columnID, bool reverseSort);
	void sortFileNameASC();
	void sortFileNameDSC();
	void sortFilePathASC();
	void sortFilePathDSC();
	void sortFileTypeASC();
	void sortFileTypeDSC();
	void sortFileSizeASC();
	void sortFileSizeDSC();
	void sortDateTimeASC();
	void sortDateTimeDSC();
	void doRefresh(bool force = false);

	// NPLX: tests
	QTreeWidget* list() const { return _hList; }
	void columnClick(int column); // upstream LVN_COLUMNCLICK
	void sortTabs();              // upstream IDC_WINDOWS_SORT (the "Sort tabs" button)
	void activateCurrent();
	void doSave();
	void doClose();

protected :
	bool eventFilter(QObject* watched, QEvent* event) override; // upstream LVN_KEYDOWN, WM_CHAR_REPLACEMENT
	void reject() override; // upstream IDCANCEL

	void updateColumnNames();
	void resetSelection();
	void updateButtonState();
	void doColumnSort();
	void doCount();
	void refreshMap();
	void putItemsToClipboard(bool isFullPath);
	Buffer* getBuffer(int index) const;
	void fillList(); // NPLX: upstream LVN_GETDISPINFO (owner data)
	std::vector<int> selectedRows() const;
	void selectRow(int row, bool select);
	void typeAhead(QChar ch);

	QTreeWidget* _hList = nullptr;
	QPushButton* _activateButton = nullptr;
	QPushButton* _saveButton = nullptr;
	QPushButton* _closeButton = nullptr;
	QPushButton* _sortButton = nullptr;
	static QRect _lastKnownLocation;
	DocTabView* _pTab = nullptr;
	std::vector<int> _idxMap;
	int _currentColumn = -1;
	int _lastSort = -1;
	bool _reverseSort = false;
	std::function<void(NMWINDLG&)> _notify;
};
