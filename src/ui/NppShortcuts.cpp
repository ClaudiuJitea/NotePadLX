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

// NPLX: the shortcut side of upstream Notepad++: Accelerator::updateShortcuts / updateFullMenu (the accelerator table
// becomes the QAction shortcuts), ScintillaAccelerator::updateKeys, CommandShortcut::setCategoryFromMenu, and the
// Settings > Shortcut Mapper commands.

#include "Notepad_plus.h"

#include <map>
#include <set>

#include <QAction>
#include <QMenu>
#include <QMenuBar>

#include "KeyMap.h"
#include "Parameters.h"
#include "QtNppMenu.h"
#include "ScintillaEditView.h"
#include "ShortcutMapper.h"
#include "localization.h"
#include "menuCmdID.h"
#include "resource.h"
#include "shortcut.h"

using namespace std;

namespace
{
	// the text of a menu item without the "\t<shortcut>" part
	QString menuLabel(const QAction* action)
	{
		const QString text = action->text();
		const qsizetype tab = text.indexOf(QLatin1Char('\t'));
		return tab < 0 ? text : text.left(tab);
	}
}

void Notepad_plus::updateShortcuts()
{
	// upstream Accelerator::updateShortcuts: the accelerator table of the menu commands, macros, Run commands and
	// plugin commands; NPLX: the shortcuts of the menu QActions, and hidden QActions for the commands without a
	// menu item (e.g. Ctrl+Tab = IDC_NEXT_DOC). Invalid shortcuts are kept, as upstream: "Allows user to 'hack'".
	NppParameters& nppParam = NppParameters::getInstance();

	std::multimap<int, QKeySequence> shortcuts;
	for (const CommandShortcut& csc : nppParam.getUserShortcuts())
	{
		if (csc.isEnabled())
			shortcuts.emplace(static_cast<int>(csc.getID()), nplx::keyComboToKeySequence(csc.getKeyCombo()));
	}

	auto keysOf = [&shortcuts](int id) {
		QList<QKeySequence> keys;
		for (auto [it, end] = shortcuts.equal_range(id); it != end; ++it)
			keys.push_back(it->second);
		return keys;
	};

	// the menu items of Scintilla commands show the Scintilla key instead (updateScintillaKeys)
	std::set<int> scintillaMenuIds;
	for (const ScintillaKeyMap& skm : nppParam.getScintillaKeyList())
		if (skm.getMenuCmdID() != 0)
			scintillaMenuIds.insert(skm.getMenuCmdID());

	// updateFullMenu: the menu items show their shortcuts
	std::set<int> menuIds;
	for (const auto& [id, action] : _actions)
	{
		menuIds.insert(id);
		if (scintillaMenuIds.count(id) && shortcuts.count(id) == 0)
			continue;
		action->setShortcuts(keysOf(id));
	}

	// the commands without a menu item
	for (auto& [id, hidden] : _hiddenShortcutActions)
		hidden->setShortcuts(QList<QKeySequence>());
	for (const auto& [id, key] : shortcuts)
	{
		if (menuIds.count(id) || !isCommandPorted(id))
			continue;
		QAction*& hidden = _hiddenShortcutActions[id];
		if (!hidden)
		{
			hidden = new QAction(this);
			hidden->setData(id);
			hidden->setShortcutContext(Qt::WindowShortcut);
			addAction(hidden);
			const int cmdID = id;
			connect(hidden, &QAction::triggered, this, [this, cmdID]() { command(cmdID); });
		}
		hidden->setShortcuts(keysOf(id));
	}

	// the macros and the Run commands (their names too: upstream ModifyMenu with toMenuItemString)
	const vector<MacroShortcut>& macros = nppParam.getMacroList();
	const vector<UserCommand>& userCommands = nppParam.getUserCommandList();
	for (QMenu* menu : { menuOfCommand(IDM_MACRO_STARTRECORDINGMACRO), menuOfCommand(IDM_EXECUTE) })
	{
		if (!menu)
			continue;
		for (QAction* action : menu->findChildren<QAction*>())
		{
			bool ok = false;
			const int id = action->data().toInt(&ok);
			if (!ok)
				continue;
			if ((id >= ID_MACRO) && (id < ID_MACRO_LIMIT) && static_cast<size_t>(id - ID_MACRO) < macros.size())
				action->setText(QString::fromStdString(macros[id - ID_MACRO].getMenuName()));
			else if ((id >= ID_USER_CMD) && (id < ID_USER_CMD_LIMIT) && static_cast<size_t>(id - ID_USER_CMD) < userCommands.size())
				action->setText(QString::fromStdString(userCommands[id - ID_USER_CMD].getMenuName()));
		}
	}
	updateDynamicMenuShortcuts();
	// NPLX_TODO(Phase 9): the plugin commands
}

void Notepad_plus::updateScintillaKeys()
{
	// upstream ScintillaAccelerator::updateKeys
	NppParameters& nppParam = NppParameters::getInstance();
	const vector<ScintillaKeyMap> & map = nppParam.getScintillaKeyList();
	size_t mapSize = map.size();
	size_t index;
	for (ScintillaEditView* view : { _mainEditView, _subEditView })
	{
		view->execute(SCI_CLEARALLCMDKEYS, 0, 0);
		for (int32_t j = static_cast<int32_t>(mapSize) - 1; j >= 0; j--) //reverse order, top of the list has highest priority
		{
			const ScintillaKeyMap& skm = map[j];
			if (skm.isEnabled())
			{		//no validating, scintilla accepts more keys
				size_t size = skm.getSize();
				for (index = 0; index < size; ++index)
					view->execute(SCI_ASSIGNCMDKEY, skm.toKeyDef(index), skm.getScintillaKeyID());
			}
		}
	}

	// upstream updateMenuItemByID: the menu item of a Scintilla command shows its key after a tab. NPLX: shown
	// only, not a QAction shortcut, so that the key still goes to the focused widget (an edit box of a panel)
	for (const ScintillaKeyMap& skm : map)
	{
		if (skm.getMenuCmdID() == 0)
			continue;
		for (auto [it, end] = _actions.equal_range(skm.getMenuCmdID()); it != end; ++it)
		{
			QAction* action = it->second;
			QString menuItem = menuLabel(action);
			if (skm.isEnabled())
			{
				menuItem += QLatin1Char('\t');
				menuItem += QString::fromStdString(skm.toString());
			}
			action->setShortcuts(QList<QKeySequence>());
			action->setText(menuItem);
		}
	}
}

void Notepad_plus::updateCommandShortcuts()
{
	// upstream Notepad_plus::updateCommandShortcuts:
	// * Menu name
	//   1. From xml file Menu/Main/Commands section
	//   2. From menu resource in Notepad_plus.rc
	//   3. From winKeyDefs[] table in Parameter.cpp
	// * Shortcut name
	//   1. From xml ShortCutMapper/MainCommandNames section
	//   2. From xml file Menu/Main/Commands section
	//   3. From the winKeyDefs[] table in Parameter.cpp
	//   4. From the menu resource in Notepad_plus.rc
	NppParameters& nppParam = NppParameters::getInstance();

	for (CommandShortcut& csc : nppParam.getUserShortcuts())
	{
		unsigned long id = csc.getID();
		wstring localizedMenuName = _nativeLangSpeaker.getNativeLangMenuString(id);
		wstring menuName = localizedMenuName;
		wstring shortcutName = _nativeLangSpeaker.getShortcutNameString(id);

		if (menuName.length() == 0)
		{
			// ::GetMenuString(_mainMenuHandle, csc.getID(), ...)
			if (const QAction* action = actionFor(static_cast<int>(id)))
				menuName = purgeMenuItemString(menuLabel(action).toStdWString().c_str(), true);
			else
				menuName = csc.getShortcutName();
		}

		if (shortcutName.length() == 0)
		{
			if (localizedMenuName.length() > 0)
				shortcutName = localizedMenuName;
			else if (csc.getShortcutName()[0])
				shortcutName = csc.getShortcutName();
			else
				shortcutName = menuName;
		}

		csc.setName(wstring2string(menuName, CP_UTF8).c_str(), wstring2string(shortcutName, CP_UTF8).c_str());
	}

	setShortcutCategories(); // csc.setCategoryFromMenu(_mainMenuHandle)
}

void Notepad_plus::setShortcutCategories()
{
	// upstream CommandShortcut::setCategoryFromMenu, for each menu command (Notepad_plus::init); the category is the
	// name of the top level menu, as NativeLangSpeaker::getMainMenuEntryName reads it from the menu bar
	const QList<QAction*> topMenus = menuBar()->actions();
	auto entryName = [&topMenus](const char* menuId, const wchar_t* defaultDest) -> wstring {
		const MenuPosition& menuPos = MenuPosition::getMenuPosition(menuId);
		if (menuPos._x != -1 && menuPos._y == -1 && menuPos._z == -1 && menuPos._x < topMenus.size())
		{
			QString str = topMenus.at(menuPos._x)->text();
			str.remove(QLatin1Char('&'));
			return str.toStdWString();
		}
		if (strcmp(menuId, "about") == 0)
		{
			NativeLangSpeaker* pNativeSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
			return pNativeSpeaker ? pNativeSpeaker->getShortcutMapperLangStr("AboutCategory", defaultDest) : wstring(defaultDest);
		}
		return defaultDest;
	};

	for (CommandShortcut& csc : NppParameters::getInstance().getUserShortcuts())
	{
		const unsigned long id = csc.getID();
		wstring category;

		if (id >= IDM_WINDOW_WINDOWS && id <= IDM_WINDOW_SORT_FS_DSC)
			category = entryName("Window", L"Window");
		else if ( id >= IDM_VIEW_GOTO_ANOTHER_VIEW && id <= IDM_VIEW_GOTO_END)
			category = entryName("view", L"View");
		else if (id == IDM_EDIT_LTR || id == IDM_EDIT_RTL)
			category = entryName("view", L"View");
		else if (id == IDC_PREV_DOC || id == IDC_NEXT_DOC)
			category = entryName("view", L"View");
		else if (id == IDM_FORMAT_TODOS || id == IDM_FORMAT_TOUNIX || id == IDM_FORMAT_TOMAC)
			category = entryName("edit", L"Edit");
		else if (id == IDM_EDIT_AUTOCOMPLETE || id == IDM_EDIT_AUTOCOMPLETE_CURRENTFILE || id == IDM_EDIT_FUNCCALLTIP ||
			id == IDM_EDIT_AUTOCOMPLETE_PATH || id == IDM_EDIT_FUNCCALLTIP_PREVIOUS || id == IDM_EDIT_FUNCCALLTIP_NEXT)
			category = entryName("edit", L"Edit");
		else if (id == IDM_LANGSTYLE_CONFIG_DLG)
			category = entryName("settings", L"Settings");
		else if (id == IDM_MACRO_STARTRECORDINGMACRO  ||id == IDM_MACRO_STOPRECORDINGMACRO  || id == IDM_MACRO_RUNMULTIMACRODLG ||
			id == IDM_MACRO_PLAYBACKRECORDEDMACRO  ||id == IDM_MACRO_SAVECURRENTMACRO || id == IDC_EDIT_TOGGLEMACRORECORDING)
			category = entryName("macro", L"Macro");


		else if ( id < IDM_EDIT)
			category = entryName("file", L"File");
		else if ( id < IDM_SEARCH)
			category = entryName("edit",L"Edit");
		else if ( id < IDM_VIEW)
			category = entryName("search", L"Search");
		else if ( id < IDM_FORMAT)
			category = entryName("view", L"View");
		else if ( id < IDM_LANG)
			category = entryName("encoding", L"Encoding");
		else if ( id < IDM_ABOUT)
			category = entryName("language", L"Language");
		else if ( id < IDM_SETTING)
			category = entryName("about", L"About");
		else if ( id < IDM_TOOL)
			category = entryName("settings", L"Settings");
		else if ( id < IDM_EXECUTE)
			category = entryName("tools", L"Tools");
		else // id >= IDM_EXECUTE
			category = entryName("run", L"Run");
		csc.setCategory(category);
	}
}
