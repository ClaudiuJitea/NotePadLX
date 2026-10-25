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

// NPLX: the dialog part of upstream Shortcut (Shortcut::doDialog / run_dlgProc, IDD_SHORTCUT_DLG): edits the name and
// the key combination of a Shortcut (a macro, a Run command, a menu command). The core Shortcut class stays toolkit-free.

#pragma once

#include <functional>
#include <vector>

#include "RcDialog.h"
#include "shortcut.h"

class ShortcutDlg : public RcDialog
{
	Q_OBJECT

public:
	// isConflict: upstream NPPM_INTERNAL_FINDKEYCONFLICTS (the key is already used by another command)
	ShortcutDlg(Shortcut& shortcut, std::function<bool(const KeyCombo&)> isConflict, QWidget* parent = nullptr);

	// upstream Shortcut::doDialog: 0 on OK (the shortcut is updated), -1 when cancelled
	int doDialog();

protected:
	void onCommand(int id, int notification) override;
	void reject() override { onCommand(IDCANCEL, RcNotifyClicked); }

private:

	Shortcut& _shortcut;
	KeyCombo _keyCombo;
	std::function<bool(const KeyCombo&)> _isConflict;
	std::vector<UCHAR> _keyIds; // upstream oemVkUsedIDs: the key of each combo index

	bool isValid() const;   // upstream Shortcut::isValid on the edited combination
	bool isEnabled() const { return _keyCombo._key != 0; }
	void updateOkState();
	void updateConflictState();
};

// NPLX: the dialog part of upstream ScintillaKeyMap (ScintillaKeyMap::doDialog / run_dlgProc, IDD_SHORTCUTSCINT_DLG):
// the list of key combinations of a Scintilla command
class ScintillaKeyMapDlg : public RcDialog
{
	Q_OBJECT

public:
	ScintillaKeyMapDlg(ScintillaKeyMap& skm, std::function<bool(const KeyCombo&)> isConflict, QWidget* parent = nullptr);

	// upstream ScintillaKeyMap::doDialog: 0 on OK (the key map is updated), -1 when cancelled
	int doDialog();

protected:
	void onCommand(int id, int notification) override;
	void reject() override { onCommand(IDCANCEL, RcNotifyClicked); }

private:
	ScintillaKeyMap& _skm;
	ScintillaKeyMap _edited;  // NPLX: upstream edits the key map in place and the caller keeps a copy
	KeyCombo _keyCombo;       // upstream uses _keyCombo as a temp. variable for the current settings
	std::function<bool(const KeyCombo&)> _isConflict;
	std::vector<UCHAR> _keyIds; // upstream oemVkUsedIDs

	void applyToCurrentIndex();
	void validateDialog();
	void showCurrentSettings();
	void updateListItem(int index);
	void updateConflictState();
	bool isComboValid() const;
};
