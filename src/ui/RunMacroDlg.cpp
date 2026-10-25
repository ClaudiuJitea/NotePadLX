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

// created by Daniel Volk mordorpost@volkarts.com

#include "RunMacroDlg.h"

#include <algorithm>

#include <QComboBox>
#include <QLineEdit>

#include "Common.h"
#include "Notepad_plus_msgs.h"
#include "Parameters.h"
#include "RunDlg_rc.h"
#include "resource.h"
#include "shortcutRc.h"

namespace
{
#include "RcShortcutRunDialogs.inc"


int getDlgItemInt(const RcDialog* dlg, int id)
{
	// upstream ::GetDlgItemInt(..., FALSE): unsigned, 0 if not a number
	bool ok = false;
	const int value = QString::fromStdWString(dlg->getItemText(id)).toInt(&ok);
	return (ok && value > 0) ? value : 0;
}
}

void RunMacroDlg::doDialog()
{
	if (!isCreated())
		create();
	else
	{
		// Shortcut might have been updated for current session
		// So reload the macro list (issue #4526)
		initMacroList();
	}
	show();
	raise();
	activateWindow();
}

void RunMacroDlg::create()
{
	buildFrom(IDD_RUN_MACRO_DLG_def);

	initMacroList();
	setItemText(IDC_M_RUN_TIMES, std::to_wstring(_times));
	setChecked(IDC_M_RUN_MULTI);

	if (auto* times = qobject_cast<QLineEdit*>(dlgItem(IDC_M_RUN_TIMES)))
		times->setMaxLength(4); // EM_LIMITTEXT
	// NPLX_TODO(Phase 8): nativeLangSpeaker->changeDlgLang(_hSelf, "MultiMacro")
}

void RunMacroDlg::initMacroList()
{
	if (!isCreated()) return;

	NppParameters& nppParam = NppParameters::getInstance();
	std::vector<MacroShortcut> & macroList = nppParam.getMacroList();

	QComboBox* combo = comboBox(IDC_MACRO_COMBO);
	const bool wasUpdating = _isUpdatingControls;
	_isUpdatingControls = true;
	combo->clear();

	if (_sendToParent && static_cast<MacroStatus>(_sendToParent(NPPM_GETCURRENTMACROSTATUS, 0, 0)) == MacroStatus::RecordingStopped)
		combo->addItem(QStringLiteral("Current recorded macro"));

	for (size_t i = 0, len = macroList.size(); i < len ; ++i)
		combo->addItem(QString::fromStdWString(string2wstring(macroList[i].getName(), CP_UTF8)));

	combo->setCurrentIndex(0);
	_isUpdatingControls = wasUpdating;
	_macroIndex = 0;
}

void RunMacroDlg::onCommand(int id, int notification)
{
	switch (id)
	{
		case IDC_M_RUN_MULTI:
		case IDC_M_RUN_EOF:
		{
			if (notification != RcNotifyClicked)
				return;
			const bool isRunMulti = (id == IDC_M_RUN_MULTI);
			if (isRunMulti)
			{
				enableItem(IDC_M_RUN_TIMES, true);
				_times = getDlgItemInt(this, IDC_M_RUN_TIMES);
			}
			else
			{
				enableItem(IDC_M_RUN_TIMES, false);
			}
			// NPLX: upstream greys IDC_TIMES_STATIC through WM_CTLCOLORSTATIC
			enableItem(IDC_TIMES_STATIC, isRunMulti);
			return;
		}

		case IDCANCEL:
			hide();
			return;

		case IDOK:
		{
			if (comboBox(IDC_MACRO_COMBO)->count() > 0 && _sendToParent)
				_sendToParent(WM_MACRODLGRUNMACRO, 0, 0);
			return;
		}

		case IDC_MACRO_COMBO:
		{
			if (notification == RcNotifySelChange)
				_macroIndex = comboBox(IDC_MACRO_COMBO)->currentIndex();
			return;
		}

		case IDC_M_RUN_TIMES:
		{
			switch (notification)
			{
				case RcNotifyKillFocus:
				{
					const int times = getDlgItemInt(this, IDC_M_RUN_TIMES);
					if (times < 1)
						setItemText(IDC_M_RUN_TIMES, L"1");
					return;
				}

				case RcNotifyEditChange:
				{
					_times = std::max<int>(getDlgItemInt(this, IDC_M_RUN_TIMES), 1);
					return;
				}

				default:
					return;
			}
		}

		default:
			return;
	}
}

int RunMacroDlg::getMacro2Exec() const
{
	bool isCurMacroPresent = _sendToParent && static_cast<MacroStatus>(_sendToParent(NPPM_GETCURRENTMACROSTATUS, 0, 0)) == MacroStatus::RecordingStopped;
	return isCurMacroPresent?(_macroIndex - 1):_macroIndex;
}
