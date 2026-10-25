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

// NPLX: port of upstream ScintillaComponent/columnEditor (Edit > Column Editor...), built from IDD_COLUMNEDIT.
// Modeless, like upstream. The validation balloon tip is a QToolTip, the red flash a stylesheet for 250 ms.

#pragma once

#include <string>

#include "NppConstants.h"
#include "Parameters.h"
#include "RcDialog.h"
#include "columnEditor_rc.h"

class ScintillaEditView;

class ColumnEditorDlg : public RcDialog
{
	Q_OBJECT

public :
	explicit ColumnEditorDlg(QWidget* parent = nullptr) : RcDialog(parent) {}
	void init(ScintillaEditView **ppEditView);

	void doDialog();

	void display(bool toShow = true);
	void switchTo(bool toText);
	NumBase getFormat();
	ColumnEditorParam::leadingChoice getLeading();
	NumBase getHexCase();

protected :
	void onCommand(int id, int notification) override;
	void reject() override { onCommand(IDCANCEL, RcNotifyClicked); } // ESC

private :
	ScintillaEditView **_ppEditView = nullptr;
	int _whichFlashRed = 0;
	bool _isBalloonTipShown = false; // upstream _hCurrentBalloonTip

	void create(); // upstream WM_INITDIALOG
	void setNumericFields(const ColumnEditorParam& colEditParam);
	int getNumericFieldValueFromText(NumBase formatChoice, const std::wstring& str);
	int sendValidationErrorMessage(int whichFlashRed, NumBase formatChoice, const std::wstring& str);
};
