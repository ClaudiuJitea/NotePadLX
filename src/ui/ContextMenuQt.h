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

// NPLX: port of upstream WinControls/ContextMenu (the popup part) on QMenu.
// The item model (MenuItemUnit) is in core/ContextMenu.h.

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "ContextMenu.h"

class QAction;
class QMenu;
class QPoint;
class QWidget;

class ContextMenu final
{
public:
	using ActionLookup = std::function<QAction*(int cmdID)>; // upstream: the main menu HMENU
	using CommandHandler = std::function<void(int cmdID)>;   // upstream: WM_COMMAND to hParent

	ContextMenu() = default;
	~ContextMenu();

	void create(QWidget* hParent, const std::vector<MenuItemUnit>& menuItemArray, const ActionLookup& mainMenuHandle = nullptr, bool copyLink = false);
	bool isCreated() const { return _hMenu != nullptr; }
	void setCommandHandler(CommandHandler handler) { _commandHandler = std::move(handler); }

	void display(const QPoint& p) const;

	void enableItem(int cmdID, bool doEnable) const;
	void checkItem(int cmdID, bool doCheck) const;
	QAction* action(int cmdID) const; // NPLX

	QMenu* getMenuHandle() const { return _hMenu.get(); }

	void destroy();

private:
	std::unique_ptr<QMenu> _hMenu;
	CommandHandler _commandHandler;
};
