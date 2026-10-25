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

// NPLX: the base of the dockable panels, in place of upstream DockingDlgInterface + DockingManager: a QDockWidget
// (QMainWindow docks, tabs and floats it, and saves the layout). The upstream calls are kept: display(),
// isClosed() / setClosed(), grabFocus(); DMN_CLOSE (the dock's close button) reaches the owner through
// setCloseHandler (upstream sends the panel's menu command), DMN_SWITCHIN calls onSwitchIn().

#pragma once

#include <functional>

#include <QDockWidget>

#include "win32compat.h" // COLORREF (the panels take the editor theme colours)

class DockingPanel : public QDockWidget
{
	Q_OBJECT

public:
	// dlgID: the menu command showing the panel (upstream DockedWidgetData::dlgID), also its saved name
	DockingPanel(const QString& title, int dlgID, const QString& objectName, QWidget* parent = nullptr);

	int dlgID() const { return _dlgID; }
	void display(bool toShow = true);
	bool isClosed() const { return _isClosed; }
	void setClosed(bool toClose) { _isClosed = toClose; }
	virtual void grabFocus();

	void setCloseHandler(std::function<void(int dlgID)> handler) { _closeHandler = std::move(handler); }

protected:
	void closeEvent(QCloseEvent* event) override; // upstream DMN_CLOSE
	virtual void onSwitchIn() {}                  // upstream DMN_SWITCHIN

private:
	int _dlgID = 0;
	bool _isClosed = false;
	std::function<void(int)> _closeHandler;
};
