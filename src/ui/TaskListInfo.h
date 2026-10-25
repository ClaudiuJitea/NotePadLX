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

// NPLX: the document list data of upstream WinControls/TaskList/TaskListDlg.h (used by the Document List panel and
// the Ctrl+Tab switcher).

#pragma once

#include <string>
#include <vector>

enum tb_stat {tb_saved, tb_unsaved, tb_ro, tb_monitored};

struct TaskLstFnStatus {
	int _iView = -1;
	int _docIndex = 0;
	std::wstring _fn;
	int _status = 0;
	void *_bufID = nullptr;
	int _docColor = -1;
	TaskLstFnStatus(const std::wstring& str, int status) : _fn(str), _status(status) {}
	TaskLstFnStatus(int iView, int docIndex, std::wstring str, int status, void* bufID, int docColor)
		: _iView(iView), _docIndex(docIndex), _fn(str), _status(status), _bufID(bufID), _docColor(docColor)
	{}
};

struct TaskListInfo {
	std::vector<TaskLstFnStatus> _tlfsLst;
	int _currentIndex = -1;
};
