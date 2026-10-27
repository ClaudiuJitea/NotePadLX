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

// NPLX: port of upstream WinControls/Grid/ShortcutMapper (Settings > Shortcut Mapper...), built from
// IDD_SHORTCUTMAPPER_DLG. The BabyGrid is BabyGridWrapper (a QTableWidget); what upstream sends to Notepad++
// (NPPM_INTERNAL_GETMENU, the accelerator updates) goes through ShortcutMapperHost.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "RcDialog.h"
#include "ShortcutMapper_rc.h"
#include "shortcut.h"

class BabyGridWrapper;
class NppMenu;
class QMenu;
class QTabBar;

enum GridState {STATE_MENU, STATE_MACRO, STATE_USER, STATE_PLUGIN, STATE_SCINTILLA};

// NPLX: the Notepad++ side of the Shortcut Mapper
struct ShortcutMapperHost
{
	std::function<NppMenu*()> _macroMenu;          // upstream ::GetSubMenu(NPPM_INTERNAL_GETMENU, MENUINDEX_MACRO)
	std::function<NppMenu*()> _runMenu;            // ... MENUINDEX_RUN
	std::function<void()> _updateShortcuts;        // nppParam.getAccelerator()->updateShortcuts()
	std::function<void()> _updateScintillaKeys;    // nppParam.getScintillaAccelerator()->updateKeys()
	std::function<bool(int cmdID)> _isRemovedCommand; // NPLX: a Windows-only command, not listed
	std::function<void(int cmdID, const KeyCombo& key)> _pluginShortcutModified; // NPPM_INTERNAL_PLUGINSHORTCUTMODIFIED
};

class ShortcutMapper : public RcDialog {
	Q_OBJECT

public:
	explicit ShortcutMapper(QWidget* parent = nullptr) : RcDialog(parent) {}
	~ShortcutMapper() override = default;

	void init(ShortcutMapperHost host, GridState initState = STATE_MENU) {
		_host = std::move(host);
		_currentState = initState;
	}

	// upstream doDialog (modal); 0 on Close
	int doDialog();

	bool findKeyConflicts(std::wstring * const keyConflictLocation,
							const KeyCombo & itemKeyCombo, const size_t & itemIndex) const;

	std::wstring getTextFromCombo(int id);
	bool isFilterValid(Shortcut sc);
	bool isFilterValid(PluginCmdShortcut sc);
	bool isFilterValid(ScintillaKeyMap sc);

	// NPLX: tests
	BabyGridWrapper* grid() const { return _babygrid; }
	void switchToTab(int index); // upstream TCN_SELCHANGE
	GridState currentState() const { return _currentState; }

protected:
	void onCommand(int id, int notification) override;
	void resizeEvent(QResizeEvent* event) override; // upstream WM_SIZE
	void resizeDialogElements();

private:
	BabyGridWrapper* _babygrid = nullptr;
	QMenu* _rightClickMenu = nullptr;
	ShortcutMapperHost _host;

	GridState _currentState = STATE_MENU;
	QTabBar* _hTabCtrl = nullptr;

	static constexpr int _nbTab = 5;
	std::wstring _tabNames[_nbTab];
	std::vector<std::wstring> _shortcutFilter;
	std::vector<size_t> _shortcutIndex;

	//save/restore the last view
	std::vector<size_t> _lastHomeRow;
	std::vector<size_t> _lastCursorRow;

	std::wstring _conflictInfoOk;
	std::wstring _conflictInfoEditing;

	void initTabs();
	void initBabyGrid();
	void fillOutBabyGrid();
	std::wstring getTabString(size_t i) const;
	void onRowChanged(int row);          // upstream BGN_ROWCHANGED
	void showRightClickMenu(const QPoint& p); // upstream BGN_CELLRCLICKED
	bool keyConflictsOfSelection(const KeyCombo& combo); // upstream NPPM_INTERNAL_FINDKEYCONFLICTS
	void removeEmptyDynamicMenu(NppMenu* hMenu, int shortcutMapperCmd, int posBase);

	static bool isConflict(const KeyCombo& lhs, const KeyCombo& rhs)
	{
		return ( (lhs._isCtrl  == rhs._isCtrl ) &&
				 (lhs._isAlt   == rhs._isAlt  ) &&
				 (lhs._isShift == rhs._isShift) &&
				 (lhs._key	   == rhs._key	  ) );
	}
};
