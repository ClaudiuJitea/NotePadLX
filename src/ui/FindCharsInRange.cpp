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


#include "FindCharsInRange.h"

#include <stdexcept>
#include <vector>

#include <QLineEdit>

#include "LocalizationQt.h"
#include "Scintilla.h"

#include "NppConstants.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "resource.h"

namespace
{
#include "RcFindCharsInRange.inc"

// upstream ::GetDlgItemInt(..., &translated, FALSE): unsigned
int getDlgItemInt(const RcDialog* dlg, int id, bool* translated)
{
	const int value = QString::fromStdWString(dlg->getItemText(id)).toInt(translated);
	if (*translated && value < 0)
		*translated = false;
	return value;
}
}

void FindCharsInRangeDlg::init(ScintillaEditView **ppEditView)
{
	if (!ppEditView)
		throw std::runtime_error("FindCharsInRangeDlg::init : ppEditView is null.");
	_ppEditView = ppEditView;
}

void FindCharsInRangeDlg::doDialog()
{
	if (!isCreated())
		create();
	display();
}

void FindCharsInRangeDlg::display(bool toShow)
{
	setVisible(toShow);
	if (toShow)
	{
		raise();
		activateWindow();
	}
}

void FindCharsInRangeDlg::create()
{
	// upstream WM_INITDIALOG
	buildFrom(IDD_FINDCHARACTERS_def);
	const bool wasUpdating = _isUpdatingControls;
	_isUpdatingControls = true;

	for (int id : { IDC_RANGESTART_EDIT, IDC_RANGEEND_EDIT })
		if (auto* edit = qobject_cast<QLineEdit*>(dlgItem(id)))
			edit->setMaxLength(3); // EM_LIMITTEXT
	setChecked(IDC_NONASCCI_RADIO);
	setChecked(ID_FINDCHAR_DIRDOWN);

	setItemText(IDC_RANGESTART_EDIT, L"0");
	setItemText(IDC_RANGEEND_EDIT, L"255");

	enableItem(IDC_RANGESTART_EDIT, false);
	enableItem(IDC_RANGEEND_EDIT, false);

	_isUpdatingControls = wasUpdating;
	nplx::changeDlgLang(*this, "FindCharsInRange");
}

void FindCharsInRangeDlg::onCommand(int id, int notification)
{
	if (notification != RcNotifyClicked)
		return;

	switch (id)
	{
		case IDC_NONASCCI_RADIO:
		case IDC_ASCCI_RADIO:
		{
			enableItem(IDC_RANGESTART_EDIT, false);
			enableItem(IDC_RANGEEND_EDIT, false);
			return;
		}

		case IDC_MYRANGE_RADIO:
		{
			enableItem(IDC_RANGESTART_EDIT, true);
			enableItem(IDC_RANGEEND_EDIT, true);
			return;
		}

		case IDCANCEL: // Close
			display(false);
			return;

		case ID_FINDCHAR_NEXT:
		{
			intptr_t currentPos = (*_ppEditView)->execute(SCI_GETCURRENTPOS);
			unsigned char startRange = 0;
			unsigned char endRange = 255;
			bool direction = dirDown;
			bool isWrap = true;
			if (!getRangeFromUI(startRange, endRange))
			{
				//STOP!
				NppParameters::getInstance().getNativeLangSpeaker()->messageBox("FindCharRangeValueError",
					L"You should type between 0 and 255.",
					L"Range Value problem",
					MB_OK);
				return;
			}
			getDirectionFromUI(direction, isWrap);
			findCharInRange(startRange, endRange, currentPos, direction, isWrap);
			return;
		}

		default :
			break;
	}
}

bool FindCharsInRangeDlg::findCharInRange(unsigned char beginRange, unsigned char endRange, intptr_t startPos, bool direction, bool wrap)
{
	size_t totalSize = (*_ppEditView)->getCurrentDocLen();
	if (startPos == -1)
		startPos = direction == dirDown ? 0 : totalSize - 1;
	if (startPos > static_cast<intptr_t>(totalSize))
		return false;

	std::vector<char> content(totalSize + 1); // NPLX: was new char[]
	(*_ppEditView)->getText(content.data(), 0, totalSize);

	bool isFound = false;
	size_t found = 0;

	for (int64_t i = startPos - (direction == dirDown ? 0 : 1);
		(direction == dirDown) ? i < static_cast<int64_t>(totalSize) : i >= 0 ;
		(direction == dirDown) ? (++i) : (--i))
	{
		if (static_cast<unsigned char>(content[i]) >= beginRange && static_cast<unsigned char>(content[i]) <= endRange)
		{
			found = static_cast<size_t>(i);
			isFound = true;
			break;
		}
	}

	if (!isFound)
	{
		if (wrap)
		{
			for (int64_t i = (direction == dirDown ? 0: totalSize - 1);
				(direction == dirDown) ? i < static_cast<int64_t>(totalSize) : i >= 0 ;
				(direction == dirDown) ? (++i) : (--i))
			{
				if (static_cast<unsigned char>(content[i]) >= beginRange && static_cast<unsigned char>(content[i]) <= endRange)
				{
					found = static_cast<size_t>(i);
					isFound = true;
					break;
				}
			}
		}
	}

	if (isFound)
	{
		auto sci_line = (*_ppEditView)->execute(SCI_LINEFROMPOSITION, found);
		(*_ppEditView)->execute(SCI_ENSUREVISIBLE, sci_line);
		(*_ppEditView)->execute(SCI_GOTOPOS, found);
		(*_ppEditView)->execute(SCI_SETSEL, (direction == dirDown)? found : found+1, (direction == dirDown) ? found + 1 : found);
	}
	return isFound;
}

void FindCharsInRangeDlg::getDirectionFromUI(bool & whichDirection, bool & isWrap)
{
	whichDirection = isCheckedOrNot(ID_FINDCHAR_DIRUP);
	isWrap = isCheckedOrNot(ID_FINDCHAR_WRAP);
}

bool FindCharsInRangeDlg::getRangeFromUI(unsigned char & startRange, unsigned char & endRange)
{
	if (isCheckedOrNot(IDC_NONASCCI_RADIO))
	{
		startRange = 128;
		endRange = 255;
		return true;
	}

	if (isCheckedOrNot(IDC_ASCCI_RADIO))
	{
		startRange = 0;
		endRange = 127;
		return true;
	}

	if (isCheckedOrNot(IDC_MYRANGE_RADIO))
	{
		bool startBool = false;
		bool endBool = false;
		int start = getDlgItemInt(this, IDC_RANGESTART_EDIT, &startBool);
		int end = getDlgItemInt(this, IDC_RANGEEND_EDIT, &endBool);

		if (!startBool || !endBool)
			return false;
		if (start > 255 || end > 255)
			return false;
		if (start > end)
			return false;
		startRange = static_cast<unsigned char>(start);
		endRange = static_cast<unsigned char>(end);
		return true;
	}

	return false;
}
