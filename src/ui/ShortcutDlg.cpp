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

#include "ShortcutDlg.h"

#include <algorithm>

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>

#include "Common.h"
#include "RunDlg_rc.h"
#include "RunMacroDlg_rc.h"
#include "ShortcutMapper_rc.h"
#include "resource.h"
#include "shortcutRc.h"

namespace
{
#include "RcShortcutRunDialogs.inc"
#include "RcShortcutMapper.inc"
}

ShortcutDlg::ShortcutDlg(Shortcut& shortcut, std::function<bool(const KeyCombo&)> isConflict, QWidget* parent)
	: RcDialog(parent), _shortcut(shortcut), _keyCombo(shortcut.getKeyCombo()), _isConflict(std::move(isConflict))
{
	buildFrom(IDD_SHORTCUT_DLG_def);
	setModal(true);
}

bool ShortcutDlg::isValid() const
{
	// upstream Shortcut::isValid (virtual) on the combination being edited: probe the real object, then restore it
	Shortcut& sc = const_cast<Shortcut&>(_shortcut);
	const KeyCombo saved = sc.getKeyCombo();
	sc.setKeyCombo(_keyCombo);
	const bool valid = sc.isValid();
	sc.setKeyCombo(saved);
	return valid;
}

void ShortcutDlg::updateOkState()
{
	const size_t textlen = getItemText(IDC_NAME_EDIT).size();
	enableItem(IDOK, isValid() && (textlen > 0 || !_shortcut.canModifyName()));
}

void ShortcutDlg::updateConflictState()
{
	// upstream Shortcut::updateConflictState: NPPM_INTERNAL_FINDKEYCONFLICTS
	const bool isConflict = _isConflict && isEnabled() && _isConflict(_keyCombo);
	showItem(IDC_CONFLICT_STATIC, isConflict);
}

int ShortcutDlg::doDialog()
{
	// upstream WM_INITDIALOG
	setItemText(IDC_NAME_EDIT, string2wstring(_shortcut.canModifyName() ? _shortcut.getMenuName() : _shortcut.getName(), CP_UTF8)); //display the menu name, with ampersands, for macros
	if (!_shortcut.canModifyName())
		qobject_cast<QLineEdit*>(dlgItem(IDC_NAME_EDIT))->setReadOnly(true);

	setChecked(IDC_CTRL_CHECK, _keyCombo._isCtrl);
	setChecked(IDC_ALT_CHECK, _keyCombo._isAlt);
	setChecked(IDC_SHIFT_CHECK, _keyCombo._isShift);

	// while buiding the IDC_KEY_COMBO keys, update the map to which VirtualKey is used for each index
	int iFound = -1;
	_keyIds.clear();
	QComboBox* keyCombo = comboBox(IDC_KEY_COMBO);
	{
		const bool wasUpdating = _isUpdatingControls;
		_isUpdatingControls = true;
		for (size_t i = 0, nbKeys = namedKeyCount(); i < nbKeys; ++i)
		{
			const char* nameStr = namedKeyName(i);
			// only add a key to the IDC_KEY_COMBO list if the string is not empty
			// NPLX: upstream renames the VK_OEM_* keys after the active keyboard layout (mapOemVirtualKeys); the US names here
			if (nameStr[0])
			{
				keyCombo->addItem(QString::fromStdWString(string2wstring(nameStr, CP_UTF8)));
				_keyIds.push_back(namedKeyId(i));
			}

			if (_keyCombo._key == namedKeyId(i))
				iFound = static_cast<int32_t>(_keyIds.size()) - 1;
		}
		if (iFound != -1)
			keyCombo->setCurrentIndex(iFound);
		_isUpdatingControls = wasUpdating;
	}

	// Hide this warning on startup
	showItem(IDC_WARNING_STATIC, false);

	updateOkState();
	updateConflictState();
	// NPLX_TODO(Phase 8): nativeLangSpeaker->changeDlgLang(_hSelf, "ShortcutMapperSubDialg")

	return exec() == QDialog::Accepted ? 0 : -1;
}

void ShortcutDlg::onCommand(int id, int notification)
{
	if (notification == RcNotifyEditChange)
	{
		if (id == IDC_NAME_EDIT)
			updateOkState();
		return;
	}
	if (notification == RcNotifySelChange)
	{
		if (id == IDC_KEY_COMBO)
		{
			const int iSel = comboBox(IDC_KEY_COMBO)->currentIndex();
			if (iSel >= 0 && static_cast<size_t>(iSel) < _keyIds.size())
				_keyCombo._key = _keyIds[iSel];
			updateOkState();
			showItem(IDC_WARNING_STATIC, !isEnabled());
			updateConflictState();
		}
		return;
	}

	switch (id)
	{
		case IDC_CTRL_CHECK :
			_keyCombo._isCtrl = isCheckedOrNot(IDC_CTRL_CHECK);
			updateOkState();
			updateConflictState();
			return;

		case IDC_ALT_CHECK :
			_keyCombo._isAlt = isCheckedOrNot(IDC_ALT_CHECK);
			updateOkState();
			updateConflictState();
			return;

		case IDC_SHIFT_CHECK :
			_keyCombo._isShift = isCheckedOrNot(IDC_SHIFT_CHECK);
			updateConflictState();
			return;

		case IDOK :
			if (!isEnabled())
			{
				_keyCombo._isCtrl = _keyCombo._isAlt = _keyCombo._isShift = false;
			}

			_shortcut.setKeyCombo(_keyCombo);
			if (_shortcut.canModifyName())
			{
				const std::wstring editName = getItemText(IDC_NAME_EDIT);
				_shortcut.setName(wstring2string(editName, CP_UTF8).c_str());
			}
			accept();
			return;

		case IDCANCEL :
			QDialog::reject();
			return;

		default:
			return;
	}
}

// ---- ScintillaKeyMapDlg: upstream ScintillaKeyMap::run_dlgProc (shortcut.cpp)

ScintillaKeyMapDlg::ScintillaKeyMapDlg(ScintillaKeyMap& skm, std::function<bool(const KeyCombo&)> isConflict, QWidget* parent)
	: RcDialog(parent), _skm(skm), _edited(skm), _isConflict(std::move(isConflict))
{
	buildFrom(IDD_SHORTCUTSCINT_DLG_def);
	setModal(true);
}

bool ScintillaKeyMapDlg::isComboValid() const
{
	// upstream isValid() on the temp. _keyCombo
	Shortcut probe(_edited.getName(), _keyCombo._isCtrl, _keyCombo._isAlt, _keyCombo._isShift, _keyCombo._key);
	return probe.isValid();
}

//This procedure uses _keyCombo as a temp. variable to store current settings which can then later be applied (by pressing OK)
void ScintillaKeyMapDlg::applyToCurrentIndex()
{
	const int index = listBox(IDC_LIST_KEYS)->currentRow();
	if (index < 0)
		return;
	_edited.setKeyComboByIndex(index, _keyCombo);
	updateListItem(index);
	listBox(IDC_LIST_KEYS)->setCurrentRow(index);
}

void ScintillaKeyMapDlg::validateDialog()
{
	bool valid = isComboValid();	//current combo valid?
	bool isDisabling = _keyCombo._key == 0;	//true if this keycombo were to disable the shortcut
	bool isDisabled = !_edited.isEnabled();	//true if this shortcut already is
	bool isDuplicate = false; //true if already in the list
	const size_t size = _edited.getSize();

	for (size_t i = 0; i < size; ++i)
	{
		const KeyCombo kc = _edited.getKeyComboByIndex(i);
		if (_keyCombo._key   == kc._key   && _keyCombo._isCtrl  == kc._isCtrl &&
			_keyCombo._isAlt == kc._isAlt && _keyCombo._isShift == kc._isShift)
		{
			isDuplicate = true;
			break;
		}
	}

	enableItem(IDC_BUTTON_ADD, valid && !isDisabling && !isDuplicate);
	enableItem(IDC_BUTTON_APPLY, valid && (!isDisabling || size == 1) && !isDuplicate);
	enableItem(IDC_BUTTON_RMVE, size > 1);
	showItem(IDC_WARNING_STATIC, isDisabled);
	updateConflictState();
}

void ScintillaKeyMapDlg::updateConflictState()
{
	// upstream Shortcut::updateConflictState: NPPM_INTERNAL_FINDKEYCONFLICTS
	const bool isConflict = _isConflict && _keyCombo._key != 0 && _isConflict(_keyCombo);
	showItem(IDC_CONFLICT_STATIC, isConflict);
}

void ScintillaKeyMapDlg::showCurrentSettings()
{
	const int keyIndex = std::max(0, listBox(IDC_LIST_KEYS)->currentRow());
	_keyCombo = _edited.getKeyComboByIndex(keyIndex);
	setChecked(IDC_CTRL_CHECK, _keyCombo._isCtrl);
	setChecked(IDC_ALT_CHECK, _keyCombo._isAlt);
	setChecked(IDC_SHIFT_CHECK, _keyCombo._isShift);
	const bool wasUpdating = _isUpdatingControls;
	_isUpdatingControls = true;
	for (size_t i = 0 ; i < _keyIds.size() ; ++i)
	{
		if (_keyCombo._key == _keyIds[i])
		{
			comboBox(IDC_KEY_COMBO)->setCurrentIndex(static_cast<int>(i));
			break;
		}
	}
	_isUpdatingControls = wasUpdating;
}

void ScintillaKeyMapDlg::updateListItem(int index)
{
	if (QListWidgetItem* item = listBox(IDC_LIST_KEYS)->item(index))
		item->setText(QString::fromStdWString(string2wstring(_edited.toString(index), CP_UTF8)));
}

int ScintillaKeyMapDlg::doDialog()
{
	// upstream WM_INITDIALOG
	setItemText(IDC_NAME_EDIT, string2wstring(_edited.getName(), CP_UTF8));
	_keyCombo = _edited.getKeyComboByIndex(0);

	_keyIds.clear();	// the logic below (always pushing onto the vector) requires that it starts fresh every time
	const bool wasUpdating = _isUpdatingControls;
	_isUpdatingControls = true;
	for (size_t i = 0, nbKeys = namedKeyCount(); i < nbKeys; ++i)
	{
		const char* nameStr = namedKeyName(i);
		// NPLX: upstream renames the VK_OEM_* keys after the keyboard layout (mapOemVirtualKeys); the US names here
		// only add a key to the IDC_KEY_COMBO list if the string is not empty
		if (nameStr[0])
		{
			comboBox(IDC_KEY_COMBO)->addItem(QString::fromStdWString(string2wstring(nameStr, CP_UTF8)));
			_keyIds.push_back(namedKeyId(i));
		}
	}

	for (size_t i = 0; i < _edited.getSize(); ++i)
		listBox(IDC_LIST_KEYS)->addItem(QString::fromStdWString(string2wstring(_edited.toString(i), CP_UTF8)));
	listBox(IDC_LIST_KEYS)->setCurrentRow(0);
	_isUpdatingControls = wasUpdating;

	showCurrentSettings();
	validateDialog();
	// Hide this warning on startup
	showItem(IDC_WARNING_STATIC, false);
	// NPLX_TODO(Phase 8): nativeLangSpeaker->changeDlgLang(_hSelf, "ShortcutMapperSubDialg")

	return exec() == QDialog::Accepted ? 0 : -1;
}

void ScintillaKeyMapDlg::onCommand(int id, int notification)
{
	if (notification == RcNotifySelChange)
	{
		switch (id)
		{
			case IDC_KEY_COMBO:
			{
				const int iSel = comboBox(IDC_KEY_COMBO)->currentIndex();
				if (iSel >= 0 && static_cast<size_t>(iSel) < _keyIds.size())
					_keyCombo._key = _keyIds[iSel];
				showItem(IDC_WARNING_STATIC, !_edited.isEnabled());
				validateDialog();
				return;
			}
			case IDC_LIST_KEYS:
			{
				showCurrentSettings();
				validateDialog();
				return;
			}
			default:
				return;
		}
	}
	if (notification != RcNotifyClicked)
		return;

	switch (id)
	{
		case IDC_CTRL_CHECK :
			_keyCombo._isCtrl = isCheckedOrNot(IDC_CTRL_CHECK);
			validateDialog();
			return;

		case IDC_ALT_CHECK :
			_keyCombo._isAlt = isCheckedOrNot(IDC_ALT_CHECK);
			validateDialog();
			return;

		case IDC_SHIFT_CHECK :
			_keyCombo._isShift = isCheckedOrNot(IDC_SHIFT_CHECK);
			validateDialog();
			return;

		case IDOK :
			_skm = _edited;
			accept();
			return;

		case IDCANCEL :
			QDialog::reject();
			return;

		case IDC_BUTTON_ADD:
		{
			const size_t oldsize = _edited.getSize();
			int res = _edited.addKeyCombo(_keyCombo);
			if (res > -1)
			{
				if (res == static_cast<int32_t>(oldsize))
				{
					listBox(IDC_LIST_KEYS)->addItem(QString::fromStdWString(string2wstring(_edited.toString(res), CP_UTF8)));
				}
				else
				{	//update current string, can happen if it was disabled
					updateListItem(res);
				}
				listBox(IDC_LIST_KEYS)->setCurrentRow(res);
			}
			showCurrentSettings();
			validateDialog();
			return;
		}

		case IDC_BUTTON_RMVE:
		{
			if (_edited.getSize() == 1)	//cannot delete last shortcut
				return;
			int i = listBox(IDC_LIST_KEYS)->currentRow();
			if (i < 0)
				return;
			_edited.removeKeyComboByIndex(i);
			delete listBox(IDC_LIST_KEYS)->takeItem(i);
			if (static_cast<size_t>(i) == _edited.getSize())
				i = static_cast<int>(_edited.getSize()) - 1;
			listBox(IDC_LIST_KEYS)->setCurrentRow(i);
			showCurrentSettings();
			validateDialog();
			return;
		}

		case IDC_BUTTON_APPLY:
		{
			applyToCurrentIndex();
			validateDialog();
			return;
		}

		default:
			return;
	}
}
