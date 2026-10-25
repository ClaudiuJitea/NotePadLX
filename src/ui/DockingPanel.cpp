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

#include "DockingPanel.h"

#include <QCloseEvent>

DockingPanel::DockingPanel(const QString& title, int dlgID, const QString& objectName, QWidget* parent)
	: QDockWidget(title, parent), _dlgID(dlgID)
{
	setObjectName(objectName); // QMainWindow::saveState() / restoreState() key
	setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
		if (visible && !_isClosed)
			onSwitchIn(); // e.g. its tab brought to the front
	});
}

void DockingPanel::display(bool toShow)
{
	setVisible(toShow);
	if (toShow)
		raise();
}

void DockingPanel::grabFocus()
{
	if (widget())
		widget()->setFocus();
}

void DockingPanel::closeEvent(QCloseEvent* event)
{
	if (_closeHandler)
	{
		// upstream: ::SendMessage(_hParent, WM_COMMAND, dlgID, 0), which hides the panel and unchecks its menu item
		event->ignore();
		_closeHandler(_dlgID);
		return;
	}
	_isClosed = true;
	QDockWidget::closeEvent(event);
}
