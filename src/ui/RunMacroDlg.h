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

// NPLX: port of upstream WinControls/shortcut/RunMacroDlg (Macro > Run a Macro Multiple Times...), built from
// IDD_RUN_MACRO_DLG. Modeless, like upstream; WM_MACRODLGRUNMACRO / NPPM_GETCURRENTMACROSTATUS go to the parent.
// created by Daniel Volk mordorpost@volkarts.com

#pragma once

#include "ParentMessenger.h"
#include "RcDialog.h"
#include "RunMacroDlg_rc.h"

class RunMacroDlg : public RcDialog
{
	Q_OBJECT

public :
	explicit RunMacroDlg(QWidget* parent = nullptr) : RcDialog(parent) {}

	void init(ParentMessenger sendToParent) { _sendToParent = std::move(sendToParent); }

	void doDialog();

	void initMacroList();

	int isMulti() const { return isCheckedOrNot(IDC_M_RUN_MULTI); }
	int getTimes() const { return _times; }
	int getMacro2Exec() const;

protected :
	void onCommand(int id, int notification) override;
	void reject() override { hide(); } // upstream IDCANCEL: SW_HIDE

private :
	void create(); // upstream WM_INITDIALOG

	ParentMessenger _sendToParent;
	int _times = 1;
	int _macroIndex = 0;
};
