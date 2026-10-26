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

#include "VerticalFileSwitcher.h"

#include <algorithm>

#include <QApplication>
#include <QHeaderView>
#include <QMenu>
#include <QMouseEvent>
#include <QTreeWidget>

#include "NplxStyle.h"
#include "NppConstants.h"
#include "NppDarkMode.h"
#include "Parameters.h"
#include "localization.h"
#include "menuCmdID.h"

using namespace std;

#define CLMNEXT_ID     1
#define CLMNPATH_ID    2
#define SEP_POS        3
#define LVGROUPS_ID    4

namespace
{
	constexpr int StatusRole = Qt::UserRole; // the row's TaskLstFnStatus index in _statuses
	constexpr int SeparatorRole = Qt::UserRole + 1;

	QIcon statusIcon(int status)
	{
		// upstream image list: saved, unsaved, read-only, monitoring (the tab bar icons)
		switch (status)
		{
			case tb_unsaved:   return nplx::style::icon(QStringLiteral("tab_unsaved"));
			case tb_ro:        return nplx::style::icon(QStringLiteral("tab_readonly"));
			case tb_monitored: return nplx::style::icon(QStringLiteral("tab_monitoring"));
			default:           return nplx::style::icon(QStringLiteral("tab_saved"));
		}
	}

	int statusOf(Buffer* buf)
	{
		return buf->isMonitoringOn()?3:(buf->isReadOnly()?2:(buf->isDirty()?1:0));
	}

	// upstream PathFindFileName / PathRemoveExtension / PathFindExtension / _wsplitpath_s (drive + dir)
	std::wstring fileNameOf(const std::wstring& path)
	{
		const size_t slash = path.find_last_of(L'/');
		return slash == std::wstring::npos ? path : path.substr(slash + 1);
	}

	std::wstring extensionOf(const std::wstring& path)
	{
		return PathFindExtension(fileNameOf(path).c_str());
	}

	std::wstring dirOf(const std::wstring& path)
	{
		const size_t slash = path.find_last_of(L'/');
		return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
	}

	TaskLstFnStatus* statusOfItem(QTreeWidgetItem* item)
	{
		return item ? static_cast<TaskLstFnStatus*>(item->data(0, StatusRole).value<void*>()) : nullptr;
	}
}

VerticalFileSwitcher::VerticalFileSwitcher(QWidget* parent)
	: DockingPanel(QString::fromWCharArray(FS_PROJECTPANELTITLE), IDM_VIEW_DOCLIST, QStringLiteral("DocumentList"), parent)
{
	NativeLangSpeaker* pNativeSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
	if (pNativeSpeaker)
		setWindowTitle(QString::fromStdWString(pNativeSpeaker->getAttrNameStr(FS_PROJECTPANELTITLE, FS_ROOTNODE, "PanelTitle")));

	// upstream WM_INITDIALOG / VerticalFileSwitcherListView::init: report view, full row select, multiple selection
	_fileListView = new QTreeWidget(this);
	_fileListView->setRootIsDecorated(false);
	_fileListView->setUniformRowHeights(true);
	_fileListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
	_fileListView->setAllColumnsShowFocus(true);
	_fileListView->setFrameShape(QFrame::NoFrame);
	_fileListView->setContextMenuPolicy(Qt::CustomContextMenu);
	_fileListView->header()->setContextMenuPolicy(Qt::CustomContextMenu);
	_fileListView->header()->setSectionsClickable(true);
	_fileListView->header()->setSortIndicatorShown(false);
	_fileListView->header()->setStretchLastSection(false);
	_fileListView->viewport()->installEventFilter(this);
	_fileListView->installEventFilter(this);
	setWidget(_fileListView);

	initPopupMenus();

	// upstream NM_CLICK: activate the document (not with Ctrl / Shift, which extend the selection)
	connect(_fileListView, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
		if (QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
			return;
		if (const TaskLstFnStatus* tlfs = statusOfItem(item))
			activateDoc(tlfs);
	});
	connect(_fileListView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { onRightClick(pos); });
	connect(_fileListView->header(), &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		// upstream WM_CONTEXTMENU on the header: the column menu
		_hGlobalMenu->popup(_fileListView->header()->mapToGlobal(pos));
	});
	connect(_fileListView->header(), &QHeaderView::sectionClicked, this, [this](int column) { columnClick(column); });
	connect(_fileListView->header(), &QHeaderView::sectionResized, this, [this](int column, int, int newSize) {
		// upstream HDN_ENDTRACK: remember the Ext. / Path widths
		if (column == 0 || _fileListView->header()->sectionsHidden())
			return;
		NppParameters& nppParams = NppParameters::getInstance();
		const QString text = _fileListView->headerItem()->text(column);
		NativeLangSpeaker* pNativeSpeaker = nppParams.getNativeLangSpeaker();
		const QString extStr = QString::fromStdWString(pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(L"Ext.", FS_ROOTNODE, FS_CLMNEXT) : L"Ext.");
		const QString pathStr = QString::fromStdWString(pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(L"Path", FS_ROOTNODE, FS_CLMNPATH) : L"Path");
		if (text == extStr)
			nppParams.getNppGUI()._fileSwitcherExtWidth = newSize;
		else if (text == pathStr)
			nppParams.getNppGUI()._fileSwitcherPathWidth = newSize;
	});
}

void VerticalFileSwitcher::init(Host host)
{
	_host = std::move(host);
	initList();
}

void VerticalFileSwitcher::initPopupMenus()
{
	// upstream VerticalFileSwitcher::initPopupMenus: the column header menu
	NppParameters& nppParams = NppParameters::getInstance();
	NativeLangSpeaker* pNativeSpeaker = nppParams.getNativeLangSpeaker();
	const NppGUI& nppGUI = nppParams.getNppGUI();

	auto localized = [pNativeSpeaker](const wchar_t* defaultStr, const char* node) {
		return QString::fromStdWString(pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(defaultStr, FS_ROOTNODE, node) : defaultStr);
	};

	_hGlobalMenu = new QMenu(this);
	struct Entry { int _id; QString _text; bool _checked; };
	const Entry entries[] = {
		{ CLMNEXT_ID, localized(L"Ext.", FS_CLMNEXT), !nppGUI._fileSwitcherWithoutExtColumn },
		{ CLMNPATH_ID, localized(L"Path", FS_CLMNPATH), !nppGUI._fileSwitcherWithoutPathColumn },
		{ SEP_POS, QString(), false },
		{ LVGROUPS_ID, localized(L"Group by View", FS_LVGROUPS), !nppGUI._fileSwitcherDisableListViewGroups },
	};
	for (const Entry& e : entries)
	{
		if (e._id == SEP_POS)
		{
			_hGlobalMenu->addSeparator();
			continue;
		}
		QAction* a = _hGlobalMenu->addAction(e._text);
		a->setCheckable(true);
		a->setChecked(e._checked);
		a->setData(e._id);
		const int id = e._id;
		connect(a, &QAction::triggered, this, [this, id]() { popupMenuCmd(id); });
	}

	// upstream Notepad_plus::_fileSwitcherMultiFilePopupMenu (several files selected)
	_multiFilesMenu = new QMenu(this);
	struct Item { int _id; const wchar_t* _text; };
	for (const Item& i : { Item{ IDM_DOCLIST_FILESCLOSE, L"Close Selected files" }, Item{ IDM_DOCLIST_FILESCLOSEOTHERS, L"Close Other files" },
		Item{ IDM_DOCLIST_COPYNAMES, L"Copy Selected Names" }, Item{ IDM_DOCLIST_COPYPATHS, L"Copy Selected Pathnames" } })
	{
		QAction* a = _multiFilesMenu->addAction(QString::fromWCharArray(i._text));
		const int id = i._id;
		connect(a, &QAction::triggered, this, [this, id]() { if (_host._command) _host._command(id); });
	}
}

void VerticalFileSwitcher::popupMenuCmd(int cmdID)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	bool* setting = nullptr;
	switch (cmdID)
	{
		case CLMNEXT_ID:  setting = &nppGUI._fileSwitcherWithoutExtColumn; break;
		case CLMNPATH_ID: setting = &nppGUI._fileSwitcherWithoutPathColumn; break;
		case LVGROUPS_ID: setting = &nppGUI._fileSwitcherDisableListViewGroups; break;
		default: return;
	}
	*setting = !*setting;
	for (QAction* a : _hGlobalMenu->actions())
	{
		if (a->data().toInt() == cmdID)
			a->setChecked(!*setting);
	}
	reload();
}

void VerticalFileSwitcher::display(bool toShow)
{
	DockingPanel::display(toShow);
	ensureVisibleCurrentItem(); // without this call the current item may stay above visible area after the program startup
}

void VerticalFileSwitcher::activateDoc(const TaskLstFnStatus* tlfs) const
{
	if (_host._activateDoc)
		_host._activateDoc(static_cast<BufferID>(tlfs->_bufID), tlfs->_iView);
}

void VerticalFileSwitcher::closeDoc(const TaskLstFnStatus* tlfs) const
{
	if (_host._closeDoc)
		_host._closeDoc(static_cast<BufferID>(tlfs->_bufID), tlfs->_iView);
}

// ---- list view (upstream VerticalFileSwitcherListView)

void VerticalFileSwitcher::fillItem(QTreeWidgetItem* item, const std::wstring& fullPath, int status) const
{
	const NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	const bool isExtColumn = !nppGUI._fileSwitcherWithoutExtColumn;
	const bool isPathColumn = !nppGUI._fileSwitcherWithoutPathColumn;

	std::wstring fn = fileNameOf(fullPath);
	if (isExtColumn)
	{
		const std::wstring ext = extensionOf(fullPath);
		fn.resize(fn.size() - ext.size()); // PathRemoveExtension
	}
	item->setText(0, QString::fromStdWString(fn));
	item->setIcon(0, statusIcon(status));
	item->setToolTip(0, QString::fromStdWString(fullPath)); // upstream LVN_GETINFOTIP
	int colIndex = 0;
	if (isExtColumn)
		item->setText(++colIndex, QString::fromStdWString(extensionOf(fullPath)));
	if (isPathColumn)
		item->setText(++colIndex, QString::fromStdWString(dirOf(fullPath)));
}

void VerticalFileSwitcher::colourItem(QTreeWidgetItem* item, int docColor) const
{
	// upstream NM_CUSTOMDRAW: the row of a coloured tab has its (light) colour as background
	QBrush brush;
	if (docColor >= 0)
	{
		const COLORREF c = NppParameters::getInstance().getIndividualTabColor(docColor, NppDarkMode::isEnabled(), false);
		brush = QBrush(QColor(GetRValue(c), GetGValue(c), GetBValue(c)));
	}
	for (int col = 0; col < item->columnCount() || col == 0; ++col)
		item->setBackground(col, brush);
}

void VerticalFileSwitcher::initList()
{
	NppParameters& nppParams = NppParameters::getInstance();
	NativeLangSpeaker *pNativeSpeaker = nppParams.getNativeLangSpeaker();
	const NppGUI& nppGUI = nppParams.getNppGUI();
	bool isExtColumn = !nppGUI._fileSwitcherWithoutExtColumn;
	bool isPathColumn = !nppGUI._fileSwitcherWithoutPathColumn;

	auto localized = [pNativeSpeaker](const wchar_t* defaultStr, const char* node) {
		return QString::fromStdWString(pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(defaultStr, FS_ROOTNODE, node) : defaultStr);
	};

	//add columns
	QStringList headers{ localized(L"Name", FS_CLMNNAME) };
	if (isExtColumn)
		headers << localized(L"Ext.", FS_CLMNEXT);
	if (isPathColumn)
		headers << localized(L"Path", FS_CLMNPATH);
	{
		QSignalBlocker blocker(_fileListView->header());
		_fileListView->setColumnCount(static_cast<int>(headers.size()));
		_fileListView->setHeaderLabels(headers);
	}

	TaskListInfo taskListInfo;
	if (_host._getTaskListInfo)
		_host._getTaskListInfo(taskListInfo);

	for (const TaskLstFnStatus& fileNameStatus : taskListInfo._tlfsLst)
	{
		auto* item = new QTreeWidgetItem();
		item->setData(0, StatusRole, QVariant::fromValue<void*>(new TaskLstFnStatus(fileNameStatus)));
		fillItem(item, fileNameStatus._fn, fileNameStatus._status);
		colourItem(item, fileNameStatus._docColor);
		_items.push_back(item);
	}
	rebuildRows();
	resizeColumns();

	_currentIndex = taskListInfo._currentIndex;
	selectCurrentItem();
	ensureVisibleCurrentItem();	// without this call the current item may become invisible after adding/removing columns
}

void VerticalFileSwitcher::rebuildRows()
{
	// upstream list view groups: the documents of each view together, the second view after a separator
	const bool isListViewGroups = !NppParameters::getInstance().getNppGUI()._fileSwitcherDisableListViewGroups;
	QSignalBlocker blocker(_fileListView);
	while (_fileListView->topLevelItemCount())
	{
		QTreeWidgetItem* item = _fileListView->takeTopLevelItem(0);
		if (item->data(0, SeparatorRole).toBool())
			delete item; // the separator rows are not in _items
	}

	std::vector<QTreeWidgetItem*> order = _items;
	if (isListViewGroups)
	{
		std::stable_partition(order.begin(), order.end(), [](QTreeWidgetItem* i) { return statusOfItem(i)->_iView == MAIN_VIEW; });
		_items = order; // upstream: the list view indexes follow the groups
	}

	bool previousIsMain = true;
	for (QTreeWidgetItem* item : order)
	{
		const bool isMain = statusOfItem(item)->_iView == MAIN_VIEW;
		if (isListViewGroups && previousIsMain && !isMain && item != order.front())
		{
			auto* separator = new QTreeWidgetItem();
			separator->setData(0, SeparatorRole, true);
			separator->setFlags(Qt::NoItemFlags);
			separator->setSizeHint(0, QSize(0, 6));
			_fileListView->addTopLevelItem(separator);
		}
		previousIsMain = isMain;
		_fileListView->addTopLevelItem(item);
	}
}

void VerticalFileSwitcher::resizeColumns()
{
	// upstream resizeColumns: Ext. and Path keep their saved width, Name takes the rest
	NppParameters& nppParams = NppParameters::getInstance();
	const NppGUI& nppGUI = nppParams.getNppGUI();
	QSignalBlocker blocker(_fileListView->header());
	int colIndex = 0;
	int others = 0;
	if (!nppGUI._fileSwitcherWithoutExtColumn)
	{
		_fileListView->setColumnWidth(++colIndex, nppGUI._fileSwitcherExtWidth);
		others += nppGUI._fileSwitcherExtWidth;
	}
	if (!nppGUI._fileSwitcherWithoutPathColumn)
	{
		_fileListView->setColumnWidth(++colIndex, nppGUI._fileSwitcherPathWidth);
		others += nppGUI._fileSwitcherPathWidth;
	}
	_fileListView->setColumnWidth(0, std::max(80, _fileListView->viewport()->width() - others));
}

void VerticalFileSwitcher::reloadList()
{
	removeAll();
	initList();
}

void VerticalFileSwitcher::reload()
{
	reloadList();
	startColumnSort();
}

const TaskLstFnStatus* VerticalFileSwitcher::statusAt(int i) const
{
	return (i >= 0 && i < itemCount()) ? statusOfItem(_items[i]) : nullptr;
}

int VerticalFileSwitcher::newItem(BufferID bufferID, int iView)
{
	int i = find(bufferID, iView);
	if (i == -1)
	{
		i = add(bufferID, iView);
	}
	return i;
}

void VerticalFileSwitcher::setItemIconStatus(BufferID bufferID)
{
	Buffer *buf = bufferID;
	for (QTreeWidgetItem* item : _items)
	{
		TaskLstFnStatus *tlfs = statusOfItem(item);
		if (tlfs->_bufID == bufferID)
		{
			tlfs->_fn = buf->getFullPathName();
			tlfs->_status = statusOf(buf);
			fillItem(item, tlfs->_fn, tlfs->_status);
		}
	}
}

void VerticalFileSwitcher::setItemColor(BufferID bufferID)
{
	for (QTreeWidgetItem* item : _items)
	{
		TaskLstFnStatus* tlfs = statusOfItem(item);
		if (tlfs->_bufID == bufferID)
		{
			tlfs->_docColor = bufferID->getDocColorId();
			colourItem(item, tlfs->_docColor);
		}
	}
}

wstring VerticalFileSwitcher::getFullFilePath(size_t i) const
{
	if (i >= _items.size())
		return L"";
	return statusOfItem(_items[i])->_fn;
}

int VerticalFileSwitcher::closeItem(BufferID bufferID, int iView)
{
	int i = find(bufferID, iView);
	if (i != -1)
		remove(i);
	return i;
}

void VerticalFileSwitcher::activateItem(BufferID bufferID, int iView)
{
	_fileListView->clearSelection();
	_currentIndex = newItem(bufferID, iView);
	selectCurrentItem();
	ensureVisibleCurrentItem();
}

int VerticalFileSwitcher::add(BufferID bufferID, int iView)
{
	Buffer *buf = bufferID;
	auto* tl = new TaskLstFnStatus(iView, 0, buf->getFullPathName(), statusOf(buf), (void *)bufferID, buf->getDocColorId());
	auto* item = new QTreeWidgetItem();
	item->setData(0, StatusRole, QVariant::fromValue<void*>(tl));
	fillItem(item, tl->_fn, tl->_status);
	colourItem(item, tl->_docColor);
	_items.push_back(item);
	rebuildRows();

	_currentIndex = static_cast<int>(std::find(_items.begin(), _items.end(), item) - _items.begin());
	selectCurrentItem();
	return _currentIndex;
}

void VerticalFileSwitcher::remove(int index)
{
	QTreeWidgetItem* item = _items[index];
	delete statusOfItem(item);
	_items.erase(_items.begin() + index);
	delete item;
	rebuildRows();
}

void VerticalFileSwitcher::removeAll()
{
	for (QTreeWidgetItem* item : _items)
		delete statusOfItem(item);
	_items.clear();
	_fileListView->clear();
}

int VerticalFileSwitcher::find(BufferID bufferID, int iView) const
{
	for (size_t i = 0; i < _items.size(); ++i)
	{
		const TaskLstFnStatus *tlfs = statusOfItem(_items[i]);
		if (tlfs->_bufID == bufferID && tlfs->_iView == iView)
			return static_cast<int>(i);
	}
	return -1;
}

void VerticalFileSwitcher::selectCurrentItem()
{
	if (_currentIndex < 0 || _currentIndex >= itemCount())
		return;
	QSignalBlocker blocker(_fileListView);
	_fileListView->setCurrentItem(_items[_currentIndex], 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void VerticalFileSwitcher::ensureVisibleCurrentItem() const
{
	if (_currentIndex >= 0 && _currentIndex < itemCount())
		_fileListView->scrollToItem(_items[_currentIndex]);
}

int VerticalFileSwitcher::nbSelectedFiles() const
{
	int n = 0;
	for (QTreeWidgetItem* item : _items)
		n += item->isSelected() ? 1 : 0;
	return n;
}

std::vector<BufferViewInfo> VerticalFileSwitcher::getSelectedFiles(bool reverse) const
{
	std::vector<BufferViewInfo> files;
	for (QTreeWidgetItem* item : _items)
	{
		bool isChosen = reverse ? !item->isSelected() : item->isSelected();
		if (isChosen)
		{
			TaskLstFnStatus *tlfs = statusOfItem(item);
			files.push_back(BufferViewInfo(static_cast<BufferID>(tlfs->_bufID), tlfs->_iView));
		}
	}
	return files;
}

// ---- sorting (upstream LVN_COLUMNCLICK, setHeaderOrder, startColumnSort, updateHeaderArrow)

void VerticalFileSwitcher::columnClick(int column)
{
	_lastSortingDirection = setHeaderOrder(column);
	_lastSortingColumn = column;
	if (_lastSortingDirection != SORT_DIRECTION_NONE)
	{
		startColumnSort();
	}
	else
	{
		reloadList();
		updateHeaderArrow();
	}
}

int VerticalFileSwitcher::setHeaderOrder(int columnIndex)
{
	if (_lastSortingColumn != columnIndex && _lastSortingDirection != SORT_DIRECTION_NONE)
		_lastSortingDirection = SORT_DIRECTION_NONE;

	if (_lastSortingDirection == SORT_DIRECTION_NONE)
		return SORT_DIRECTION_UP;
	if (_lastSortingDirection == SORT_DIRECTION_UP)
		return SORT_DIRECTION_DOWN;
	//if (_lastSortingDirection == SORT_DIRECTION_DOWN)
	return SORT_DIRECTION_NONE;
}

void VerticalFileSwitcher::startColumnSort()
{
	if (_lastSortingColumn >= _fileListView->columnCount())
	{
		_lastSortingColumn = 0;
		_lastSortingDirection = SORT_DIRECTION_NONE;
	}

	if (_lastSortingDirection != SORT_DIRECTION_NONE)
	{
		// upstream ListViewCompareProc: lstrcmp of the column texts, within each group
		const int column = _lastSortingColumn;
		const bool up = _lastSortingDirection == SORT_DIRECTION_UP;
		const bool isListViewGroups = !NppParameters::getInstance().getNppGUI()._fileSwitcherDisableListViewGroups;
		BufferID currentBuf = _currentIndex >= 0 && _currentIndex < itemCount() ? static_cast<BufferID>(statusOfItem(_items[_currentIndex])->_bufID) : nullptr;
		int currentView = _currentIndex >= 0 && _currentIndex < itemCount() ? statusOfItem(_items[_currentIndex])->_iView : -1;
		std::stable_sort(_items.begin(), _items.end(), [column, up, isListViewGroups](QTreeWidgetItem* a, QTreeWidgetItem* b) {
			if (isListViewGroups && statusOfItem(a)->_iView != statusOfItem(b)->_iView)
				return statusOfItem(a)->_iView < statusOfItem(b)->_iView;
			const int result = a->text(column).compare(b->text(column));
			return up ? result < 0 : result > 0;
		});
		rebuildRows();
		if (currentBuf)
			_currentIndex = find(currentBuf, currentView);
		selectCurrentItem();
	}
	updateHeaderArrow();
}

void VerticalFileSwitcher::updateHeaderArrow()
{
	QHeaderView* header = _fileListView->header();
	if (_lastSortingDirection == SORT_DIRECTION_NONE)
	{
		header->setSortIndicatorShown(false);
		return;
	}
	header->setSortIndicatorShown(true);
	QSignalBlocker blocker(header);
	header->setSortIndicator(_lastSortingColumn, _lastSortingDirection == SORT_DIRECTION_UP ? Qt::AscendingOrder : Qt::DescendingOrder);
}

// ---- mouse and keyboard (upstream WM_NOTIFY / WM_PARENTNOTIFY)

void VerticalFileSwitcher::onRightClick(const QPoint& pos)
{
	// upstream NM_RCLICK: one file: activate it and show the tab context menu; several: the multi-file menu
	QTreeWidgetItem* clicked = _fileListView->itemAt(pos);
	const TaskLstFnStatus* tlfs = statusOfItem(clicked);
	if (!tlfs)
		return;
	if (!clicked->isSelected())
	{
		_fileListView->clearSelection();
		clicked->setSelected(true);
	}

	const QPoint globalPos = _fileListView->viewport()->mapToGlobal(pos);
	if (nbSelectedFiles() == 1)
	{
		activateDoc(tlfs);
		if (_host._showTabContextMenu)
			_host._showTabContextMenu(globalPos);
	}
	else if (nbSelectedFiles() > 1)
	{
		_multiFilesMenu->popup(globalPos);
	}
}

bool VerticalFileSwitcher::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == _fileListView->viewport())
	{
		if (event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button() == Qt::MiddleButton)
		{
			// upstream WM_MBUTTONUP: close the document under the mouse
			if (const TaskLstFnStatus* tlfs = statusOfItem(_fileListView->itemAt(static_cast<QMouseEvent*>(event)->position().toPoint())))
			{
				closeDoc(tlfs);
				return true;
			}
		}
		else if (event->type() == QEvent::MouseButtonDblClick)
		{
			// upstream NM_DBLCLK on an empty area: a new document
			if (!_fileListView->itemAt(static_cast<QMouseEvent*>(event)->position().toPoint()))
			{
				if (_host._command)
					_host._command(IDM_FILE_NEW);
				return true;
			}
		}
		else if (event->type() == QEvent::Resize)
		{
			resizeColumns();
		}
	}
	else if (watched == _fileListView && event->type() == QEvent::KeyPress)
	{
		const int key = static_cast<QKeyEvent*>(event)->key();
		if (key == Qt::Key_Return || key == Qt::Key_Enter)
		{
			// upstream LVN_KEYDOWN VK_RETURN
			if (const TaskLstFnStatus* tlfs = statusOfItem(_fileListView->currentItem()))
				activateDoc(tlfs);
			return true;
		}
	}
	return DockingPanel::eventFilter(watched, event);
}

// ---- colours

void VerticalFileSwitcher::setBackgroundColor(COLORREF bgColour)
{
	_bgColour = QColor(GetRValue(bgColour), GetGValue(bgColour), GetBValue(bgColour));
	updateColours();
}

void VerticalFileSwitcher::setForegroundColor(COLORREF fgColour)
{
	_fgColour = QColor(GetRValue(fgColour), GetGValue(fgColour), GetBValue(fgColour));
	updateColours();
}

void VerticalFileSwitcher::updateColours()
{
	// upstream ListView_SetBkColor / SetTextColor: the editor theme's default colours
	QString qss;
	if (_bgColour.isValid())
		qss += QStringLiteral("background: %1; ").arg(_bgColour.name());
	if (_fgColour.isValid())
		qss += QStringLiteral("color: %1; ").arg(_fgColour.name());
	_fileListView->setStyleSheet(qss.isEmpty() ? QString() : QStringLiteral("QTreeView { %1}").arg(qss));
}
