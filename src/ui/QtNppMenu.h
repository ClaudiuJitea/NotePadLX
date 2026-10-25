// This file is part of NotePadLX, a Linux port of Notepad++.
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

// NPLX: NppMenu (the HMENU replacement used by the portable core) on a QMenuBar or a QMenu.
// Command ids are stored in QAction::data(), as Notepad_plus::buildMainMenu does.

#pragma once

#include <map>
#include <memory>

#include "NppMenu.h"

class QAction;
class QMenu;
class QWidget;

class QtNppMenu : public NppMenu
{
public:
	explicit QtNppMenu(QWidget* menuWidget) : _menu(menuWidget) {} // a QMenuBar or a QMenu

	int itemCount() const override;
	std::wstring itemText(int pos) const override;
	NppMenu* subMenu(int pos) const override;
	int itemId(int pos) const override;

	NppMenu* insertSubMenu(int pos, const std::wstring& text) override;
	void insertItem(int pos, int cmdId, const std::wstring& text) override;
	void insertSeparator(int pos) override;
	void removeItem(int pos) override;

private:
	QAction* actionAt(int pos) const;
	QAction* beforeAction(int pos) const; // nullptr appends

	QWidget* _menu = nullptr;
	mutable std::map<QMenu*, std::unique_ptr<QtNppMenu>> _subMenus;
};
