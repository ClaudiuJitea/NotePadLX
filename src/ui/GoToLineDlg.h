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

// NPLX: port of upstream ScintillaComponent/GoToLineDlg on QDialog (layout from IDD_GOLINE in Notepad_plus.rc).

#pragma once

#include <functional>

#include <QDialog>

class QLabel;
class QLineEdit;
class QRadioButton;
class ScintillaEditView;

class GoToLineDlg : public QDialog
{
	Q_OBJECT

public :
	explicit GoToLineDlg(QWidget* parent = nullptr);

	void init(ScintillaEditView **ppEditView) { _ppEditView = ppEditView; }
	// NPLX: upstream sends SCN_PAINTED with LINKTRIGGERED to the parent after a jump (re-detects the clickable links)
	void setLinkTriggered(std::function<void()> linkTriggered) { _linkTriggered = std::move(linkTriggered); }

	void doDialog(bool isRTL = false);
	void display(bool toShow = true);
	void updateLinesNumbers() const;

	// NPLX: upstream WM_COMMAND
	void command(int id);
	QLineEdit* goLineEdit() const { return _goLineEdit; } // NPLX: tests

protected :
	enum mode {go2line, go2offsset};
	mode _mode = go2line;

	void reject() override { command(IDCANCEL_GOTOLINE); }

private :
	static constexpr int IDCANCEL_GOTOLINE = 2; // IDCANCEL
	std::function<void()> _linkTriggered;
	ScintillaEditView **_ppEditView = nullptr;

	QRadioButton* _lineRadio = nullptr;
	QRadioButton* _offsetRadio = nullptr;
	QLineEdit* _currLineEdit = nullptr;
	QLineEdit* _goLineEdit = nullptr;
	QLabel* _lastLine = nullptr;

	long long getLine() const;
};
