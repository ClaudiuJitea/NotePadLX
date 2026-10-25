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

#include "ContextMenuQt.h"

#include <QAction>
#include <QMenu>

#include "Parameters.h"
#include "localization.h"
#include "menuCmdID.h"

ContextMenu::~ContextMenu()
{
	destroy();
}

void ContextMenu::create(QWidget* hParent, const std::vector<MenuItemUnit>& menuItemArray, const ActionLookup& mainMenuHandle, bool copyLink)
{
	destroy();
	_hMenu = std::make_unique<QMenu>(hParent);

	bool lastIsSep = false;
	QMenu* hParentFolder = nullptr;
	std::wstring currentParentFolderStr;

	auto insertItem = [this](QMenu* menu, const MenuItemUnit& item) -> QAction* {
		if (item._cmdID == 0)
			return menu->addSeparator();
		// NPLX: '&' keeps its mnemonic meaning, like the Win32 menu strings
		QAction* action = menu->addAction(QString::fromStdWString(item._itemName));
		const int cmdID = static_cast<int>(item._cmdID);
		action->setData(cmdID);
		QObject::connect(action, &QAction::triggered, menu, [this, cmdID]() {
			if (_commandHandler)
				_commandHandler(cmdID);
		});
		return action;
	};

	for (size_t i = 0, len = menuItemArray.size(); i < len; ++i)
	{
		const MenuItemUnit & item = menuItemArray[i];
		if (item._parentFolderName.empty())
		{
			currentParentFolderStr.clear();
			hParentFolder = nullptr;
		}
		else
		{
			if (item._parentFolderName != currentParentFolderStr)
			{
				currentParentFolderStr = item._parentFolderName;
				hParentFolder = _hMenu->addMenu(QString::fromStdWString(currentParentFolderStr));
			}
		}

		QAction* inserted = nullptr;
		if (hParentFolder)
		{
			inserted = insertItem(hParentFolder, item);
			lastIsSep = false;
		}
		else if ((i == 0 || i == menuItemArray.size() - 1) && item._cmdID == 0)
		{
			lastIsSep = true;
		}
		else if (item._cmdID != 0)
		{
			inserted = insertItem(_hMenu.get(), item);
			lastIsSep = false;
		}
		else if (/*item._cmdID == 0 &&*/ !lastIsSep)
		{
			inserted = insertItem(_hMenu.get(), item);
			lastIsSep = true;
		}
		else // last item is separator and current item is separator
		{
			lastIsSep = true;
		}

		if (mainMenuHandle && inserted && item._cmdID != 0)
		{
			// upstream: GetMenuState(mainMenuHandle, ...) for the enabled / checked state, and the bitmaps
			if (const QAction* mainAction = mainMenuHandle(static_cast<int>(item._cmdID)))
			{
				if (!mainAction->isEnabled())
				{
					inserted->setEnabled(false);
					inserted->setToolTip(mainAction->toolTip());
				}
				if (mainAction->isCheckable() && mainAction->isChecked())
				{
					inserted->setCheckable(true);
					inserted->setChecked(true);
				}
				inserted->setIcon(mainAction->icon());
			}
		}

		if (copyLink && inserted && (item._cmdID == IDM_EDIT_COPY))
		{
			NativeLangSpeaker* nativeLangSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
			std::wstring localized = nativeLangSpeaker->getNativeLangMenuString(IDM_EDIT_COPY_LINK);
			if (localized.length() == 0)
				localized = L"Copy link";
			// upstream replaces the last item added (the Copy entry) by "Copy link"
			inserted->setText(QString::fromStdWString(localized));
			inserted->setData(static_cast<int>(IDM_EDIT_COPY_LINK));
			inserted->setEnabled(true);
			QObject::disconnect(inserted, &QAction::triggered, nullptr, nullptr);
			QObject::connect(inserted, &QAction::triggered, _hMenu.get(), [this]() {
				if (_commandHandler)
					_commandHandler(IDM_EDIT_COPY_LINK);
			});
		}
	}
}

void ContextMenu::display(const QPoint& p) const
{
	if (_hMenu)
		_hMenu->exec(p);
}

QAction* ContextMenu::action(int cmdID) const
{
	if (!_hMenu)
		return nullptr;
	for (QAction* a : _hMenu->findChildren<QAction*>())
	{
		if (!a->isSeparator() && !a->menu() && a->data().toInt() == cmdID)
			return a;
	}
	return nullptr;
}

void ContextMenu::enableItem(int cmdID, bool doEnable) const
{
	if (QAction* a = action(cmdID))
		a->setEnabled(doEnable);
}

void ContextMenu::checkItem(int cmdID, bool doCheck) const
{
	if (QAction* a = action(cmdID))
	{
		a->setCheckable(true);
		a->setChecked(doCheck);
	}
}

void ContextMenu::destroy()
{
	_hMenu.reset();
}
