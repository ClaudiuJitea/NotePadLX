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

#include "LastRecentFileList.h"

#include <QAction>
#include <QMenu>

#include "localization.h"
#include "menuCmdID.h"

using std::wstring;

void LastRecentFileList::initMenu(QMenu* hMenu, int idBase, QAction* beforeAction, std::function<void(int)> triggered, bool doSubMenu)
{
	_hParentMenu = hMenu;
	_beforeAction = beforeAction;
	_triggered = std::move(triggered);
	_isSubMenuMode = doSubMenu;
	_idBase = idBase;

	for (int i = 0; i < NB_MAX_LRF_FILE; ++i)
		_idFreeArray[i] = true;
}

void LastRecentFileList::updateMenu()
{
	// NPLX: rebuilt from scratch each time (upstream inserts/removes Win32 menu items by position)
	for (QAction* a : _menuActions)
	{
		if (_hParentMenu)
			_hParentMenu->removeAction(a);
		a->deleteLater();
	}
	_menuActions.clear();
	if (_hSubMenu)
	{
		_hSubMenu->deleteLater();
		_hSubMenu = nullptr;
	}

	if (!_hParentMenu || _size == 0)
		return;

	NppParameters& nppParam = NppParameters::getInstance();
	NativeLangSpeaker* pNativeLangSpeaker = nppParam.getNativeLangSpeaker();

	wstring recentFileList = pNativeLangSpeaker->getSubMenuEntryName("file-recentFiles");
	wstring openRecentClosedFile = pNativeLangSpeaker->getNativeLangMenuString(IDM_FILE_RESTORELASTCLOSEDFILE);
	wstring openAllFiles = pNativeLangSpeaker->getNativeLangMenuString(IDM_OPEN_ALL_RECENT_FILE);
	wstring cleanFileList = pNativeLangSpeaker->getNativeLangMenuString(IDM_CLEAN_RECENT_FILE_LIST);

	if (recentFileList == L"")
		recentFileList = L"&Recent Files";
	if (openRecentClosedFile == L"")
		openRecentClosedFile = L"Restore Recent Closed File";
	if (openAllFiles == L"")
		openAllFiles = L"Open All Recent Files";
	if (cleanFileList == L"")
		cleanFileList = L"Empty Recent Files List";

	// Same layout as upstream: the files, a separator, the 3 commands, a separator
	auto makeAction = [this](const wstring& text, int id) {
		QAction* a = new QAction(QString::fromStdWString(text), _hParentMenu);
		a->setData(id);
		QObject::connect(a, &QAction::triggered, [this, id]() { if (_triggered) _triggered(id); });
		return a;
	};
	auto makeSeparator = [this]() {
		QAction* a = new QAction(_hParentMenu);
		a->setSeparator(true);
		return a;
	};

	std::vector<QAction*> items;
	for (int j = 0; j < _size; ++j)
	{
		wstring strBuffer(BuildMenuFileName(nppParam.getRecentFileCustomLength(), j, _lrfl.at(j)._name));
		items.push_back(makeAction(strBuffer, _lrfl.at(j)._id));
	}
	items.push_back(makeSeparator());
	items.push_back(makeAction(openRecentClosedFile, IDM_FILE_RESTORELASTCLOSEDFILE));
	items.push_back(makeAction(openAllFiles, IDM_OPEN_ALL_RECENT_FILE));
	items.push_back(makeAction(cleanFileList, IDM_CLEAN_RECENT_FILE_LIST));

	if (isSubMenuMode())
	{
		_hSubMenu = new QMenu(QString::fromStdWString(recentFileList), _hParentMenu);
		for (QAction* a : items)
			_hSubMenu->addAction(a);
		QAction* subMenuAction = _hSubMenu->menuAction();
		_hParentMenu->insertAction(_beforeAction, subMenuAction);
		QAction* sep = makeSeparator();
		_hParentMenu->insertAction(_beforeAction, sep);
		_menuActions = { subMenuAction, sep };
		for (QAction* a : items)
			a->setParent(_hSubMenu);
	}
	else
	{
		items.push_back(makeSeparator());
		for (QAction* a : items)
			_hParentMenu->insertAction(_beforeAction, a);
		_menuActions = items;
	}
}

void LastRecentFileList::add(const wchar_t* fn)
{
	if (_userMax == 0 || _locked)
		return;

	RecentItem itemToAdd(fn);

	int index = find(fn);
	if (index != -1)
	{
		//already in list, bump upwards
		remove(index);
	}

	if (_size == _userMax)
	{
		itemToAdd._id = _lrfl.back()._id;
		_lrfl.pop_back();	//remove oldest
	}
	else
	{
		itemToAdd._id = popFirstAvailableID();
		++_size;
	}
	_lrfl.push_front(itemToAdd);
	updateMenu();
}

void LastRecentFileList::remove(const wchar_t* fn)
{
	int index = find(fn);
	if (index != -1)
		remove(index);
}

void LastRecentFileList::remove(size_t index)
{
	if (_size == 0 || _locked)
		return;
	if (index < _lrfl.size())
	{
		setAvailable(_lrfl.at(index)._id);
		_lrfl.erase(_lrfl.begin() + index);
		--_size;
		updateMenu();
	}
}

void LastRecentFileList::clear()
{
	if (_size == 0)
		return;

	for (int i = (_size - 1); i >= 0; i--)
	{
		setAvailable(_lrfl.at(i)._id);
		_lrfl.erase(_lrfl.begin() + i);
	}
	_size = 0;
	updateMenu();
}

wstring& LastRecentFileList::getItem(int id)
{
	int i = 0;
	for (; i < _size; ++i)
	{
		if (_lrfl.at(i)._id == id)
			break;
	}

	if (i == _size)
		i = 0;

	return _lrfl.at(i)._name;	//if not found, return first
}

wstring& LastRecentFileList::getIndex(int index)
{
	return _lrfl.at(index)._name;	//if not found, return first
}

void LastRecentFileList::setUserMaxNbLRF(int size)
{
	_userMax = size;
	if (_size > _userMax)
	{	//start popping items
		int toPop = _size - _userMax;
		while (toPop > 0)
		{
			setAvailable(_lrfl.back()._id);
			_lrfl.pop_back();
			toPop--;
			_size--;
		}

		updateMenu();
		_size = _userMax;
	}
}

void LastRecentFileList::saveLRFL()
{
	NppParameters& nppParams = NppParameters::getInstance();
	if (nppParams.writeRecentFileHistorySettings(_userMax))
	{
		for (int i = _size - 1; i >= 0; i--)	//reverse order: so loading goes in correct order
		{
			nppParams.writeHistory(_lrfl.at(i)._name.c_str());
		}
	}
}

int LastRecentFileList::find(const wchar_t* fn)
{
	for (int i = 0; i < _size; ++i)
	{
		if (wcscmp(_lrfl.at(i)._name.c_str(), fn) == 0)
		{
			return i;
		}
	}
	return -1;
}

int LastRecentFileList::popFirstAvailableID()
{
	for (int i = 0; i < NB_MAX_LRF_FILE; ++i)
	{
		if (_idFreeArray[i])
		{
			_idFreeArray[i] = false;
			return i + _idBase;
		}
	}
	return 0;
}

void LastRecentFileList::setAvailable(int id)
{
	int index = id - _idBase;
	_idFreeArray[index] = true;
}
