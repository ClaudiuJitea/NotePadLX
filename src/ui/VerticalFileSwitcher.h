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

// NPLX: port of upstream WinControls/VerticalFileSwitcher (VerticalFileSwitcher + VerticalFileSwitcherListView): the
// "Document List" panel. The Win32 report list view is a QTreeWidget without root decoration; each row keeps its
// TaskLstFnStatus (upstream: the item's LPARAM). Upstream's list view groups (one per view) are rows ordered by
// view, with a separator row before the second view.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QColor>

#include "Buffer.h"
#include "DockingPanel.h"
#include "TaskListInfo.h"

class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

#define FS_PROJECTPANELTITLE		L"Document List"

#define SORT_DIRECTION_NONE     -1
#define SORT_DIRECTION_UP     0
#define SORT_DIRECTION_DOWN   1

#define FS_ROOTNODE					"DocList"
#define FS_CLMNNAME					"ColumnName"
#define FS_CLMNEXT					"ColumnExt"
#define FS_CLMNPATH					"ColumnPath"
#define FS_LVGROUPS					"ListGroups"

class VerticalFileSwitcher : public DockingPanel
{
	Q_OBJECT

public:
	// NPLX: what upstream asks its parent with SendMessage (WM_GETTASKLISTINFO, NPPM_ACTIVATEDOC, NPPM_INTERNAL_CLOSEDOC,
	// NM_RCLICK, WM_COMMAND)
	struct Host
	{
		std::function<void(TaskListInfo&)> _getTaskListInfo;
		std::function<void(BufferID, int view)> _activateDoc;
		std::function<void(BufferID, int view)> _closeDoc;
		std::function<void(const QPoint& globalPos)> _showTabContextMenu; // one file selected: the tab context menu
		std::function<void(int cmdID)> _command;
	};

	explicit VerticalFileSwitcher(QWidget* parent = nullptr);
	void init(Host host);

	void display(bool toShow = true);

	//Activate document in scintilla by using the internal index
	void activateDoc(const TaskLstFnStatus* tlfs) const;
	void closeDoc(const TaskLstFnStatus* tlfs) const;

	int newItem(BufferID bufferID, int iView);
	int closeItem(BufferID bufferID, int iView);
	void activateItem(BufferID bufferID, int iView);
	void setItemIconStatus(BufferID bufferID);
	void setItemColor(BufferID bufferID);
	std::wstring getFullFilePath(size_t i) const;

	int setHeaderOrder(int columnIndex);
	void updateHeaderArrow();
	int nbSelectedFiles() const;
	std::vector<BufferViewInfo> getSelectedFiles(bool reverse = false) const;
	void startColumnSort();
	void reload();
	void updateTabOrder() {
		if (_lastSortingDirection == SORT_DIRECTION_NONE) {
			reloadList();
		}
	}

	void setBackgroundColor(COLORREF bgColour);
	void setForegroundColor(COLORREF fgColour);

	// NPLX: tests
	QTreeWidget* listView() const { return _fileListView; }
	int itemCount() const { return static_cast<int>(_items.size()); }
	QTreeWidgetItem* itemAt(int i) const { return _items.at(i); }
	const TaskLstFnStatus* statusAt(int i) const;
	void columnClick(int column); // upstream LVN_COLUMNCLICK
	QMenu* globalMenu() const { return _hGlobalMenu; }

protected:
	bool eventFilter(QObject* watched, QEvent* event) override; // middle click, Enter, double-click on empty area

private:
	Host _host;
	QTreeWidget* _fileListView = nullptr;
	std::vector<QTreeWidgetItem*> _items; // the document rows, in list order (upstream list view indexes)
	QMenu* _hGlobalMenu = nullptr;
	QMenu* _multiFilesMenu = nullptr; // upstream Notepad_plus::_fileSwitcherMultiFilePopupMenu
	int _currentIndex = 0;
	int _lastSortingColumn = 0;
	int _lastSortingDirection = SORT_DIRECTION_NONE;
	QColor _bgColour;
	QColor _fgColour;

	// VerticalFileSwitcherListView
	void initList();
	void reloadList();
	void removeAll();
	int find(BufferID bufferID, int iView) const;
	int add(BufferID bufferID, int iView);
	void remove(int index);
	void selectCurrentItem();
	void ensureVisibleCurrentItem() const;
	void fillItem(QTreeWidgetItem* item, const std::wstring& fullPath, int status) const;
	void colourItem(QTreeWidgetItem* item, int docColor) const; // upstream: the row background of a tab colour
	void rebuildRows(); // the rows of _items in the widget, with the view separator
	void resizeColumns();

	void initPopupMenus();
	void popupMenuCmd(int cmdID);
	void onRightClick(const QPoint& pos);
	void updateColours();
};
