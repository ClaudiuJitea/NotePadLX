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

#include "ShortcutMapper.h"

#include <cstdint>
#include <string>
#include <vector>

#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTabBar>

#include "LocalizationQt.h"
#include "BabyGridWrapper.h"
#include "Common.h"
#include "NppMenu.h"
#include "Parameters.h"
#include "ShortcutDlg.h"
#include "localization.h"
#include "menuCmdID.h"
#include "resource.h"
#include "shortcut.h"
#include "shortcutRc.h"

using namespace std;

namespace
{
#include "RcShortcutMapper.inc"

NativeLangSpeaker* langSpeaker()
{
	return NppParameters::getInstance().getNativeLangSpeaker();
}

wstring mapperLangStr(const char* nodeName, const wchar_t* defaultStr)
{
	NativeLangSpeaker* speaker = langSpeaker();
	return speaker ? speaker->getShortcutMapperLangStr(nodeName, defaultStr) : wstring(defaultStr);
}
}

void ShortcutMapper::initTabs()
{
	_hTabCtrl = qobject_cast<QTabBar*>(dlgItem(IDC_BABYGRID_TABBAR));
	_hTabCtrl->setExpanding(false);
	_hTabCtrl->setDrawBase(false);

	for (size_t i = 0; i < _nbTab; ++i)
	{
		_tabNames[i] = getTabString(i);
		_hTabCtrl->addTab(QString::fromStdWString(_tabNames[i]));
	}

	_hTabCtrl->setCurrentIndex(static_cast<int>(_currentState));
	connect(_hTabCtrl, &QTabBar::currentChanged, this, [this](int index) { switchToTab(index); });
}

wstring ShortcutMapper::getTabString(size_t i) const
{
	if (i >= _nbTab)
		return L"";

	switch (i)
	{
		case 1:
			return mapperLangStr("MacrosTab", L"Macros");

		case 2:
			return mapperLangStr("RunCommandsTab", L"Run commands");

		case 3:
			return mapperLangStr("PluginCommandsTab", L"Plugin commands");

		case 4:
			return mapperLangStr("ScintillaCommandsTab", L"Scintilla commands");

		default: //0
			return mapperLangStr("MainMenuTab", L"Main menu");
	}
}

void ShortcutMapper::initBabyGrid()
{
	_lastHomeRow.resize(5, 1);
	_lastCursorRow.resize(5, 1);

	_babygrid = new BabyGridWrapper(this);
	_babygrid->setObjectName(QStringLiteral("shortcutGrid"));
	_babygrid->makeColAutoWidth(true);
	_babygrid->setAutoRow(true);
	_babygrid->setColsNumbered(false);
	// NPLX: the colours follow the NotePadLX theme (upstream sets the BabyGrid colours for light and dark mode)

	_babygrid->_onCellDoubleClicked = [this](int) { onCommand(IDM_BABYGRID_MODIFY, RcNotifyClicked); };
	_babygrid->_onCellRightClicked = [this](int, const QPoint& p) { showRightClickMenu(p); };
	_babygrid->_onDeleteCell = [this]() {
		// upstream BGN_DELETECELL (VK_DELETE)
		switch(_currentState)
		{
			case STATE_MACRO:
			case STATE_USER:
				onCommand(IDM_BABYGRID_DELETE, RcNotifyClicked);
				return;
			case STATE_MENU:
			case STATE_PLUGIN:
			case STATE_SCINTILLA:
			default:
				return;
		}
	};
	_babygrid->_onRowChanged = [this](int row) { onRowChanged(row); };

	nplx::changeDlgLang(*this, "ShortcutMapper");
	_conflictInfoOk = mapperLangStr("ConflictInfoOk", L"No shortcut conflicts for this item.");
	_conflictInfoEditing = mapperLangStr("ConflictInfoEditing", L"No conflicts . . .");
}

wstring ShortcutMapper::getTextFromCombo(int id)
{
	return stringToLower(getItemText(id));
}

bool ShortcutMapper::isFilterValid(Shortcut sc)
{
	// all words in _shortcutFilter must be in the name or keycombo
	// For example, the shortcut with name "foo bar baz" and keycombo "Ctrl+A"
	// would be matched by the filter "foo ctrl" and the filter "bar +a"
	// but *not* by the filter "foo shift" or the filter "quz +a"
	size_t filterSize = _shortcutFilter.size();
	if (filterSize == 0)
		return true;
	wstring shortcut_name = stringToLower(string2wstring(sc.getName(), CP_UTF8));
	wstring shortcut_value = stringToLower(string2wstring(sc.toString(), CP_UTF8));
	for (size_t i = 0; i < filterSize; ++i)
	{
		wstring filterWord = _shortcutFilter.at(i);
		// every word must be matched by keycombo or name
		if (shortcut_name.find(filterWord) == std::string::npos &&
			shortcut_value.find(filterWord) == std::string::npos)
			return false;
	}
	return true;
}

bool ShortcutMapper::isFilterValid(PluginCmdShortcut sc)
{
	// all words in _shortcutFilter must be in the name or the keycombo or the plugin name
	size_t filterSize = _shortcutFilter.size();
	if (filterSize == 0)
		return true;
	wstring shortcut_name = stringToLower(string2wstring(sc.getName(), CP_UTF8));
	wstring shortcut_value = stringToLower(string2wstring(sc.toString(), CP_UTF8));
	wstring module_name = stringToLower(string2wstring(sc.getModuleName(), CP_UTF8));
	for (size_t i = 0; i < filterSize; ++i)
	{
		wstring filterWord = _shortcutFilter.at(i);
		// every word must be matched by keycombo or name or plugin name
		if (shortcut_name.find(filterWord) == std::string::npos &&
			shortcut_value.find(filterWord) == std::string::npos &&
			module_name.find(filterWord) == std::string::npos)
			return false;
	}
	return true;
}

bool ShortcutMapper::isFilterValid(ScintillaKeyMap sc)
{
	// all words in _shortcutFilter must be in the name or the list of keycombos
	size_t filterSize = _shortcutFilter.size();
	if (filterSize == 0)
		return true;
	wstring shortcut_name = stringToLower(string2wstring(sc.getName(), CP_UTF8));
	wstring shortcut_value = stringToLower(string2wstring(sc.toString(), CP_UTF8));
	for (size_t i = 0; i < filterSize; ++i)
	{
		wstring filterWord = _shortcutFilter.at(i);
		// every word must be matched by keycombo or name
		if (shortcut_name.find(filterWord) == std::string::npos &&
			shortcut_value.find(filterWord) == std::string::npos)
			return false;
	}
	return true;
}

void ShortcutMapper::fillOutBabyGrid()
{
	NppParameters& nppParam = NppParameters::getInstance();
	_babygrid->clear();
	_babygrid->setInitialContent(true);
	_shortcutIndex.clear();

	size_t nbItems = 0;
	wstring nameStr = mapperLangStr("ColumnName", L"Name");
	wstring shortcutStr = mapperLangStr("ColumnShortcut", L"Shortcut");

	switch(_currentState)
	{
		case STATE_MENU:
		{
			nbItems = nppParam.getUserShortcuts().size();
			_babygrid->setLineColNumber(nbItems, 3);
			wstring categoryStr = mapperLangStr("ColumnCategory", L"Category");
			_babygrid->setText(0, 3, categoryStr.c_str());
		}
		break;

		case STATE_MACRO:
		{
			nbItems = nppParam.getMacroList().size();
			_babygrid->setLineColNumber(nbItems, 2);
		}
		break;

		case STATE_USER:
		{
			nbItems = nppParam.getUserCommandList().size();
			_babygrid->setLineColNumber(nbItems, 2);
		}
		break;

		case STATE_PLUGIN:
		{
			nbItems = nppParam.getPluginCommandList().size();
			_babygrid->setLineColNumber(nbItems, 3);
			wstring pluginStr = mapperLangStr("ColumnPlugin", L"Plugin");
			_babygrid->setText(0, 3, pluginStr.c_str());
		}
		break;

		case STATE_SCINTILLA:
		{
			nbItems = nppParam.getScintillaKeyList().size();
			_babygrid->setLineColNumber(nbItems, 2);
		}
		break;
	}
	// NPLX: the titles after the column count is set (setLineColNumber resizes the header)
	_babygrid->setText(0, 1, nameStr.c_str());
	_babygrid->setText(0, 2, shortcutStr.c_str());

	bool isMarker = false;
	size_t cs_index = 0;

	// make _shortcutFilter a list of the words in IDC_BABYGRID_FILTER
	wstring shortcutFilterStr = getTextFromCombo(IDC_BABYGRID_FILTER);
	const wstring whitespace(L" ");
	std::vector<wstring> shortcutFilterWithEmpties;
	stringSplit(shortcutFilterStr, whitespace, shortcutFilterWithEmpties);
	// now add only the non-empty strings in the split list to _shortcutFilter
	_shortcutFilter = std::vector<wstring>();
	for (size_t i = 0; i < shortcutFilterWithEmpties.size(); ++i)
	{
		wstring filterWord = shortcutFilterWithEmpties.at(i);
		if (!filterWord.empty())
			_shortcutFilter.push_back(filterWord);
	}

	switch(_currentState)
	{
		case STATE_MENU:
		{
			vector<CommandShortcut> & cshortcuts = nppParam.getUserShortcuts();
			cs_index = 1;
			for (size_t i = 0; i < nbItems; ++i)
			{
				if (_host._isRemovedCommand && _host._isRemovedCommand(static_cast<int>(cshortcuts[i].getID())))
					continue; // NPLX: Windows-only (e.g. the WinGUp updater)
				if (isFilterValid(cshortcuts[i]))
				{
					if (findKeyConflicts(nullptr, cshortcuts[i].getKeyCombo(), i))
						isMarker = _babygrid->setMarker(true);

					_babygrid->setText(cs_index, 1, string2wstring(cshortcuts[i].getName(), CP_UTF8).c_str());
					if (cshortcuts[i].isEnabled()) //avoid empty strings for better performance
						_babygrid->setText(cs_index, 2, string2wstring(cshortcuts[i].toString(), CP_UTF8).c_str());
					_babygrid->setText(cs_index, 3, cshortcuts[i].getCategory());
					if (isMarker)
						isMarker = _babygrid->setMarker(false);
					_shortcutIndex.push_back(i);
					cs_index++;
				}
			}
			_babygrid->setLineColNumber(cs_index - 1 , 3);
			enableItem(IDM_BABYGRID_MODIFY, true);
			enableItem(IDM_BABYGRID_CLEAR, true);
			enableItem(IDM_BABYGRID_DELETE, false);
		}
		break;

		case STATE_MACRO:
		{
			vector<MacroShortcut> & cshortcuts = nppParam.getMacroList();
			cs_index = 1;
			for (size_t i = 0; i < nbItems; ++i)
			{
				if (isFilterValid(cshortcuts[i]))
				{
					if (findKeyConflicts(nullptr, cshortcuts[i].getKeyCombo(), i))
						isMarker = _babygrid->setMarker(true);

					_babygrid->setText(cs_index, 1, string2wstring(cshortcuts[i].getName(), CP_UTF8).c_str());
					if (cshortcuts[i].isEnabled()) //avoid empty strings for better performance
						_babygrid->setText(cs_index, 2, string2wstring(cshortcuts[i].toString(), CP_UTF8).c_str());

					if (isMarker)
						isMarker = _babygrid->setMarker(false);
					_shortcutIndex.push_back(i);
					cs_index++;
				}
			}
			_babygrid->setLineColNumber(cs_index - 1 , 2);

			bool shouldBeEnabled = nbItems > 0;
			enableItem(IDM_BABYGRID_MODIFY, shouldBeEnabled);
			enableItem(IDM_BABYGRID_CLEAR, shouldBeEnabled);
			enableItem(IDM_BABYGRID_DELETE, shouldBeEnabled);
		}
		break;

		case STATE_USER:
		{
			vector<UserCommand> & cshortcuts = nppParam.getUserCommandList();
			cs_index = 1;
			for (size_t i = 0; i < nbItems; ++i)
			{
				if (isFilterValid(cshortcuts[i]))
				{
					if (findKeyConflicts(nullptr, cshortcuts[i].getKeyCombo(), i))
						isMarker = _babygrid->setMarker(true);

					_babygrid->setText(cs_index, 1, string2wstring(cshortcuts[i].getName(), CP_UTF8).c_str());
					if (cshortcuts[i].isEnabled()) //avoid empty strings for better performance
						_babygrid->setText(cs_index, 2, string2wstring(cshortcuts[i].toString(), CP_UTF8).c_str());

					if (isMarker)
						isMarker = _babygrid->setMarker(false);
					_shortcutIndex.push_back(i);
					cs_index++;
				}
			}
			_babygrid->setLineColNumber(cs_index - 1 , 2);

			bool shouldBeEnabled = nbItems > 0;
			enableItem(IDM_BABYGRID_MODIFY, shouldBeEnabled);
			enableItem(IDM_BABYGRID_CLEAR, shouldBeEnabled);
			enableItem(IDM_BABYGRID_DELETE, shouldBeEnabled);
		}
		break;

		case STATE_PLUGIN:
		{
			vector<PluginCmdShortcut> & cshortcuts = nppParam.getPluginCommandList();
			cs_index = 1;
			for (size_t i = 0; i < nbItems; ++i)
			{
				if (isFilterValid(cshortcuts[i]))
				{
					if (findKeyConflicts(nullptr, cshortcuts[i].getKeyCombo(), i))
						isMarker = _babygrid->setMarker(true);

					_babygrid->setText(cs_index, 1, string2wstring(cshortcuts[i].getName(), CP_UTF8).c_str());
					if (cshortcuts[i].isEnabled()) //avoid empty strings for better performance
						_babygrid->setText(cs_index, 2, string2wstring(cshortcuts[i].toString(), CP_UTF8).c_str());
					_babygrid->setText(cs_index, 3, string2wstring(cshortcuts[i].getModuleName(), CP_UTF8).c_str());

					if (isMarker)
						isMarker = _babygrid->setMarker(false);
					_shortcutIndex.push_back(i);
					cs_index++;
				}
			}
			_babygrid->setLineColNumber(cs_index - 1 , 3);

			bool shouldBeEnabled = nbItems > 0;
			enableItem(IDM_BABYGRID_MODIFY, shouldBeEnabled);
			enableItem(IDM_BABYGRID_CLEAR, shouldBeEnabled);
			enableItem(IDM_BABYGRID_DELETE, false);
		}
		break;

		case STATE_SCINTILLA:
		{
			vector<ScintillaKeyMap> & cshortcuts = nppParam.getScintillaKeyList();
			cs_index=1;
			for (size_t i = 0; i < nbItems; ++i)
			{
				if (isFilterValid(cshortcuts[i]))
				{
					if (cshortcuts[i].isEnabled())
					{
						size_t sciCombos = cshortcuts[i].getSize();
						for (size_t sciIndex = 0; sciIndex < sciCombos; ++sciIndex)
						{
							if (findKeyConflicts(nullptr, cshortcuts[i].getKeyComboByIndex(sciIndex), i))
							{
								isMarker = _babygrid->setMarker(true);
								break;
							}
						}
					}

					_babygrid->setText(cs_index, 1, string2wstring(cshortcuts[i].getName(), CP_UTF8).c_str());
					if (cshortcuts[i].isEnabled()) //avoid empty strings for better performance
						_babygrid->setText(cs_index, 2, string2wstring(cshortcuts[i].toString(), CP_UTF8).c_str());

					if (isMarker)
						isMarker = _babygrid->setMarker(false);
					_shortcutIndex.push_back(i);
					cs_index++;
				}
			}
			_babygrid->setLineColNumber(cs_index - 1 , 2);
			enableItem(IDM_BABYGRID_MODIFY, true);
			enableItem(IDM_BABYGRID_CLEAR, false);
			enableItem(IDM_BABYGRID_DELETE, false);
		}
		break;
	}

	_babygrid->setInitialContent(false);

	if (nbItems > 0) //restore the last view
		_babygrid->setLastView(_lastHomeRow[_currentState], _lastCursorRow[_currentState]);
	else //clear the info area
		setItemText(IDC_BABYGRID_INFO, L"");
}

void ShortcutMapper::resizeDialogElements()
{
	// upstream resizeDialogElements: the buttons centred at the bottom, the filter and the info box above them,
	// the grid between the tabs and the info box
	if (!_babygrid)
		return;

	QWidget* modBtn = dlgItem(IDM_BABYGRID_MODIFY);
	QWidget* clearBtn = dlgItem(IDM_BABYGRID_CLEAR);
	QWidget* delBtn = dlgItem(IDM_BABYGRID_DELETE);
	QWidget* okBtn = dlgItem(IDOK);
	QWidget* filterStatic = dlgItem(IDC_BABYGRID_STATIC);
	QWidget* filterEdit = dlgItem(IDC_BABYGRID_FILTER);
	QWidget* filterClear = dlgItem(IDC_BABYGRID_FILTER_CLEAR);
	QWidget* info = dlgItem(IDC_BABYGRID_INFO);

	const int padding = 8;
	const int wClient = width();
	const int center = wClient / 2;
	const int wBtn = okBtn->width();
	const int hBtn = okBtn->height();
	const int gapBtn = 8;
	const int gapBtnHalf = gapBtn / 2;

	const int btnTop = height() - padding - hBtn;
	modBtn->move(center - gapBtnHalf - wBtn * 2 - gapBtn, btnTop);
	clearBtn->move(center - gapBtnHalf - wBtn, btnTop);
	delBtn->move(center + gapBtnHalf, btnTop);
	okBtn->move(center + gapBtnHalf + wBtn + gapBtn, btnTop);

	const int heightFilter = filterEdit->height();
	const int filterTop = btnTop - gapBtn - heightFilter;
	filterStatic->move(padding, filterTop + (heightFilter - filterStatic->height()) / 2);
	const int filterLeft = filterStatic->geometry().right() + 6;
	const int clrBtnWidth = filterClear->width();
	filterEdit->setGeometry(filterLeft, filterTop, wClient - padding - clrBtnWidth - 2 - filterLeft, heightFilter);
	filterClear->setGeometry(wClient - padding - clrBtnWidth, filterTop, clrBtnWidth, heightFilter);

	const int heightInfo = info->height();
	const int infoTop = filterTop - gapBtn - heightInfo;
	info->setGeometry(padding, infoTop, wClient - 2 * padding, heightInfo);

	const int gridTop = _hTabCtrl ? _hTabCtrl->geometry().bottom() + 1 : padding;
	_hTabCtrl->setGeometry(padding, _hTabCtrl->geometry().top(), wClient - 2 * padding, _hTabCtrl->sizeHint().height());
	_babygrid->setGeometry(padding, gridTop, wClient - 2 * padding, infoTop - gapBtn - gridTop);
}

void ShortcutMapper::resizeEvent(QResizeEvent* event)
{
	RcDialog::resizeEvent(event);
	resizeDialogElements();
}

int ShortcutMapper::doDialog()
{
	// upstream WM_INITDIALOG
	buildFrom(IDD_SHORTCUTMAPPER_DLG_def);
	setWindowFlag(Qt::WindowMaximizeButtonHint, true);
	// upstream WS_THICKFRAME: resizable, no smaller than the .rc size
	setMinimumSize(size());
	setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
	resize(size().width() * 5 / 4, size().height() * 5 / 4);

	initBabyGrid();
	initTabs();
	fillOutBabyGrid();
	resizeDialogElements();
	_babygrid->setFocus();

	return exec() == QDialog::Accepted ? 0 : -1;
}

void ShortcutMapper::switchToTab(int index)
{
	// upstream WM_NOTIFY TCN_SELCHANGE
	//save the current view
	_lastHomeRow[_currentState] = _babygrid->getHomeRow();
	_lastCursorRow[_currentState] = _babygrid->getSelectedRow();

	if (_hTabCtrl->currentIndex() != index)
	{
		const QSignalBlocker blocker(_hTabCtrl);
		_hTabCtrl->setCurrentIndex(index);
	}
	switch (index)
	{
		case 0:
			_currentState = STATE_MENU;
			break;

		case 1:
			_currentState = STATE_MACRO;
			break;

		case 2:
			_currentState = STATE_USER;
			break;

		case 3:
			_currentState = STATE_PLUGIN;
			break;

		case 4:
			_currentState = STATE_SCINTILLA;
			break;
	}
	fillOutBabyGrid();
}

bool ShortcutMapper::keyConflictsOfSelection(const KeyCombo& combo)
{
	// upstream NPPM_INTERNAL_FINDKEYCONFLICTS (sent by the Shortcut dialog to the Shortcut Mapper)
	wstring conflictInfo;

	// In case of using filter will make the filtered items change index, so here we get its real index
	const int selected = _babygrid->getSelectedRow();
	if (selected < 1 || static_cast<size_t>(selected) > _shortcutIndex.size())
		return false;
	size_t realIndexOfSelectedItem = _shortcutIndex[selected - 1];

	const bool isConflict = findKeyConflicts(&conflictInfo, combo, realIndexOfSelectedItem);
	if (isConflict)
		setItemText(IDC_BABYGRID_INFO, conflictInfo);
	else
		setItemText(IDC_BABYGRID_INFO, _conflictInfoEditing);

	return isConflict;
}

void ShortcutMapper::onRowChanged(int row)
{
	// upstream BGN_ROWCHANGED
	if (_babygrid->getNumberRows() < 1 || row < 1 || static_cast<size_t>(row) > _shortcutIndex.size())
		return;

	NppParameters& nppParam = NppParameters::getInstance();
	const size_t currentIndex = row - 1;

	// In case of using filter will make the filtered items change index, so here we get its real index
	size_t realIndexOfSelectedItem = _shortcutIndex[currentIndex];

	wstring conflictInfo;

	switch (_currentState)
	{
		case STATE_MENU:
		{
			vector<CommandShortcut> & vShortcuts = nppParam.getUserShortcuts();
			findKeyConflicts(&conflictInfo, vShortcuts[realIndexOfSelectedItem].getKeyCombo(), realIndexOfSelectedItem);
		}
		break;

		case STATE_MACRO:
		{
			vector<MacroShortcut> & vShortcuts = nppParam.getMacroList();
			findKeyConflicts(&conflictInfo, vShortcuts[realIndexOfSelectedItem].getKeyCombo(), realIndexOfSelectedItem);
		}
		break;

		case STATE_USER:
		{
			vector<UserCommand> & vShortcuts = nppParam.getUserCommandList();
			findKeyConflicts(&conflictInfo, vShortcuts[realIndexOfSelectedItem].getKeyCombo(), realIndexOfSelectedItem);
		}
		break;

		case STATE_PLUGIN:
		{
			vector<PluginCmdShortcut> & vShortcuts = nppParam.getPluginCommandList();
			findKeyConflicts(&conflictInfo, vShortcuts[realIndexOfSelectedItem].getKeyCombo(), realIndexOfSelectedItem);
		}
		break;

		case STATE_SCINTILLA:
		{
			vector<ScintillaKeyMap> & vShortcuts = nppParam.getScintillaKeyList();
			size_t sciCombos = vShortcuts[realIndexOfSelectedItem].getSize();
			for (size_t sciIndex = 0; sciIndex < sciCombos; ++sciIndex)
				findKeyConflicts(&conflictInfo, vShortcuts[realIndexOfSelectedItem].getKeyComboByIndex(sciIndex), realIndexOfSelectedItem);
		}
		break;
	}

	if (conflictInfo.empty())
		setItemText(IDC_BABYGRID_INFO, _conflictInfoOk);
	else
		setItemText(IDC_BABYGRID_INFO, conflictInfo);
}

void ShortcutMapper::showRightClickMenu(const QPoint& p)
{
	// upstream BGN_CELLRCLICKED
	if (!_rightClickMenu)
	{
		_rightClickMenu = new QMenu(this);
		const struct { int _id; wstring _text; } items[] = {
			{ IDM_BABYGRID_MODIFY, mapperLangStr("ModifyContextMenu", L"Modify") },
			{ IDM_BABYGRID_DELETE, mapperLangStr("DeleteContextMenu", L"Delete") },
			{ IDM_BABYGRID_CLEAR, mapperLangStr("ClearContextMenu", L"Clear") },
		};
		for (const auto& item : items)
		{
			QAction* action = _rightClickMenu->addAction(QString::fromStdWString(item._text));
			action->setData(item._id);
			const int id = item._id;
			connect(action, &QAction::triggered, this, [this, id]() { onCommand(id, RcNotifyClicked); });
		}
	}

	auto enableMenuItem = [this](int id, bool enable) {
		for (QAction* action : _rightClickMenu->actions())
			if (action->data().toInt() == id)
				action->setEnabled(enable);
	};

	if (_babygrid->getNumberRows() < 1)
	{
		enableMenuItem(IDM_BABYGRID_MODIFY, false);
		enableMenuItem(IDM_BABYGRID_DELETE, false);
		enableMenuItem(IDM_BABYGRID_CLEAR, false);
	}
	else
	{
		enableMenuItem(IDM_BABYGRID_MODIFY, true);
		enableMenuItem(IDM_BABYGRID_DELETE, true);
		if (_currentState == STATE_SCINTILLA)
			enableMenuItem(IDM_BABYGRID_CLEAR, false);
		else
			enableMenuItem(IDM_BABYGRID_CLEAR, true);

		switch(_currentState)
		{
			case STATE_MACRO:
			case STATE_USER:
			{
				enableMenuItem(IDM_BABYGRID_DELETE, true);
			}
			break;

			case STATE_MENU:
			case STATE_PLUGIN:
			case STATE_SCINTILLA:
			{
				enableMenuItem(IDM_BABYGRID_DELETE, false);
			}
			break;
		}
	}

	_rightClickMenu->popup(p);
}

void ShortcutMapper::removeEmptyDynamicMenu(NppMenu* hMenu, int shortcutMapperCmd, int posBase)
{
	// upstream: ::RemoveMenu(hMenu, IDM_SETTING_SHORTCUT_MAPPER_*, MF_BYCOMMAND), then the two separators
	for (int pos = 0; pos < hMenu->itemCount(); ++pos)
	{
		if (hMenu->itemId(pos) == shortcutMapperCmd)
		{
			hMenu->removeItem(pos);
			break;
		}
	}
	//remove separator
	hMenu->removeItem(posBase - 1);
	hMenu->removeItem(posBase - 1);
}

void ShortcutMapper::onCommand(int id, int notification)
{
	if (id == IDC_BABYGRID_FILTER)
	{
		if (notification == RcNotifyEditChange)
		{
			fillOutBabyGrid();
		}
		return;
	}
	if (notification != RcNotifyClicked)
		return;

	auto isConflictOfSelection = [this](const KeyCombo& kc) { return keyConflictsOfSelection(kc); };

	switch (id)
	{
		case IDCANCEL :
		{
			done(QDialog::Rejected); // ::EndDialog(_hSelf, -1)
			return;
		}

		case IDOK :
		{
			done(QDialog::Accepted); // ::EndDialog(_hSelf, 0)
			return;
		}

		case IDM_BABYGRID_CLEAR :
		{
			if (_babygrid->getNumberRows() < 1)
				return;

			NppParameters& nppParam = NppParameters::getInstance();
			int row = _babygrid->getSelectedRow();
			if (row < 1)
				return;
			size_t shortcutIndex = _shortcutIndex[row-1];
			bool isModified = false;

			switch(_currentState)
			{
				case STATE_MENU:
				{
					//Get CommandShortcut corresponding to row
					vector<CommandShortcut> & shortcuts = nppParam.getUserShortcuts();
					CommandShortcut csc = shortcuts[shortcutIndex];
					csc.clear();
					shortcuts[shortcutIndex] = csc;
					//shortcut was altered
					nppParam.addUserModifiedIndex(shortcutIndex);

					//save the current view
					_lastHomeRow[_currentState] = _babygrid->getHomeRow();
					_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
					fillOutBabyGrid();

					isModified = true;

					//Notify current Accelerator class to update everything
					if (_host._updateShortcuts)
						_host._updateShortcuts();
					nppParam.setShortcutDirty();
				}
				break;

				case STATE_MACRO:
				{
					//Get MacroShortcut corresponding to row
					vector<MacroShortcut> & shortcuts = nppParam.getMacroList();
					MacroShortcut msc = shortcuts[shortcutIndex];
					msc.clear();
					shortcuts[shortcutIndex] = msc;

					//save the current view
					_lastHomeRow[_currentState] = _babygrid->getHomeRow();
					_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
					fillOutBabyGrid();

					isModified = true;

					//Notify current Accelerator class to update everything
					if (_host._updateShortcuts)
						_host._updateShortcuts();
					nppParam.setShortcutDirty();
				}
				break;

				case STATE_USER:
				{
					//Get UserCommand corresponding to row
					vector<UserCommand> & shortcuts = nppParam.getUserCommandList();
					UserCommand ucmd = shortcuts[shortcutIndex];
					ucmd.clear();

					//shortcut was altered
					shortcuts[shortcutIndex] = ucmd;

					//save the current view
					_lastHomeRow[_currentState] = _babygrid->getHomeRow();
					_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
					fillOutBabyGrid();

					isModified = true;

					//Notify current Accelerator class to update everything
					if (_host._updateShortcuts)
						_host._updateShortcuts();
					nppParam.setShortcutDirty();
				}
				break;

				case STATE_PLUGIN:
				{
					//Get PluginCmdShortcut corresponding to row
					vector<PluginCmdShortcut> & shortcuts = nppParam.getPluginCommandList();
					PluginCmdShortcut pcsc = shortcuts[shortcutIndex];
					pcsc.clear();
					//shortcut was altered
					nppParam.addPluginModifiedIndex(shortcutIndex);
					shortcuts[shortcutIndex] = pcsc;

					//save the current view
					_lastHomeRow[_currentState] = _babygrid->getHomeRow();
					_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
					fillOutBabyGrid();

					isModified = true;

					//Notify current Accelerator class to update everything
					if (_host._updateShortcuts)
						_host._updateShortcuts();
					// upstream: NPPM_INTERNAL_PLUGINSHORTCUTMODIFIED with an empty ShortcutKey
					if (_host._pluginShortcutModified)
						_host._pluginShortcutModified(static_cast<int>(pcsc.getID()), KeyCombo{});
					nppParam.setShortcutDirty();
				}
				break;

				case STATE_SCINTILLA:
				{
					// Do nothing
				}
				break;
			}

			if (!isModified)
				onRowChanged(row);

			return;
		}

		case IDM_BABYGRID_MODIFY :
		{
			if (_babygrid->getNumberRows() < 1)
				return;

			NppParameters& nppParam = NppParameters::getInstance();
			int row = _babygrid->getSelectedRow();
			if (row < 1)
				return;
			size_t shortcutIndex = _shortcutIndex[row-1];
			bool isModified = false;

			switch(_currentState)
			{
				case STATE_MENU:
				{
					//Get CommandShortcut corresponding to row
					vector<CommandShortcut> & shortcuts = nppParam.getUserShortcuts();
					CommandShortcut csc = shortcuts[shortcutIndex], prevcsc = shortcuts[shortcutIndex];
					ShortcutDlg dlg(csc, isConflictOfSelection, this);
					if (dlg.doDialog() != -1 && prevcsc != csc)
					{
						//shortcut was altered
						nppParam.addUserModifiedIndex(shortcutIndex);
						shortcuts[shortcutIndex] = csc;

						//save the current view
						_lastHomeRow[_currentState] = _babygrid->getHomeRow();
						_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
						fillOutBabyGrid();

						isModified = true;

						//Notify current Accelerator class to update everything
						if (_host._updateShortcuts)
							_host._updateShortcuts();
						nppParam.setShortcutDirty();
					}
				}
				break;

				case STATE_MACRO:
				{
					//Get MacroShortcut corresponding to row
					vector<MacroShortcut> & shortcuts = nppParam.getMacroList();
					MacroShortcut msc = shortcuts[shortcutIndex], prevmsc = shortcuts[shortcutIndex];
					ShortcutDlg dlg(msc, isConflictOfSelection, this);
					if (dlg.doDialog() != -1 && prevmsc != msc)
					{
						//shortcut was altered
						shortcuts[shortcutIndex] = msc;
						// NPLX: the menu text follows the name (upstream updateShortcuts -> ModifyMenu)
						nppParam.getMacroMenuItems().getItemFromIndex(shortcutIndex)._itemName = string2wstring(msc.getName(), CP_UTF8);

						//save the current view
						_lastHomeRow[_currentState] = _babygrid->getHomeRow();
						_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
						fillOutBabyGrid();

						isModified = true;

						//Notify current Accelerator class to update everything
						if (_host._updateShortcuts)
							_host._updateShortcuts();
						nppParam.setShortcutDirty();
					}
				}
				break;

				case STATE_USER:
				{
					//Get UserCommand corresponding to row
					vector<UserCommand> & shortcuts = nppParam.getUserCommandList();
					UserCommand ucmd = shortcuts[shortcutIndex];
					UserCommand prevucmd = ucmd;
					ShortcutDlg dlg(ucmd, isConflictOfSelection, this);
					if (dlg.doDialog() != -1 && prevucmd != ucmd)
					{
						//shortcut was altered
						shortcuts[shortcutIndex] = ucmd;
						// NPLX: the menu text follows the name (upstream updateShortcuts -> ModifyMenu)
						nppParam.getRunMenuItems().getItemFromIndex(shortcutIndex)._itemName = string2wstring(ucmd.getName(), CP_UTF8);

						//save the current view
						_lastHomeRow[_currentState] = _babygrid->getHomeRow();
						_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
						fillOutBabyGrid();

						isModified = true;

						//Notify current Accelerator class to update everything
						if (_host._updateShortcuts)
							_host._updateShortcuts();
						nppParam.setShortcutDirty();
					}
				}
				break;

				case STATE_PLUGIN:
				{
					//Get PluginCmdShortcut corresponding to row
					vector<PluginCmdShortcut> & shortcuts = nppParam.getPluginCommandList();
					PluginCmdShortcut pcsc = shortcuts[shortcutIndex];
					PluginCmdShortcut prevpcsc = pcsc;
					ShortcutDlg dlg(pcsc, isConflictOfSelection, this);
					if (dlg.doDialog() != -1 && prevpcsc != pcsc)
					{
						//shortcut was altered
						nppParam.addPluginModifiedIndex(shortcutIndex);
						shortcuts[shortcutIndex] = pcsc;

						//save the current view
						_lastHomeRow[_currentState] = _babygrid->getHomeRow();
						_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
						fillOutBabyGrid();

						isModified = true;

						//Notify current Accelerator class to update everything
						if (_host._updateShortcuts)
							_host._updateShortcuts();
						if (_host._pluginShortcutModified)
							_host._pluginShortcutModified(static_cast<int>(pcsc.getID()), pcsc.getKeyCombo());
						nppParam.setShortcutDirty();
					}
				}
				break;

				case STATE_SCINTILLA:
				{
					//Get ScintillaKeyMap corresponding to row
					vector<ScintillaKeyMap> & shortcuts = nppParam.getScintillaKeyList();
					ScintillaKeyMap skm = shortcuts[shortcutIndex], prevskm = shortcuts[shortcutIndex];
					ScintillaKeyMapDlg dlg(skm, isConflictOfSelection, this);
					if (dlg.doDialog() != -1 && prevskm != skm)
					{
						//shortcut was altered
						nppParam.addScintillaModifiedIndex((int)shortcutIndex);
						shortcuts[shortcutIndex] = skm;

						//save the current view
						_lastHomeRow[_currentState] = _babygrid->getHomeRow();
						_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
						fillOutBabyGrid();
						_babygrid->updateView();

						isModified = true;

						//Notify current Accelerator class to update key
						if (_host._updateScintillaKeys)
							_host._updateScintillaKeys();
						nppParam.setShortcutDirty();
					}
				}
				break;
			}

			if (!isModified)
				onRowChanged(row);

			return;
		}

		case IDM_BABYGRID_DELETE :
		{
			if (_babygrid->getNumberRows() < 1)
				return;

			NppParameters& nppParam = NppParameters::getInstance();
			NativeLangSpeaker* speaker = langSpeaker();
			const int res = speaker
				? speaker->messageBox("SCMapperDoDeleteOrNot", L"Are you sure you want to delete this shortcut?", L"Are you sure?", MB_OKCANCEL)
				: IDOK;

			if (res == IDOK)
			{
				const int row = _babygrid->getSelectedRow();
				if (row < 1)
					return;
				size_t shortcutIndex = _shortcutIndex[row-1];

				switch(_currentState)
				{
					case STATE_MENU:
					case STATE_PLUGIN:
					case STATE_SCINTILLA:
					{
						return;
					}

					case STATE_MACRO:
					{
						vector<MacroShortcut> & theMacros = nppParam.getMacroList();
						theMacros.erase(theMacros.begin() + shortcutIndex);

						//save the current view
						_lastHomeRow[_currentState] = _babygrid->getHomeRow();
						_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
						const size_t numberRows = _babygrid->getNumberRows();
						if (_lastHomeRow[_currentState] == numberRows)
							--_lastHomeRow[_currentState];
						if (_lastCursorRow[_currentState] == numberRows)
							--_lastCursorRow[_currentState];

						fillOutBabyGrid();

						// clear all menu
						DynamicMenu& macroMenu = nppParam.getMacroMenuItems();
						macroMenu.clearMenu();

						// Erase the menu item
						macroMenu.erase(shortcutIndex);

						size_t nbElem = theMacros.size();
						for (size_t i = shortcutIndex; i < nbElem; ++i)	//lower the IDs of the remaining items so there are no gaps
						{
							MacroShortcut ms = theMacros[i];
							ms.setID(ms.getID() - 1);	//shift all IDs
							theMacros[i] = ms;

							// Adjust menu items
							MenuItemUnit& miu = macroMenu.getItemFromIndex(i);
							miu._cmdID -= 1;	//shift all IDs
						}
						// create from scratch according the new menu items structure
						macroMenu.createMenu();

						NppMenu* hMenu = _host._macroMenu ? _host._macroMenu() : nullptr;
						if (!hMenu) return;

						int32_t posBase = macroMenu.getPosBase();
						if (nbElem == 0)
							removeEmptyDynamicMenu(hMenu, IDM_SETTING_SHORTCUT_MAPPER_MACRO, posBase);
					}
					break;

					case STATE_USER:
					{
						vector<UserCommand> & theUserCmds = nppParam.getUserCommandList();
						theUserCmds.erase(theUserCmds.begin() + shortcutIndex);

						//save the current view
						_lastHomeRow[_currentState] = _babygrid->getHomeRow();
						_lastCursorRow[_currentState] = _babygrid->getSelectedRow();
						const size_t numberRows = _babygrid->getNumberRows();
						if (_lastHomeRow[_currentState] == numberRows)
							--_lastHomeRow[_currentState];
						if (_lastCursorRow[_currentState] == numberRows)
							--_lastCursorRow[_currentState];

						fillOutBabyGrid();

						// clear all menu
						DynamicMenu& runMenu = nppParam.getRunMenuItems();
						runMenu.clearMenu();

						// Erase the menu item
						runMenu.erase(shortcutIndex);

						// preparing to remove from menu
						size_t nbElem = theUserCmds.size();
						for (size_t i = shortcutIndex; i < nbElem; ++i)	//lower the IDs of the remaining items so there are no gaps
						{
							UserCommand uc = theUserCmds[i];
							uc.setID(uc.getID() - 1);	//shift all IDs
							theUserCmds[i] = uc;
							// Adjust menu items
							MenuItemUnit& miu = runMenu.getItemFromIndex(i);
							miu._cmdID -= 1;	//shift all IDs
						}

						// create from scratch according the new menu items structure
						runMenu.createMenu();

						NppMenu* hMenu = _host._runMenu ? _host._runMenu() : nullptr;
						if (!hMenu) return;

						int32_t posBase = runMenu.getPosBase();
						if (nbElem == 0)
							removeEmptyDynamicMenu(hMenu, IDM_SETTING_SHORTCUT_MAPPER_RUN, posBase);
					}
					break;
				}

				// updateShortcuts() will update all menu item - the menu items will be shifted
				if (_host._updateShortcuts)
					_host._updateShortcuts();
				nppParam.setShortcutDirty();
			}
			return;
		}

		case IDC_BABYGRID_FILTER_CLEAR:
		{
			setItemText(IDC_BABYGRID_FILTER, L"");
			fillOutBabyGrid(); // NPLX: setItemText does not notify (upstream EN_CHANGE)
			dlgItem(IDC_BABYGRID_FILTER)->setFocus();
			return;
		}

		default:
			return;
	}
}

bool ShortcutMapper::findKeyConflicts(wstring * const keyConflictLocation,
										const KeyCombo & itemKeyComboToTest, const size_t & itemIndexToTest) const
{
	if (itemKeyComboToTest._key == 0) //no key assignment
		return false;

	bool retIsConflict = false; //returns true when a conflict is found
	NppParameters& nppParam = NppParameters::getInstance();

	// NPLX: upstream repeats this block for each list; one generic loop over the lists of CommandShortcut
	auto appendConflict = [&](size_t gridState, size_t itemIndex, const string& name, const string& keys, bool isAlternateKey) {
		if (!keyConflictLocation->empty())
			*keyConflictLocation += L"\n"; // NPLX: was \r\n
		*keyConflictLocation += _tabNames[gridState].empty() ? getTabString(gridState) : _tabNames[gridState];
		*keyConflictLocation += L"  |  ";
		*keyConflictLocation += std::to_wstring(itemIndex + 1);
		*keyConflictLocation += isAlternateKey ? L"*   " : L"   ";
		*keyConflictLocation += string2wstring(name, CP_UTF8);
		*keyConflictLocation += L"  ( ";
		*keyConflictLocation += string2wstring(keys, CP_UTF8);
		*keyConflictLocation += L" )";
	};

	auto scanCommands = [&](auto& vShortcuts, size_t gridState) -> bool {
		size_t nbItems = vShortcuts.size();
		for (size_t itemIndex = 0; itemIndex < nbItems; ++itemIndex)
		{
			if (!vShortcuts[itemIndex].isEnabled()) //no key assignment
				continue;

			if ((itemIndex == itemIndexToTest) && (gridState == static_cast<size_t>(_currentState))) //don't catch oneself
				continue;

			if (isConflict(vShortcuts[itemIndex].getKeyCombo(), itemKeyComboToTest))
			{
				retIsConflict = true;
				if (keyConflictLocation == nullptr)
					return true;
				appendConflict(gridState, itemIndex, vShortcuts[itemIndex].getName(), vShortcuts[itemIndex].toString(), false);
			}
		}
		return false;
	};

	for (size_t gridState = STATE_MENU; gridState <= STATE_SCINTILLA; ++gridState)
	{
		switch (gridState)
		{
			case STATE_MENU:
				if (scanCommands(nppParam.getUserShortcuts(), gridState))
					return retIsConflict;
				break;

			case STATE_MACRO:
				if (scanCommands(nppParam.getMacroList(), gridState))
					return retIsConflict;
				break;

			case STATE_USER:
				if (scanCommands(nppParam.getUserCommandList(), gridState))
					return retIsConflict;
				break;

			case STATE_PLUGIN:
				if (scanCommands(nppParam.getPluginCommandList(), gridState))
					return retIsConflict;
				break;

			case STATE_SCINTILLA:
			{
				vector<ScintillaKeyMap> & vShortcuts = nppParam.getScintillaKeyList();
				size_t nbItems = vShortcuts.size();
				for (size_t itemIndex = 0; itemIndex < nbItems; ++itemIndex)
				{
					if (!vShortcuts[itemIndex].isEnabled()) //no key assignment
						continue;

					if ((itemIndex == itemIndexToTest) && (gridState == static_cast<size_t>(_currentState))) //don't catch oneself
						continue;

					size_t sciCombos = vShortcuts[itemIndex].getSize();
					for (size_t sciIndex = 0; sciIndex < sciCombos; ++sciIndex)
					{
						if (isConflict(vShortcuts[itemIndex].getKeyComboByIndex(sciIndex), itemKeyComboToTest))
						{
							retIsConflict = true;
							if (keyConflictLocation == nullptr)
								return retIsConflict;
							appendConflict(gridState, itemIndex, vShortcuts[itemIndex].getName(), vShortcuts[itemIndex].toString(sciIndex), sciIndex > 0);
						}
					}
				}
				break;
			}
		}
	}
	return retIsConflict;
}
