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

// NPLX: port of upstream lastRecentFileList.h. The list logic is unchanged; the menu is made of
// QActions inserted in the File menu (before "Exit") or in a "Recent Files" sub-menu.

#pragma once

#include <deque>
#include <functional>
#include <vector>

#include "Parameters.h"

class QAction;
class QMenu;

struct RecentItem {
	int _id = 0;
	std::wstring _name;
	explicit RecentItem(const wchar_t* name) : _name(name) {}
};

typedef std::deque<RecentItem> recentList;

class LastRecentFileList
{
public:
	LastRecentFileList() {
		_userMax = (NppParameters::getInstance()).getNbMaxRecentFile();
	}

	// NPLX: hMenu = the File menu, beforeAction = the entry the list is inserted before ("Exit");
	// triggered(id) is called with the command id of a clicked entry
	void initMenu(QMenu* hMenu, int idBase, QAction* beforeAction, std::function<void(int)> triggered, bool doSubMenu = false);
	void updateMenu();

	void add(const wchar_t* fn);
	void remove(const wchar_t* fn);
	void remove(size_t index);
	void clear();

	int getSize() {
		return _size;
	}

	static int getMaxNbLRF() {
		return NB_MAX_LRF_FILE;
	}

	int getUserMaxNbLRF() const {
		return _userMax;
	}

	std::wstring& getItem(int id);	//use menu id
	std::wstring& getIndex(int index);	//use menu id

	std::wstring getFirstItem() const {
		if (_lrfl.size() == 0)
			return L"";
		return _lrfl.front()._name;
	}

	void setUserMaxNbLRF(int size);

	void saveLRFL();

	void setLock(bool lock) {
		_locked = lock;
	}

	// upstream switchMode: between the File menu and the "Recent Files" sub-menu (updateMenu rebuilds the items)
	void switchMode() { _isSubMenuMode = !_isSubMenuMode; }

	bool isSubMenuMode() const {
		return _isSubMenuMode;
	}

private:
	recentList _lrfl;
	int _userMax = 0;
	int _size = 0;

	// For the menu
	QMenu* _hParentMenu = nullptr;
	QMenu* _hSubMenu = nullptr;
	QAction* _beforeAction = nullptr;
	std::vector<QAction*> _menuActions; // everything this list added to the menu
	std::function<void(int)> _triggered;
	bool _isSubMenuMode = false;
	int _idBase = -1;
	bool _idFreeArray[NB_MAX_LRF_FILE] = {false};
	bool _locked = false;

	int find(const wchar_t* fn);
	int popFirstAvailableID();
	void setAvailable(int id);
};
