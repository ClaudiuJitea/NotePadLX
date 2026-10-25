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

#include "QtNppMenu.h"

#include <QAction>
#include <QMenu>
#include <QWidget>

QAction* QtNppMenu::actionAt(int pos) const
{
	const QList<QAction*> actions = _menu->actions();
	return (pos >= 0 && pos < actions.size()) ? actions.at(pos) : nullptr;
}

QAction* QtNppMenu::beforeAction(int pos) const
{
	return pos < 0 ? nullptr : actionAt(pos);
}

int QtNppMenu::itemCount() const
{
	return static_cast<int>(_menu->actions().size());
}

std::wstring QtNppMenu::itemText(int pos) const
{
	QAction* action = actionAt(pos);
	if (!action || action->isSeparator())
		return std::wstring();
	// Win32 menu strings carry the shortcut after a tab
	QString text = action->text();
	if (!action->shortcut().isEmpty())
		text += QLatin1Char('\t') + action->shortcut().toString(QKeySequence::NativeText);
	return text.toStdWString();
}

NppMenu* QtNppMenu::subMenu(int pos) const
{
	QAction* action = actionAt(pos);
	QMenu* menu = action ? action->menu() : nullptr;
	if (!menu)
		return nullptr;
	auto& sub = _subMenus[menu];
	if (!sub)
		sub = std::make_unique<QtNppMenu>(menu);
	return sub.get();
}

int QtNppMenu::itemId(int pos) const
{
	QAction* action = actionAt(pos);
	if (!action || action->isSeparator() || action->menu())
		return -1;
	bool ok = false;
	const int id = action->data().toInt(&ok);
	return ok ? id : -1;
}

NppMenu* QtNppMenu::insertSubMenu(int pos, const std::wstring& text)
{
	auto* menu = new QMenu(QString::fromStdWString(text), _menu);
	_menu->insertAction(beforeAction(pos), menu->menuAction());
	auto& sub = _subMenus[menu];
	sub = std::make_unique<QtNppMenu>(menu);
	return sub.get();
}

void QtNppMenu::insertItem(int pos, int cmdId, const std::wstring& text)
{
	auto* action = new QAction(QString::fromStdWString(text), _menu);
	action->setData(cmdId);
	_menu->insertAction(beforeAction(pos), action);
}

void QtNppMenu::insertSeparator(int pos)
{
	auto* action = new QAction(_menu);
	action->setSeparator(true);
	_menu->insertAction(beforeAction(pos), action);
}

void QtNppMenu::removeItem(int pos)
{
	QAction* action = actionAt(pos);
	if (!action)
		return;
	if (QMenu* menu = action->menu())
		_subMenus.erase(menu);
	_menu->removeAction(action);
	if (action->parent() == _menu)
		delete action;
}
