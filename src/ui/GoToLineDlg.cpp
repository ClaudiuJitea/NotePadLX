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

#include "GoToLineDlg.h"

#include <string>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>

#include "LocalizationQt.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "resource.h"

GoToLineDlg::GoToLineDlg(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(QStringLiteral("Go To..."));
	setWindowFlag(Qt::WindowContextHelpButtonHint, false);
	setModal(false);

	auto* layout = new QGridLayout(this);

	auto* modeRow = new QHBoxLayout;
	_lineRadio = new QRadioButton(QStringLiteral("&Line"), this);
	_offsetRadio = new QRadioButton(QStringLiteral("&Offset"), this);
	modeRow->addWidget(_lineRadio);
	modeRow->addWidget(_offsetRadio);
	modeRow->addStretch(1);
	layout->addLayout(modeRow, 0, 0, 1, 3);

	auto* hereLabel = new QLabel(QStringLiteral("You are here:"), this);
	layout->addWidget(hereLabel, 1, 0);
	_currLineEdit = new QLineEdit(this);
	_currLineEdit->setReadOnly(true);
	layout->addWidget(_currLineEdit, 1, 1);

	auto* goLabel = new QLabel(QStringLiteral("You want to &go to:"), this);
	_goLineEdit = new QLineEdit(this);
	_goLineEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]*")), _goLineEdit)); // ES_NUMBER
	goLabel->setBuddy(_goLineEdit);
	layout->addWidget(goLabel, 2, 0);
	layout->addWidget(_goLineEdit, 2, 1);

	auto* furtherLabel = new QLabel(QStringLiteral("You can't go further than:"), this);
	layout->addWidget(furtherLabel, 3, 0);
	_lastLine = new QLabel(this);
	layout->addWidget(_lastLine, 3, 1);

	auto* go = new QPushButton(QStringLiteral("Go"), this);
	go->setDefault(true);
	auto* cancel = new QPushButton(QStringLiteral("I'm going nowhere"), this);
	layout->addWidget(go, 2, 2);
	layout->addWidget(cancel, 3, 2);

	connect(go, &QPushButton::clicked, this, [this]() { command(IDOK); });
	connect(cancel, &QPushButton::clicked, this, [this]() { command(IDCANCEL); });
	connect(_lineRadio, &QRadioButton::toggled, this, [this](bool checked) { if (checked) command(IDC_RADIO_GOTOLINE); });
	connect(_offsetRadio, &QRadioButton::toggled, this, [this](bool checked) { if (checked) command(IDC_RADIO_GOTOOFFSET); });

	// upstream WM_INITDIALOG
	_lineRadio->setChecked(true);

	// the upstream control IDs, for the translations (<Dialog><GoToLine>)
	nplx::setCtrlId(_lineRadio, IDC_RADIO_GOTOLINE);
	nplx::setCtrlId(_offsetRadio, IDC_RADIO_GOTOOFFSET);
	nplx::setCtrlId(hereLabel, ID_URHERE_STATIC);
	nplx::setCtrlId(goLabel, ID_UGO_STATIC);
	nplx::setCtrlId(furtherLabel, ID_NOMORETHAN_STATIC);
	nplx::setCtrlId(go, IDOK);
	nplx::setCtrlId(cancel, IDCANCEL);
	nplx::changeDlgLang(this, "GoToLine");
}

void GoToLineDlg::doDialog(bool isRTL)
{
	if (isRTL)
		setLayoutDirection(Qt::RightToLeft);
	display();
}

void GoToLineDlg::display(bool toShow)
{
	setVisible(toShow);
	if (toShow)
	{
		updateLinesNumbers();
		raise();
		activateWindow();
		_goLineEdit->setFocus();
	}
	else
	{
		// clean Line Edit
		_goLineEdit->clear();
	}
}

long long GoToLineDlg::getLine() const
{
	const std::string goLineEditStr = _goLineEdit->text().toStdString();
	if (goLineEditStr.empty())
		return -1;
	char* p_end = nullptr;
	return strtoll(goLineEditStr.c_str(), &p_end, 10);
}

void GoToLineDlg::command(int id)
{
	switch (id)
	{
		case IDCANCEL : // Close
			display(false);
			return;

		case IDOK :
		{
			(*_ppEditView)->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
			long long line = getLine();
			if (line != -1)
			{
				display(false);
				if (_mode == go2line)
				{
					(*_ppEditView)->execute(SCI_ENSUREVISIBLE, static_cast<WPARAM>(line - 1));
					(*_ppEditView)->execute(SCI_GOTOLINE, static_cast<WPARAM>(line - 1));
				}
				else
				{
					size_t posToGoto = 0;
					if (line > 0)
					{
						// make sure not jumping into the middle of a multibyte character
						// or into the middle of a CR/LF pair for Windows files
						auto before = (*_ppEditView)->execute(SCI_POSITIONBEFORE, static_cast<WPARAM>(line));
						posToGoto = (*_ppEditView)->execute(SCI_POSITIONAFTER, before);
					}
					auto sci_line = (*_ppEditView)->execute(SCI_LINEFROMPOSITION, posToGoto);
					(*_ppEditView)->execute(SCI_ENSUREVISIBLE, sci_line);
					(*_ppEditView)->execute(SCI_GOTOPOS, posToGoto);
				}
			}
			unsigned long MODEVENTMASK_ON = NppParameters::getInstance().getScintillaModEventMask();
			(*_ppEditView)->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);

			if (_linkTriggered)
				_linkTriggered(); // upstream ::SendMessage(_hParent, WM_NOTIFY, LINKTRIGGERED, SCN_PAINTED)
			(*_ppEditView)->setFocus();
			return;
		}

		case IDC_RADIO_GOTOLINE:
		case IDC_RADIO_GOTOOFFSET:
		{
			if (id == IDC_RADIO_GOTOLINE)
			{
				_mode = go2line;
			}
			else
			{
				_mode = go2offsset;
			}
			updateLinesNumbers();
			return;
		}

		default:
			break;
	}
}

void GoToLineDlg::updateLinesNumbers() const
{
	if (!_ppEditView || !*_ppEditView)
		return;

	size_t current = 0;
	size_t limit = 0;

	if (_mode == go2line)
	{
		current = (*_ppEditView)->getCurrentLineNumber() + 1;
		limit = (*_ppEditView)->execute(SCI_GETLINECOUNT);
	}
	else
	{
		current = (*_ppEditView)->execute(SCI_GETCURRENTPOS);
		size_t currentDocLength = (*_ppEditView)->getCurrentDocLen();
		limit = (currentDocLength > 0 ? currentDocLength - 1 : 0);
	}
	_currLineEdit->setText(QString::number(current));
	_lastLine->setText(QString::number(limit));
}
