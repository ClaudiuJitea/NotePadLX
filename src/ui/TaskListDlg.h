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

// NPLX: port of upstream WinControls/TaskList (TaskListDlg + TaskList): the Ctrl+Tab document switcher, a
// borderless popup listing the documents (most recently used first). Tab / arrows / the wheel move the selection;
// releasing Ctrl, or a click, switches to the selected document; Esc or Ctrl+F4 dismisses it.

#pragma once

#include <functional>

#include <QDialog>

#include "NppConstants.h" // dirUp, dirDown
#include "TaskListInfo.h"

class QListWidget;

class TaskListDlg : public QDialog
{
	Q_OBJECT

public :
	// getTaskListInfo: upstream WM_GETTASKLISTINFO; activate: upstream NPPM_ACTIVATEDOC (view, index)
	TaskListDlg(QWidget* parent, bool dir, std::function<void(TaskListInfo&)> getTaskListInfo, std::function<void(int view, int index)> activate);
	~TaskListDlg() override;

	int doDialog();

	static int _instanceCount;

	// NPLX: tests
	int currentIndex() const { return _currentIndex; }
	void moveSelection(int direction);
	void pick(); // upstream ID_PICKEDUP
	QListWidget* list() const { return _taskList; }

protected:
	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override; // upstream TaskListSelectProc WM_KEYUP
	void wheelEvent(QWheelEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;
	void showEvent(QShowEvent* event) override; // upstream WM_ACTIVATE

private :
	QListWidget* _taskList = nullptr;
	TaskListInfo _taskListInfo;
	std::function<void(int view, int index)> _activate;
	int _currentIndex = 0;
};
