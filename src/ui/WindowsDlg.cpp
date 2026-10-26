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

#include "WindowsDlg.h"

#include <algorithm>
#include <cwctype>
#include <string>

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "LocalizationQt.h"
#include "Buffer.h"
#include "Common.h"
#include "DocTabView.h"
#include "Parameters.h"
#include "localization.h"
#include "menuCmdID.h"

using namespace std;

#define WD_ROOTNODE					"WindowsDlg"
#define WD_CLMNNAME					"ColumnName"
#define WD_CLMNPATH					"ColumnPath"
#define WD_CLMNTYPE					"ColumnType"
#define WD_CLMNSIZE					"ColumnSize"
#define WD_CLMNDT                   "ColumnDateTime"
#define WD_NBDOCSTOTAL				"NbDocsTotal"
#define WD_MENUCOPYNAME				"MenuCopyName"
#define WD_MENUCOPYPATH				"MenuCopyPath"

static const wchar_t *readonlyString = L" [Read Only]";

namespace
{
	struct NumericStringEquivalence
	{
		int operator()(const wchar_t* s1, const wchar_t* s2) const
		{
			return numstrcmp(s1, s2);
		}

		static int numstrcmp(const wchar_t *str1, const wchar_t *str2)
		{
			wchar_t *p1 = nullptr, *p2 = nullptr;
			int c1 = 0, c2 = 0, lcmp = 0;
			for (;;)
			{
				if (*str1 == 0 || *str2 == 0)
				{
					if (*str1 != *str2)
						lcmp = *str1 - *str2;
					break;
				}
				if (std::iswdigit(*str1) && std::iswdigit(*str2))
				{
					lcmp = wcstol(str1, &p1, 10) - wcstol(str2, &p2, 10);
					if ( lcmp == 0 )
						lcmp = static_cast<int>((p2 - str2) - (p1 - str1));
					if ( lcmp != 0 )
						break;
					str1 = p1, str2 = p2;
				}
				else
				{
					if (*str1 < 128 && std::iswupper(*str1)) // NPLX: was ::iswascii
						c1 = towlower(*str1);
					else
						c1 = *str1;
					if (*str2 < 128 && std::iswupper(*str2))
						c2 = towlower(*str2);
					else
						c2 = *str2;
					lcmp = (c1 - c2);
					if (lcmp != 0)
						break;
					++str1, ++str2;
				}
			}
			return ( lcmp < 0 ) ? -1 : (lcmp > 0 ? 1 : 0);
		}
	};

	struct BufferEquivalent
	{
		NumericStringEquivalence _strequiv{};
		DocTabView* _pTab = nullptr;
		int _iColumn = 0;
		bool _reverse = false;
		BufferEquivalent(DocTabView* pTab, int iColumn, bool reverse)
			: _pTab(pTab), _iColumn(iColumn), _reverse(reverse)
		{}

		bool operator()(int i1, int i2) const
		{
			if (i1 == i2) return false; // equivalence test not equality
			if (_reverse) std::swap(i1, i2);
			return compare(i1, i2);
		}

		static inline unsigned long long to_u64(const FILETIME& ft)
		{
			return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; // 100-ns ticks since 1601 UTC
		}

		bool compare(int i1, int i2) const
		{
			if (_iColumn >= 0 && _iColumn <= 4)
			{
				BufferID bid1 = _pTab->getBufferByIndex(i1);
				BufferID bid2 = _pTab->getBufferByIndex(i2);
				Buffer * b1 = MainFileManager.getBufferByID(bid1);
				Buffer * b2 = MainFileManager.getBufferByID(bid2);

				if (_iColumn == 0)
				{
					const wchar_t *s1 = b1->getFileName();
					const wchar_t *s2 = b2->getFileName();
					int result = _strequiv(s1, s2);

					if (result != 0) // default to filepath sorting when equivalent
						return result < 0;
				}
				else if (_iColumn == 2)
				{
					NppParameters & nppParameters = NppParameters::getInstance();
					const wchar_t *s1;
					const wchar_t *s2;

					Lang *lang1 = nppParameters.getLangFromID(b1->getLangType());
					if (lang1)
						s1 = lang1->getLangName();
					else
						s1 = L"";

					Lang *lang2 = nppParameters.getLangFromID(b2->getLangType());
					if (lang2)
						s2 = lang2->getLangName();
					else
						s2 = L"";

					int result = _strequiv(s1, s2);

					if (result != 0) // default to filepath sorting when equivalent
						return result < 0;
				}
				else if (_iColumn == 3)
				{
					auto t1 = b1->docLength();
					auto t2 = b2->docLength();

					if (t1 != t2) // default to filepath sorting when equivalent
						return (t1 < t2);
				}
				else if (_iColumn == 4)
				{
					const auto v1 = to_u64(b1->getLastModifiedTimestamp());
					const auto v2 = to_u64(b2->getLastModifiedTimestamp());

					if (v1 != v2)
						return (v1 < v2);
				}

				// _iColumn == 1
				const wchar_t *s1 = b1->getFullPathName();
				const wchar_t *s2 = b2->getFullPathName();
				return _strequiv(s1, s2) < 0;	//we can compare the full path to sort on directory, since after sorting directories sorting files is the second thing to do (if directories are the same that is)
			}
			return false;
		}
	};

	QString columnTitle(const wchar_t* defaultStr, const char* node)
	{
		NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
		return QString::fromStdWString(pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(defaultStr, WD_ROOTNODE, node) : defaultStr);
	}
}

QRect WindowsDlg::_lastKnownLocation;

WindowsDlg::WindowsDlg(QWidget* parent)
	: QDialog(parent)
{
	// upstream IDD_WINDOWS: the list on the left, the buttons on the right (WindowsDlgMap)
	setWindowTitle(QStringLiteral("Windows"));
	setSizeGripEnabled(true);

	_hList = new QTreeWidget(this);
	_hList->setRootIsDecorated(false);
	_hList->setUniformRowHeights(true);
	_hList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	_hList->setAllColumnsShowFocus(true);
	_hList->setColumnCount(5);
	_hList->header()->setSectionsClickable(true);
	_hList->header()->setSectionsMovable(true); // LVS_EX_HEADERDRAGDROP
	_hList->header()->setStretchLastSection(false);
	_hList->setContextMenuPolicy(Qt::CustomContextMenu);
	_hList->installEventFilter(this);

	_activateButton = new QPushButton(tr("&Activate"), this);
	_activateButton->setDefault(true);
	_saveButton = new QPushButton(tr("&Save"), this);
	_closeButton = new QPushButton(tr("&Close window(s)"), this);
	_sortButton = new QPushButton(tr("Sort &tabs"), this);
	auto* okButton = new QPushButton(tr("&OK"), this);

	// the upstream control IDs, for the translations (<Dialog><Window>; WindowsDlgRc.h: IDD_WINDOWS 7000)
	nplx::setCtrlId(_activateButton, IDOK);
	nplx::setCtrlId(okButton, IDCANCEL);
	nplx::setCtrlId(_saveButton, 7000 + 2);  // IDC_WINDOWS_SAVE
	nplx::setCtrlId(_closeButton, 7000 + 3); // IDC_WINDOWS_CLOSE
	nplx::setCtrlId(_sortButton, 7000 + 4);  // IDC_WINDOWS_SORT

	auto* buttons = new QVBoxLayout();
	buttons->addWidget(_activateButton);
	buttons->addWidget(_saveButton);
	buttons->addWidget(_closeButton);
	buttons->addWidget(_sortButton);
	buttons->addStretch(1);
	buttons->addWidget(okButton);

	auto* layout = new QHBoxLayout(this);
	layout->addWidget(_hList, 1);
	layout->addLayout(buttons);

	resize(780, 420);
	updateColumnNames();

	// upstream WM_COMMAND
	connect(_activateButton, &QPushButton::clicked, this, [this]() { activateCurrent(); });
	connect(_saveButton, &QPushButton::clicked, this, [this]() { doSave(); });
	connect(_closeButton, &QPushButton::clicked, this, [this]() { doClose(); });
	connect(_sortButton, &QPushButton::clicked, this, [this]() { sortTabs(); });
	connect(okButton, &QPushButton::clicked, this, [this]() { reject(); });

	// upstream WM_NOTIFY
	connect(_hList->header(), &QHeaderView::sectionClicked, this, [this](int column) { columnClick(column); });
	connect(_hList, &QTreeWidget::itemSelectionChanged, this, [this]() { updateButtonState(); });
	connect(_hList, &QTreeWidget::itemDoubleClicked, this, [this]() { activateCurrent(); });
	connect(_hList, &QWidget::customContextMenuRequested, this, [this]() {
		// upstream WM_CONTEXTMENU: Copy Name(s) / Copy Pathname(s)
		QMenu menu(this);
		const bool enableMenu = !selectedRows().empty();
		QAction* copyName = menu.addAction(columnTitle(L"Copy Name(s)", WD_MENUCOPYNAME));
		QAction* copyPath = menu.addAction(columnTitle(L"Copy Pathname(s)", WD_MENUCOPYPATH));
		copyName->setEnabled(enableMenu);
		copyPath->setEnabled(enableMenu);
		QAction* chosen = menu.exec(QCursor::pos());
		if (chosen == copyName)
			putItemsToClipboard(false);
		else if (chosen == copyPath)
			putItemsToClipboard(true);
	});
}

void WindowsDlg::init(DocTabView *pTab, std::function<void(NMWINDLG&)> notify)
{
	_pTab = pTab;
	_notify = std::move(notify);
}

int WindowsDlg::doDialog()
{
	// upstream onInitDialog
	_currentColumn = -1;
	_lastSort = -1;
	_reverseSort = false;

	updateColumnNames();
	_hList->setColumnWidth(0, width() / 4);
	_hList->setColumnWidth(1, 300);
	_hList->setColumnWidth(2, 100);
	_hList->setColumnWidth(3, 100);
	_hList->setColumnWidth(4, 140);

	// upstream ListView_SetBkColor / SetTextColor: the editor theme's default colours
	const NppParameters& nppParam = NppParameters::getInstance();
	const COLORREF fgColor = nppParam.getCurrentDefaultFgColor();
	const COLORREF bgColor = nppParam.getCurrentDefaultBgColor();
	_hList->setStyleSheet(QStringLiteral("QTreeView { background: %1; color: %2; }")
		.arg(QColor(GetRValue(bgColor), GetGValue(bgColor), GetBValue(bgColor)).name(), QColor(GetRValue(fgColor), GetGValue(fgColor), GetBValue(fgColor)).name()));

	if (_lastKnownLocation.isValid())
		setGeometry(_lastKnownLocation);

	doRefresh(true);
	nplx::changeDlgLang(this, "Window");
	return exec();
}

void WindowsDlg::updateColumnNames()
{
	// upstream: "⇵" on the sortable columns, "▽" / "△" on the sorted one
	struct Column { const wchar_t* _text; const char* _node; };
	static constexpr Column columns[] = {
		{ L"Name", WD_CLMNNAME }, { L"Path", WD_CLMNPATH }, { L"Type", WD_CLMNTYPE }, { L"Size", WD_CLMNSIZE }, { L"Modified time", WD_CLMNDT }
	};
	QStringList titles;
	for (int i = 0; i < 5; ++i)
	{
		QString columnText = columnTitle(columns[i]._text, columns[i]._node);
		if (_currentColumn != i)
			columnText = QStringLiteral("⇵ ") + columnText;
		else if (_reverseSort)
			columnText = QStringLiteral("△ ") + columnText;
		else
			columnText = QStringLiteral("▽ ") + columnText;
		titles << columnText;
	}
	_hList->setHeaderLabels(titles);
	_hList->headerItem()->setTextAlignment(2, Qt::AlignCenter);
	_hList->headerItem()->setTextAlignment(3, Qt::AlignCenter);
}

void WindowsDlg::sortTabs()
{
	// upstream IDC_WINDOWS_SORT
	if (_currentColumn == -1)
	{
		// no sort column selected yet: sort by file name (upstream)
		_currentColumn = 0;
		_reverseSort = false;
		_lastSort = _currentColumn;

		updateColumnNames();
		doColumnSort();
	}
	doSortToTabs();
	doColumnSort();
}

void WindowsDlg::columnClick(int column)
{
	// upstream LVN_COLUMNCLICK: sort columns with stable sort
	_currentColumn = column;
	if (_lastSort == _currentColumn)
	{
		_reverseSort = true;
		_lastSort = -1;
	}
	else
	{
		_reverseSort = false;
		_lastSort = _currentColumn;
	}
	updateColumnNames();
	doColumnSort();
}

std::vector<int> WindowsDlg::selectedRows() const
{
	std::vector<int> rows;
	for (int i = 0, n = _hList->topLevelItemCount(); i < n; ++i)
	{
		if (_hList->topLevelItem(i)->isSelected())
			rows.push_back(i);
	}
	return rows;
}

void WindowsDlg::selectRow(int row, bool select)
{
	if (QTreeWidgetItem* item = _hList->topLevelItem(row))
		item->setSelected(select);
}

void WindowsDlg::fillList()
{
	// upstream LVN_GETDISPINFO for each row (owner data)
	QSignalBlocker blocker(_hList);
	const std::vector<int> selected = selectedRows();
	_hList->clear();
	for (int row = 0; row < static_cast<int>(_idxMap.size()); ++row)
	{
		Buffer* buf = getBuffer(row);
		if (!buf)
			continue;
		auto* item = new QTreeWidgetItem(_hList);

		// file name
		wstring text = buf->getFileName();
		if (buf->isDirty())
			text += '*';
		else if (buf->isReadOnly())
			text += readonlyString;
		item->setText(0, QString::fromStdWString(text));

		// directory
		const wstring fullName = buf->getFullPathName();
		const wstring fileName = buf->getFileName();
		item->setText(1, QString::fromStdWString(fullName.substr(0, fullName.size() - fileName.size())));

		// Type
		NppParameters& nppParameters = NppParameters::getInstance();
		Lang* lang = nppParameters.getLangFromID(buf->getLangType());
		item->setText(2, lang ? QString::fromWCharArray(lang->getLangName()) : QString());
		item->setTextAlignment(2, Qt::AlignCenter);

		// size
		item->setText(3, QString::number(buf->docLength()));
		item->setTextAlignment(3, Qt::AlignCenter);

		// Modified time
		const FILETIME ft = buf->getLastModifiedTimestamp();
		if (ft.dwLowDateTime != 0 || ft.dwHighDateTime != 0)
		{
			// NPLX: was FileTimeToLocalFileTime + FileTimeToSystemTime; "YYYY-MM-DD HH:MM:SS" like upstream
			const unsigned long long ticks = BufferEquivalent::to_u64(ft);
			const qint64 msecs = static_cast<qint64>(ticks / 10000ULL) - 11644473600000LL; // 1601 -> 1970
			item->setText(4, QDateTime::fromMSecsSinceEpoch(msecs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
		}
	}
	for (int row : selected)
		selectRow(row, true);
}

void WindowsDlg::doColumnSort()
{
	if (_currentColumn == -1)
		return;

	size_t i = 0;
	size_t n = _idxMap.size();
	vector<int> sortMap;
	sortMap.resize(n);

	for (; i < n; ++i)
	{
		QTreeWidgetItem* item = _hList->topLevelItem(static_cast<int>(i));
		sortMap[_idxMap[i]] = (item && item->isSelected()) ? 1 : 0;
	}

	stable_sort(_idxMap.begin(), _idxMap.end(), BufferEquivalent(_pTab, _currentColumn, _reverseSort));
	fillList();
	{
		QSignalBlocker blocker(_hList);
		for (i = 0; i < n; ++i)
			selectRow(static_cast<int>(i), sortMap[_idxMap[i]] != 0);
	}

	updateButtonState();
}

void WindowsDlg::updateButtonState()
{
	const size_t nSelection = selectedRows().size();
	if (nSelection == 0)
	{
		_activateButton->setEnabled(false);
		_saveButton->setEnabled(false);
		_closeButton->setEnabled(false);
	}
	else
	{
		_saveButton->setEnabled(true);
		_closeButton->setEnabled(true);
		_activateButton->setEnabled(nSelection == 1);
	}
	_sortButton->setEnabled(true);
}

void WindowsDlg::doRefresh(bool force)
{
	if (!force && !isVisible())
		return;

	size_t count = (_pTab != NULL) ? _pTab->nbItem() : 0;
	size_t oldSize = _idxMap.size();
	if (!force && count == oldSize)
		return;

	if (count != oldSize)
	{
		size_t lo = 0;
		_idxMap.resize(count);
		if (oldSize < count)
			lo = oldSize;
		for (size_t i = lo; i < count; ++i)
			_idxMap[i] = int(i);
	}
	fillList();
	resetSelection();
	updateButtonState();
	doCount();
}

void WindowsDlg::resetSelection()
{
	if (!_pTab)
		return;

	auto curSel = _pTab->getCurrentTabIndex();
	int pos = 0;
	QSignalBlocker blocker(_hList);
	for (vector<int>::iterator itr = _idxMap.begin(), end = _idxMap.end(); itr != end; ++itr, ++pos)
	{
		if (*itr == curSel)
		{
			selectRow(pos, true);
			_hList->setCurrentItem(_hList->topLevelItem(pos), 0, QItemSelectionModel::NoUpdate);
			_hList->scrollToItem(_hList->topLevelItem(pos));
		}
		else
		{
			selectRow(pos, false);
		}
	}
}

void WindowsDlg::doSave()
{
	NMWINDLG nmdlg{};
	nmdlg.type = WDT_SAVE;
	const std::vector<int> rows = selectedRows();
	if (rows.empty())
		return;
	nmdlg.curSel = rows.front();
	nmdlg.nItems = static_cast<unsigned int>(rows.size());
	std::vector<unsigned int> items;
	for (int row : rows)
		items.push_back(_idxMap[row]);
	nmdlg.Items = items.data();
	if (_notify)
		_notify(nmdlg);
	fillList();
	{
		QSignalBlocker blocker(_hList);
		for (int row : rows)
			selectRow(row, true);
	}
}

void WindowsDlg::reject()
{
	_lastKnownLocation = geometry();
	QDialog::reject();
}

void WindowsDlg::activateCurrent()
{
	const std::vector<int> rows = selectedRows();
	if (rows.size() == 1)
	{
		NMWINDLG nmdlg{};
		nmdlg.type = WDT_ACTIVATE;
		nmdlg.curSel = _idxMap[rows.front()];
		if (_notify)
			_notify(nmdlg);
		_lastKnownLocation = geometry();
		accept();
	}
}

void WindowsDlg::doClose()
{
	NMWINDLG nmdlg{};
	nmdlg.type = WDT_CLOSE;
	const std::vector<int> rows = selectedRows();
	if (rows.empty()) return;
	int index = rows.front();
	nmdlg.curSel = _idxMap[index];
	const unsigned int n = nmdlg.nItems = static_cast<unsigned int>(rows.size());
	std::vector<unsigned int> items(n);
	vector<int> key;
	key.resize(n, 0x7fffffff);

	{
		QSignalBlocker blocker(_hList);
		for (unsigned int j = 0; j < n; ++j)
		{
			selectRow(rows[j], false); // deselect
			items[j] = _idxMap[rows[j]];
			key[j] = rows[j];
		}
	}
	nmdlg.Items = items.data();

	if (_notify)
		_notify(nmdlg);
	if (nmdlg.processed)
	{
		// Trying to retain sort order. fairly sure there is a much better algorithm for this
		vector<int>::iterator kitr = key.begin();
		for (unsigned int k = 0; k < n; ++k, ++kitr)
		{
			if (nmdlg.Items[k] == static_cast<unsigned int>(-1))
			{
				int oldVal = _idxMap[*kitr];
				_idxMap[*kitr] = -1;
				for (vector<int>::iterator itr = _idxMap.begin(), end = _idxMap.end(); itr != end; ++itr)
					if (*itr > oldVal)
						--(*itr);
			}
		}
		_idxMap.erase(remove(_idxMap.begin(), _idxMap.end(), -1), _idxMap.end());
	}

	if (_idxMap.size() < 1)
	{
		reject();
		return;
	}
	else if (_pTab->nbItem() != _idxMap.size())	//if the nb of items is not equal to the nb of tabs, refresh the list
		doRefresh(true);
	else
	{
		fillList();
		// select first previously selected item (or last one if only the last one was removed)
		if (index == static_cast<int>(_idxMap.size()))
			index -= 1;

		if (index >= 0)
		{
			QSignalBlocker blocker(_hList);
			selectRow(index, true);
		}
	}
	updateButtonState();
	doCount();
}

void WindowsDlg::doCount()
{
	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

	wstring msg = pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(L"Windows", "Dialog", "Window", "title") : L"Windows";
	msg += L" - ";
	msg += pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(L"Total documents: ", WD_ROOTNODE, WD_NBDOCSTOTAL) : L"Total documents: ";
	msg += L" ";
	msg += to_wstring(_idxMap.size());
	setWindowTitle(QString::fromStdWString(msg));
}

void WindowsDlg::doSort()
{
	if (!_pTab)
		return;

	size_t count =  _pTab->nbItem();
	std::vector<unsigned int> items(count);
	auto currrentTabIndex = _pTab->getCurrentTabIndex();
	NMWINDLG nmdlg{};
	nmdlg.type = WDT_SORT;
	nmdlg.curSel = currrentTabIndex;
	nmdlg.nItems = static_cast<unsigned int>(count);
	nmdlg.Items = items.data();
	for (size_t i=0; i < count; ++i)
	{
		nmdlg.Items[i] = _idxMap[i];
	}
	if (_notify)
		_notify(nmdlg);
	if (nmdlg.processed)
	{
		_idxMap.clear();
		refreshMap();
	}

	//After sorting, need to open the active tab before sorting
	//This will be helpful when large number of documents are opened
	long long newPosition = -1;
	std::vector<int>::iterator it = std::find(_idxMap.begin(), _idxMap.end(), currrentTabIndex);
	if (it != _idxMap.end())
	{
		newPosition = it - _idxMap.begin();
	}
	nmdlg.type = WDT_ACTIVATE;
	nmdlg.curSel = static_cast<unsigned int>(newPosition);
	if (_notify)
		_notify(nmdlg);
}

void WindowsDlg::sort(int columnID, bool reverseSort)
{
	refreshMap();
	_currentColumn = columnID;
	_reverseSort = reverseSort;
	stable_sort(_idxMap.begin(), _idxMap.end(), BufferEquivalent(_pTab, _currentColumn, _reverseSort));
}

void WindowsDlg::sortFileNameASC() { sort(0, false); }
void WindowsDlg::sortFileNameDSC() { sort(0, true); }
void WindowsDlg::sortFilePathASC() { sort(1, false); }
void WindowsDlg::sortFilePathDSC() { sort(1, true); }
void WindowsDlg::sortFileTypeASC() { sort(2, false); }
void WindowsDlg::sortFileTypeDSC() { sort(2, true); }
void WindowsDlg::sortFileSizeASC() { sort(3, false); }
void WindowsDlg::sortFileSizeDSC() { sort(3, true); }
void WindowsDlg::sortDateTimeASC() { sort(4, false); }
void WindowsDlg::sortDateTimeDSC() { sort(4, true); }

void WindowsDlg::refreshMap()
{
	size_t count = (_pTab != NULL) ? _pTab->nbItem() : 0;
	size_t oldSize = _idxMap.size();
	if (count == oldSize)
		return;

	size_t lo = 0;
	_idxMap.resize(count);
	if (oldSize < count)
		lo = oldSize;
	for (size_t i = lo; i < count; ++i)
		_idxMap[i] = int(i);
}

void WindowsDlg::doSortToTabs()
{
	const std::vector<int> rows = selectedRows();
	int curSel = rows.empty() ? 0 : rows.front();

	NMWINDLG nmdlg{};
	nmdlg.type = WDT_SORT;
	nmdlg.curSel = _idxMap[curSel];
	nmdlg.nItems = static_cast<unsigned int>(_idxMap.size());
	std::vector<unsigned int> items(nmdlg.nItems);
	for (unsigned int j = 0; j < nmdlg.nItems; ++j)
	{
		items[j] = _idxMap[j];
		if (static_cast<int>(j) == curSel)
			nmdlg.curSel = j;
	}
	nmdlg.Items = items.data();
	if (_notify)
		_notify(nmdlg);
	if (nmdlg.processed)
	{
		_idxMap.clear();
		doRefresh(true);
	}
}

void WindowsDlg::putItemsToClipboard(bool isFullPath)
{
	// upstream buf2Clipboard (CRLF) - NPLX: LF line ends
	wstring selection;
	for (int row : selectedRows())
	{
		if (Buffer* buf = getBuffer(row))
		{
			selection += isFullPath ? buf->getFullPathName() : buf->getFileName();
			selection += L"\n";
		}
	}
	if (!selection.empty())
		QApplication::clipboard()->setText(QString::fromStdWString(selection));
}

Buffer* WindowsDlg::getBuffer(int index) const
{
	if (index < 0 || index >= static_cast<int>(_idxMap.size()))
		return nullptr;

	index = _idxMap[index];
	if (index < 0 || !_pTab || index >= static_cast<int>(_pTab->nbItem()))
		return nullptr;

	BufferID bufID = _pTab->getBufferByIndex(index);
	return MainFileManager.getBufferByID(bufID);
}

void WindowsDlg::typeAhead(QChar ch)
{
	// upstream WM_CHAR_REPLACEMENT: select the next item starting with the typed character
	const int itemCount = _hList->topLevelItemCount();
	std::vector<bool> stateMap;
	for (int index = 0; index < itemCount; index++)
		stateMap.push_back(_hList->topLevelItem(index)->isSelected());

	int firstMatchFound = -1;
	int lastMatchSel = -1;
	int targetedIndex = -1;
	for (int index = 0; index < itemCount; ++index)
	{
		const QString text = _hList->topLevelItem(index)->text(0);
		if (!text.isEmpty() && text.at(0).toLower() == ch.toLower())
		{
			if (firstMatchFound == -1)
				firstMatchFound = index;

			if (stateMap.at(index))
			{
				lastMatchSel = index;
				continue;
			}

			if (lastMatchSel != -1)
			{
				targetedIndex = index;
				break;
			}
		}
	}

	const int target = (targetedIndex != -1) ? targetedIndex : firstMatchFound;
	if (target != -1)
	{
		_hList->clearSelection();
		_hList->setCurrentItem(_hList->topLevelItem(target));
		_hList->scrollToItem(_hList->topLevelItem(target));
	}
	else if (!NppParameters::getInstance().getNppGUI()._muteSounds)
	{
		QApplication::beep(); // NPLX: was MessageBeep
	}
}

bool WindowsDlg::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == _hList && event->type() == QEvent::KeyPress)
	{
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		const Qt::KeyboardModifiers mods = keyEvent->modifiers();
		// upstream LVN_KEYDOWN: Ctrl+A selects all, Ctrl+C copies the pathnames
		if (keyEvent->key() == Qt::Key_A && mods == Qt::ControlModifier)
		{
			_hList->selectAll();
			return true;
		}
		if (keyEvent->key() == Qt::Key_C && (mods & Qt::ControlModifier))
		{
			if (!selectedRows().empty())
				putItemsToClipboard(true);
			return true;
		}
		if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
		{
			activateCurrent();
			return true;
		}
		// upstream WM_CHAR hook: type-ahead on the file names
		const QString text = keyEvent->text();
		if (text.size() == 1 && text.at(0).isPrint() && !(mods & (Qt::ControlModifier | Qt::AltModifier)))
		{
			typeAhead(text.at(0));
			return true;
		}
	}
	return QDialog::eventFilter(watched, event);
}
