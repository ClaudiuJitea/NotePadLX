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

// NPLX: port of upstream WinControls/FindCharsInRange (Search > Find characters in range...), built from
// IDD_FINDCHARACTERS. Modeless, like upstream.

#pragma once

#include <cstdint>

#include "RcDialog.h"
#include "findCharsInRange_rc.h"

class ScintillaEditView;

class FindCharsInRangeDlg : public RcDialog
{
	Q_OBJECT

public :
	explicit FindCharsInRangeDlg(QWidget* parent = nullptr) : RcDialog(parent) {}

	void init(ScintillaEditView **ppEditView);

	void doDialog();

	void display(bool toShow = true);

protected :
	void onCommand(int id, int notification) override;
	void reject() override { display(false); } // upstream IDCANCEL (Close, ESC)

private :
	ScintillaEditView **_ppEditView = nullptr;

	void create(); // upstream WM_INITDIALOG
	bool findCharInRange(unsigned char beginRange, unsigned char endRange, intptr_t startPos, bool direction, bool wrap);
	bool getRangeFromUI(unsigned char & startRange, unsigned char & endRange);
	void getDirectionFromUI(bool & whichDirection, bool & isWrap);
};
