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

#include "UserDefineDialog.h"

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <vector>

#include <QComboBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpressionValidator>
#include <QSlider>
#include <QTabBar>

#include "SciLexer.h"

#include "ColourPicker.h"
#include "Common.h"
#include "NppConstants.h"
#include "NppXml.h"
#include "ScintillaEditView.h"
#include "UserDefineResource.h"
#include "colors.h"
#include "localization.h"
#include "menuCmdID.h"
#include "resource.h"

namespace
{
	// upstream dialog resources (generated from UserDefineDialog.rc)
#include "RcDialogs.inc"

	// upstream _itow(value, buffer, 10)
	void itow(int value, wchar_t* buffer)
	{
		const std::wstring s = std::to_wstring(value);
		std::wcscpy(buffer, s.c_str());
	}
}

using namespace std;

UserLangContainer * SharedParametersDialog::_pUserLang = NULL;
ScintillaEditView * SharedParametersDialog::_pScintilla = NULL;
std::wstring SharedParametersDialog::_kwlBuffer = std::wstring(max_char, L'\0');

static void convertTo(wchar_t* dest, const wchar_t* toConvert, const wchar_t* prefix)
{
	bool inGroup = false;
	size_t index = std::wcslen(dest);
	if (index > 0)
		dest[index++] = ' ';
	dest[index++] = prefix[0];
	dest[index++] = prefix[1];

	for (size_t i = 0, len = std::wcslen(toConvert); i < len && index < max_char - 7; ++i)
	{
		if (i == 0 && toConvert[i] == '(' && toConvert[i + 1] == '(')
		{
			inGroup = true;
		}
		else if (toConvert[i] == ' ' && toConvert[i + 1] == '(' && toConvert[i + 2] == '(')
		{
			inGroup = true;
			dest[index++] = ' ';
			dest[index++] = prefix[0];
			dest[index++] = prefix[1];
			++i;    // skip space
		}

		if (inGroup && i >= 2 && toConvert[i - 1] == ')' && toConvert[i - 2] == ')') // NPLX: i >= 2 (upstream reads before the start)
		{
			inGroup = false;
		}

		if (toConvert[i] == ' ')
		{
			if (toConvert[i + 1] != ' ' && toConvert[i + 1] != '\0')
			{
				dest[index++] = ' ';
				if (!inGroup)
				{
					dest[index++] = prefix[0];
					dest[index++] = prefix[1];
				}
			}
		}
		else
		{
			dest[index++] = toConvert[i];
		}
	}
	dest[index] = '\0';
}

// ============================================================================
// SharedParametersDialog
// ============================================================================

size_t SharedParametersDialog::getTextIntoBuffer(int ctrlID)
{
	// NPLX: _kwlBuffer keeps its max_char size (retrieve/convertTo write into it), like the Win32 buffer
	const std::wstring text = getItemText(ctrlID);
	const size_t len = std::min<size_t>(text.size(), max_char - 1);
	std::copy_n(text.data(), len, _kwlBuffer.data());
	_kwlBuffer[len] = L'\0';
	return len;
}

bool SharedParametersDialog::setPropertyByCheck(int id, bool & bool2set)
{
	bool2set = isCheckedOrNot(id);

	if (_pScintilla->getCurrentBuffer()->getLangType() == L_USER)
		_pScintilla->styleChange();

	return TRUE;
}

void SharedParametersDialog::openStyler(int stylerIndex, int enabledNesters)
{
	StylerDlg stylerDlg(this, stylerIndex, enabledNesters);
	stylerDlg.doDialog();
}

void SharedParametersDialog::onCommand(int id, int notification)
{
	if (notification == RcNotifyEditChange)
	{
		setKeywords2List(id);

		if (_pScintilla->getCurrentBuffer()->getLangType() == L_USER)
			_pScintilla->styleChange();
	}
}

// ============================================================================
// FolderStyleDialog
// ============================================================================

FolderStyleDialog::FolderStyleDialog(QWidget* parent)
	: SharedParametersDialog(parent)
{
	buildFrom(IDD_FOLDER_STYLE_DLG_def);
	// upstream URLCtrl on IDC_WEB_HELP_LINK
	if (auto* link = qobject_cast<QLabel*>(dlgItem(IDC_WEB_HELP_LINK)))
	{
		link->setText(QStringLiteral("<a href=\"https://npp-user-manual.org/docs/user-defined-language-system/\">%1</a>").arg(link->text()));
		link->setOpenExternalLinks(true);
	}
}

void FolderStyleDialog::onCommand(int id, int notification)
{
	if (notification != RcNotifyClicked)
	{
		SharedParametersDialog::onCommand(id, notification);
		return;
	}

	switch (id)
	{
		case IDC_FOLDER_FOLD_COMPACT :
			setPropertyByCheck(id, _pUserLang->_foldCompact);
			return;
		case IDC_FOLDER_IN_CODE1_STYLER :
			openStyler(SCE_USER_STYLE_FOLDER_IN_CODE1, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_FOLDER_IN_CODE2_STYLER :
			openStyler(SCE_USER_STYLE_FOLDER_IN_CODE2, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_FOLDER_IN_COMMENT_STYLER :
			openStyler(SCE_USER_STYLE_FOLDER_IN_COMMENT, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_DEFAULT_STYLER :
			openStyler(SCE_USER_STYLE_DEFAULT, SCE_USER_MASK_NESTING_NONE);
			return;
		default :
			SharedParametersDialog::onCommand(id, notification);
	}
}

void FolderStyleDialog::setKeywords2List(int ctrlID)
{
	switch (ctrlID)
	{
		case IDC_FOLDER_IN_CODE1_OPEN_EDIT :
		case IDC_FOLDER_IN_CODE1_MIDDLE_EDIT :
		case IDC_FOLDER_IN_CODE1_CLOSE_EDIT :
		case IDC_FOLDER_IN_CODE2_OPEN_EDIT :
		case IDC_FOLDER_IN_CODE2_MIDDLE_EDIT :
		case IDC_FOLDER_IN_CODE2_CLOSE_EDIT :
		case IDC_FOLDER_IN_COMMENT_OPEN_EDIT :
		case IDC_FOLDER_IN_COMMENT_MIDDLE_EDIT :
		case IDC_FOLDER_IN_COMMENT_CLOSE_EDIT :
		{
			const size_t len = getTextIntoBuffer(ctrlID);
			_pUserLang->_keywordLists[globalMappper().dialogMapper[ctrlID]] = (len > 0) ? wstring2string(_kwlBuffer).c_str() : "";
			break;
		}
	}
}

void FolderStyleDialog::updateDlg()
{
	setChecked(IDC_FOLDER_FOLD_COMPACT, _pUserLang->_foldCompact);

	setItemText(IDC_FOLDER_IN_CODE1_OPEN_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_FOLDERS_IN_CODE1_OPEN]));
	setItemText(IDC_FOLDER_IN_CODE1_MIDDLE_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_FOLDERS_IN_CODE1_MIDDLE]));
	setItemText(IDC_FOLDER_IN_CODE1_CLOSE_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_FOLDERS_IN_CODE1_CLOSE]));
	setItemText(IDC_FOLDER_IN_CODE2_OPEN_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_FOLDERS_IN_CODE2_OPEN]));
	setItemText(IDC_FOLDER_IN_CODE2_MIDDLE_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_FOLDERS_IN_CODE2_MIDDLE]));
	setItemText(IDC_FOLDER_IN_CODE2_CLOSE_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_FOLDERS_IN_CODE2_CLOSE]));
	setItemText(IDC_FOLDER_IN_COMMENT_OPEN_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_FOLDERS_IN_COMMENT_OPEN]));
	setItemText(IDC_FOLDER_IN_COMMENT_MIDDLE_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_FOLDERS_IN_COMMENT_MIDDLE]));
	setItemText(IDC_FOLDER_IN_COMMENT_CLOSE_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_FOLDERS_IN_COMMENT_CLOSE]));
}

void FolderStyleDialog::retrieve(wchar_t* dest, size_t destMaxLen, const wchar_t* toRetrieve, const wchar_t* prefix)
{
	size_t j = 0;
	bool begin2Copy = false;

	for (size_t i = 0, len = std::wcslen(toRetrieve); i < len; ++i)
	{
		if ((i == 0 || (toRetrieve[i-1] == ' ')) && (toRetrieve[i] == prefix[0] && toRetrieve[i+1] == prefix[1]))
		{
			if (j > 0 && j < destMaxLen)
				dest[j++] = ' ';

			begin2Copy = true;
			++i;
			continue;
		}
		else if (((toRetrieve[i] == ' ') && begin2Copy))
		{
			begin2Copy = false;
		}

		if (begin2Copy && j < destMaxLen)
			dest[j++] = toRetrieve[i];
	}

	if (j < destMaxLen)
		dest[j++] = '\0';
}

// ============================================================================
// KeyWordsStyleDialog
// ============================================================================

KeyWordsStyleDialog::KeyWordsStyleDialog(QWidget* parent)
	: SharedParametersDialog(parent)
{
	buildFrom(IDD_KEYWORD_STYLE_DLG_def);
	// upstream: extend Keyword edit boxes to hold 128k of wchar_t (QPlainTextEdit has no limit)
}

void KeyWordsStyleDialog::onCommand(int id, int notification)
{
	if (notification != RcNotifyClicked)
	{
		SharedParametersDialog::onCommand(id, notification);
		return;
	}

	switch (id)
	{
		case IDC_KEYWORD1_PREFIX_CHECK :
			setPropertyByCheck(id, _pUserLang->_isPrefix[0]);
			return;
		case IDC_KEYWORD2_PREFIX_CHECK :
			setPropertyByCheck(id, _pUserLang->_isPrefix[1]);
			return;
		case IDC_KEYWORD3_PREFIX_CHECK :
			setPropertyByCheck(id, _pUserLang->_isPrefix[2]);
			return;
		case IDC_KEYWORD4_PREFIX_CHECK :
			setPropertyByCheck(id, _pUserLang->_isPrefix[3]);
			return;
		case IDC_KEYWORD5_PREFIX_CHECK :
			setPropertyByCheck(id, _pUserLang->_isPrefix[4]);
			return;
		case IDC_KEYWORD6_PREFIX_CHECK :
			setPropertyByCheck(id, _pUserLang->_isPrefix[5]);
			return;
		case IDC_KEYWORD7_PREFIX_CHECK :
			setPropertyByCheck(id, _pUserLang->_isPrefix[6]);
			return;
		case IDC_KEYWORD8_PREFIX_CHECK :
			setPropertyByCheck(id, _pUserLang->_isPrefix[7]);
			return;

		case IDC_KEYWORD1_STYLER :
			openStyler(SCE_USER_STYLE_KEYWORD1, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_KEYWORD2_STYLER :
			openStyler(SCE_USER_STYLE_KEYWORD2, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_KEYWORD3_STYLER :
			openStyler(SCE_USER_STYLE_KEYWORD3, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_KEYWORD4_STYLER :
			openStyler(SCE_USER_STYLE_KEYWORD4, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_KEYWORD5_STYLER :
			openStyler(SCE_USER_STYLE_KEYWORD5, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_KEYWORD6_STYLER :
			openStyler(SCE_USER_STYLE_KEYWORD6, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_KEYWORD7_STYLER :
			openStyler(SCE_USER_STYLE_KEYWORD7, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_KEYWORD8_STYLER :
			openStyler(SCE_USER_STYLE_KEYWORD8, SCE_USER_MASK_NESTING_NONE);
			return;
		default :
			SharedParametersDialog::onCommand(id, notification);
	}
}

void KeyWordsStyleDialog::setKeywords2List(int id)
{
	switch (id)
	{
		case IDC_KEYWORD1_EDIT :
		case IDC_KEYWORD2_EDIT :
		case IDC_KEYWORD3_EDIT :
		case IDC_KEYWORD4_EDIT :
		case IDC_KEYWORD5_EDIT :
		case IDC_KEYWORD6_EDIT :
		case IDC_KEYWORD7_EDIT :
		case IDC_KEYWORD8_EDIT :
		{
			const size_t len = getTextIntoBuffer(id);
			_pUserLang->_keywordLists[globalMappper().dialogMapper[id]] = (len > 0) ? wstring2string(_kwlBuffer).c_str() : "";
			break;
		}
	}
}

void KeyWordsStyleDialog::updateDlg()
{
	setItemText(IDC_KEYWORD1_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_KEYWORDS1]));
	setItemText(IDC_KEYWORD2_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_KEYWORDS2]));
	setItemText(IDC_KEYWORD3_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_KEYWORDS3]));
	setItemText(IDC_KEYWORD4_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_KEYWORDS4]));
	setItemText(IDC_KEYWORD5_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_KEYWORDS5]));
	setItemText(IDC_KEYWORD6_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_KEYWORDS6]));
	setItemText(IDC_KEYWORD7_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_KEYWORDS7]));
	setItemText(IDC_KEYWORD8_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_KEYWORDS8]));

	setChecked(IDC_KEYWORD1_PREFIX_CHECK, _pUserLang->_isPrefix[0]);
	setChecked(IDC_KEYWORD2_PREFIX_CHECK, _pUserLang->_isPrefix[1]);
	setChecked(IDC_KEYWORD3_PREFIX_CHECK, _pUserLang->_isPrefix[2]);
	setChecked(IDC_KEYWORD4_PREFIX_CHECK, _pUserLang->_isPrefix[3]);
	setChecked(IDC_KEYWORD5_PREFIX_CHECK, _pUserLang->_isPrefix[4]);
	setChecked(IDC_KEYWORD6_PREFIX_CHECK, _pUserLang->_isPrefix[5]);
	setChecked(IDC_KEYWORD7_PREFIX_CHECK, _pUserLang->_isPrefix[6]);
	setChecked(IDC_KEYWORD8_PREFIX_CHECK, _pUserLang->_isPrefix[7]);
}

// ============================================================================
// CommentStyleDialog
// ============================================================================

CommentStyleDialog::CommentStyleDialog(QWidget* parent)
	: SharedParametersDialog(parent)
{
	buildFrom(IDD_COMMENT_STYLE_DLG_def);
}

void CommentStyleDialog::showEvent(QShowEvent* event)
{
	// upstream WM_ACTIVATE / WM_SHOWWINDOW
	setChecked(IDC_ALLOW_ANYWHERE, _pUserLang->_forcePureLC == PURE_LC_NONE);
	setChecked(IDC_FORCE_AT_BOL, _pUserLang->_forcePureLC == PURE_LC_BOL);
	setChecked(IDC_ALLOW_WHITESPACE, _pUserLang->_forcePureLC == PURE_LC_WSP);

	setChecked(IDC_DOT_RADIO, _pUserLang->_decimalSeparator == DECSEP_DOT);
	setChecked(IDC_COMMA_RADIO, _pUserLang->_decimalSeparator == DECSEP_COMMA);
	setChecked(IDC_BOTH_RADIO, _pUserLang->_decimalSeparator == DECSEP_BOTH);

	SharedParametersDialog::showEvent(event);
}

void CommentStyleDialog::onCommand(int id, int notification)
{
	if (notification != RcNotifyClicked)
	{
		SharedParametersDialog::onCommand(id, notification);
		return;
	}

	switch (id)
	{
		case IDC_FOLDING_OF_COMMENTS :
			setPropertyByCheck(id, _pUserLang->_allowFoldOfComments);
			return;

		case IDC_ALLOW_ANYWHERE :
		case IDC_FORCE_AT_BOL :
		case IDC_ALLOW_WHITESPACE :
		{
			if (isCheckedOrNot(IDC_ALLOW_ANYWHERE))
				_pUserLang->_forcePureLC = PURE_LC_NONE;
			else if (isCheckedOrNot(IDC_FORCE_AT_BOL))
				_pUserLang->_forcePureLC = PURE_LC_BOL;
			else if (isCheckedOrNot(IDC_ALLOW_WHITESPACE))
				_pUserLang->_forcePureLC = PURE_LC_WSP;

			if (_pScintilla->getCurrentBuffer()->getLangType() == L_USER)
				_pScintilla->styleChange();
			return;
		}
		case IDC_DOT_RADIO :
		case IDC_COMMA_RADIO :
		case IDC_BOTH_RADIO :
		{
			if (isCheckedOrNot(IDC_DOT_RADIO))
				_pUserLang->_decimalSeparator = DECSEP_DOT;
			else if (isCheckedOrNot(IDC_COMMA_RADIO))
				_pUserLang->_decimalSeparator = DECSEP_COMMA;
			else if (isCheckedOrNot(IDC_BOTH_RADIO))
				_pUserLang->_decimalSeparator = DECSEP_BOTH;

			if (_pScintilla->getCurrentBuffer()->getLangType() == L_USER)
				_pScintilla->styleChange();
			return;
		}
		case IDC_COMMENTLINE_STYLER :
			openStyler(SCE_USER_STYLE_COMMENTLINE);
			return;
		case IDC_COMMENT_STYLER :
			openStyler(SCE_USER_STYLE_COMMENT);
			return;
		case IDC_NUMBER_STYLER :
			openStyler(SCE_USER_STYLE_NUMBER, SCE_USER_MASK_NESTING_NONE);
			return;
		default :
			SharedParametersDialog::onCommand(id, notification);
	}
}

void CommentStyleDialog::setKeywords2List(int id)
{
	int index = 0;
	switch (id)
	{
		case IDC_NUMBER_PREFIX1_EDIT :
		case IDC_NUMBER_PREFIX2_EDIT :
		case IDC_NUMBER_EXTRAS1_EDIT :
		case IDC_NUMBER_EXTRAS2_EDIT :
		case IDC_NUMBER_SUFFIX1_EDIT :
		case IDC_NUMBER_SUFFIX2_EDIT :
		case IDC_NUMBER_RANGE_EDIT :
		{
			const size_t len = getTextIntoBuffer(id);
			_pUserLang->_keywordLists[globalMappper().dialogMapper[id]] = (len > 0) ? wstring2string(_kwlBuffer).c_str() : "";
			break;
		}

		case IDC_COMMENT_OPEN_EDIT :
		case IDC_COMMENT_CLOSE_EDIT :
		case IDC_COMMENTLINE_OPEN_EDIT :
		case IDC_COMMENTLINE_CONTINUE_EDIT :
		case IDC_COMMENTLINE_CLOSE_EDIT :
			index = SCE_USER_KWLIST_COMMENTS;
			break;
		default : index = -1;
	}
	if (index != -1)
	{
		auto newList = std::wstring(max_char, L'\0');

		static constexpr int list[]{
			IDC_COMMENTLINE_OPEN_EDIT,
			IDC_COMMENTLINE_CONTINUE_EDIT,
			IDC_COMMENTLINE_CLOSE_EDIT,
			IDC_COMMENT_OPEN_EDIT,
			IDC_COMMENT_CLOSE_EDIT
		};

		wchar_t intBuffer[10] = { '0', 0 };
		for (int i = 0; static_cast<size_t>(i) < sizeof(list) / sizeof(int); ++i)
		{
			itow(i, intBuffer + 1);
			getTextIntoBuffer(list[i]);
			convertTo(newList.data(), _kwlBuffer.c_str(), intBuffer);
		}

		_pUserLang->_keywordLists[index] = wstring2string(newList).c_str();
	}
}

void CommentStyleDialog::retrieve(wchar_t* dest, size_t destMaxLen, const wchar_t* toRetrieve, const wchar_t* prefix)
{
	size_t j = 0;
	bool begin2Copy = false;
	bool inGroup = false;

	for (size_t i = 0, len = std::wcslen(toRetrieve); i < len; ++i)
	{
		if ((i == 0 || (toRetrieve[i - 1] == ' ')) && (toRetrieve[i] == prefix[0] && toRetrieve[i + 1] == prefix[1]))
		{
			if (j > 0 && j < destMaxLen)
				dest[j++] = ' ';

			begin2Copy = true;
			++i;
			continue;
		}

		if (toRetrieve[i] == '(' && toRetrieve[i + 1] == '(' && !inGroup && begin2Copy)
		{
			inGroup = true;
		}

		if (i >= 2 && toRetrieve[i] != ')' && toRetrieve[i - 1] == ')' && toRetrieve[i - 2] == ')' && inGroup) // NPLX: i >= 2
		{
			inGroup = false;
		}

		if (toRetrieve[i] == ' ' && begin2Copy)
		{
			begin2Copy = false;
		}

		if (j < destMaxLen && (begin2Copy || inGroup))
			dest[j++] = toRetrieve[i];
	}

	if (j < destMaxLen)
		dest[j++] = '\0';
}

void CommentStyleDialog::updateDlg()
{
	static constexpr int list[]{
		IDC_COMMENTLINE_OPEN_EDIT,
		IDC_COMMENTLINE_CONTINUE_EDIT,
		IDC_COMMENTLINE_CLOSE_EDIT,
		IDC_COMMENT_OPEN_EDIT,
		IDC_COMMENT_CLOSE_EDIT
	};

	wchar_t intBuffer[10] = { '0', 0 };
	for (int i = 0; static_cast<size_t>(i) < sizeof(list) / sizeof(int); ++i)
	{
		itow(i, intBuffer + 1);
		retrieve(_kwlBuffer.data(), _kwlBuffer.size(), string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_COMMENTS]).c_str(), intBuffer);
		setItemText(list[i], _kwlBuffer.c_str());
	}

	setChecked(IDC_FOLDING_OF_COMMENTS, _pUserLang->_allowFoldOfComments);

	setChecked(IDC_ALLOW_ANYWHERE, _pUserLang->_forcePureLC == PURE_LC_NONE);
	setChecked(IDC_FORCE_AT_BOL, _pUserLang->_forcePureLC == PURE_LC_BOL);
	setChecked(IDC_ALLOW_WHITESPACE, _pUserLang->_forcePureLC == PURE_LC_WSP);

	setChecked(IDC_DOT_RADIO, _pUserLang->_decimalSeparator == DECSEP_DOT);
	setChecked(IDC_COMMA_RADIO, _pUserLang->_decimalSeparator == DECSEP_COMMA);
	setChecked(IDC_BOTH_RADIO, _pUserLang->_decimalSeparator == DECSEP_BOTH);

	setItemText(IDC_NUMBER_PREFIX1_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_NUMBER_PREFIX1]));
	setItemText(IDC_NUMBER_PREFIX2_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_NUMBER_PREFIX2]));
	setItemText(IDC_NUMBER_EXTRAS1_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_NUMBER_EXTRAS1]));
	setItemText(IDC_NUMBER_EXTRAS2_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_NUMBER_EXTRAS2]));
	setItemText(IDC_NUMBER_SUFFIX1_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_NUMBER_SUFFIX1]));
	setItemText(IDC_NUMBER_SUFFIX2_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_NUMBER_SUFFIX2]));
	setItemText(IDC_NUMBER_RANGE_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_NUMBER_RANGE]));
}

// ============================================================================
// SymbolsStyleDialog
// ============================================================================

namespace
{
	constexpr int delimiterEdits[]{
		IDC_DELIMITER1_BOUNDARYOPEN_EDIT,
		IDC_DELIMITER1_ESCAPE_EDIT,
		IDC_DELIMITER1_BOUNDARYCLOSE_EDIT,
		IDC_DELIMITER2_BOUNDARYOPEN_EDIT,
		IDC_DELIMITER2_ESCAPE_EDIT,
		IDC_DELIMITER2_BOUNDARYCLOSE_EDIT,
		IDC_DELIMITER3_BOUNDARYOPEN_EDIT,
		IDC_DELIMITER3_ESCAPE_EDIT,
		IDC_DELIMITER3_BOUNDARYCLOSE_EDIT,
		IDC_DELIMITER4_BOUNDARYOPEN_EDIT,
		IDC_DELIMITER4_ESCAPE_EDIT,
		IDC_DELIMITER4_BOUNDARYCLOSE_EDIT,
		IDC_DELIMITER5_BOUNDARYOPEN_EDIT,
		IDC_DELIMITER5_ESCAPE_EDIT,
		IDC_DELIMITER5_BOUNDARYCLOSE_EDIT,
		IDC_DELIMITER6_BOUNDARYOPEN_EDIT,
		IDC_DELIMITER6_ESCAPE_EDIT,
		IDC_DELIMITER6_BOUNDARYCLOSE_EDIT,
		IDC_DELIMITER7_BOUNDARYOPEN_EDIT,
		IDC_DELIMITER7_ESCAPE_EDIT,
		IDC_DELIMITER7_BOUNDARYCLOSE_EDIT,
		IDC_DELIMITER8_BOUNDARYOPEN_EDIT,
		IDC_DELIMITER8_ESCAPE_EDIT,
		IDC_DELIMITER8_BOUNDARYCLOSE_EDIT
	};
}

SymbolsStyleDialog::SymbolsStyleDialog(QWidget* parent)
	: SharedParametersDialog(parent)
{
	buildFrom(IDD_SYMBOL_STYLE_DLG_def);
}

void SymbolsStyleDialog::updateDlg()
{
	const auto& list = delimiterEdits;
	wchar_t intBuffer[10] = {'0', 0};

	for (int i = 0; static_cast<size_t>(i) < sizeof(list)/sizeof(int); ++i)
	{
		if (i < 10)
			itow(i, intBuffer + 1);
		else
			itow(i, intBuffer);

		retrieve(_kwlBuffer.data(), _kwlBuffer.size(), string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_DELIMITERS]).c_str(), intBuffer);
		setItemText(list[i], _kwlBuffer.c_str());
	}

	setItemText(IDC_OPERATOR1_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_OPERATORS1]));
	setItemText(IDC_OPERATOR2_EDIT, string2wstring(_pUserLang->_keywordLists[SCE_USER_KWLIST_OPERATORS2]));
}

void SymbolsStyleDialog::onCommand(int id, int notification)
{
	if (notification != RcNotifyClicked)
	{
		SharedParametersDialog::onCommand(id, notification);
		return;
	}

	switch (id)
	{
		case IDC_OPERATOR_STYLER :
			openStyler(SCE_USER_STYLE_OPERATOR, SCE_USER_MASK_NESTING_NONE);
			return;
		case IDC_DELIMITER1_STYLER :
			openStyler(SCE_USER_STYLE_DELIMITER1);
			return;
		case IDC_DELIMITER2_STYLER :
			openStyler(SCE_USER_STYLE_DELIMITER2);
			return;
		case IDC_DELIMITER3_STYLER :
			openStyler(SCE_USER_STYLE_DELIMITER3);
			return;
		case IDC_DELIMITER4_STYLER :
			openStyler(SCE_USER_STYLE_DELIMITER4);
			return;
		case IDC_DELIMITER5_STYLER :
			openStyler(SCE_USER_STYLE_DELIMITER5);
			return;
		case IDC_DELIMITER6_STYLER :
			openStyler(SCE_USER_STYLE_DELIMITER6);
			return;
		case IDC_DELIMITER7_STYLER :
			openStyler(SCE_USER_STYLE_DELIMITER7);
			return;
		case IDC_DELIMITER8_STYLER :
			openStyler(SCE_USER_STYLE_DELIMITER8);
			return;
		default :
			SharedParametersDialog::onCommand(id, notification);
	}
}

void SymbolsStyleDialog::retrieve(wchar_t* dest, size_t destMaxLen, const wchar_t* toRetrieve, const wchar_t* prefix)
{
	size_t j = 0;
	bool begin2Copy = false;
	bool inGroup = false;

	for (size_t i = 0, len = std::wcslen(toRetrieve); i < len; ++i)
	{
		if ((i == 0 || (toRetrieve[i-1] == ' ')) && (toRetrieve[i] == prefix[0] && toRetrieve[i+1] == prefix[1]))
		{
			if (j > 0 && j < destMaxLen)
				dest[j++] = ' ';

			begin2Copy = true;
			++i;
			continue;
		}

		if (toRetrieve[i] == '(' && toRetrieve[i + 1] == '(' && !inGroup && begin2Copy)
		{
			inGroup = true;
		}

		if (i >= 2 && toRetrieve[i] != ')' && toRetrieve[i - 1] == ')' && toRetrieve[i - 2] == ')' && inGroup) // NPLX: i >= 2
		{
			inGroup = false;
		}

		if (toRetrieve[i] == ' ' && begin2Copy)
		{
			begin2Copy = false;
		}

		if (j < destMaxLen && (begin2Copy || inGroup))
			dest[j++] = toRetrieve[i];
	}
	if (j < destMaxLen)
		dest[j++] = '\0';
}

void SymbolsStyleDialog::setKeywords2List(int id)
{
	switch (id)
	{
		case IDC_OPERATOR1_EDIT:
		{
			const size_t len = getTextIntoBuffer(id);
			_pUserLang->_keywordLists[SCE_USER_KWLIST_OPERATORS1] = (len > 0) ? wstring2string(_kwlBuffer).c_str() : "";
			break;
		}

		case IDC_OPERATOR2_EDIT:
		{
			const size_t len = getTextIntoBuffer(id);
			_pUserLang->_keywordLists[SCE_USER_KWLIST_OPERATORS2] = (len > 0) ? wstring2string(_kwlBuffer).c_str() : "";
			break;
		}

		default :
		{
			if (std::find(std::begin(delimiterEdits), std::end(delimiterEdits), id) == std::end(delimiterEdits))
				break;

			auto newList = std::wstring(max_char, L'\0');
			wchar_t intBuffer[10] = {'0', 0};
			const auto& list = delimiterEdits;

			for (int i = 0; static_cast<size_t>(i) < sizeof(list)/sizeof(int); ++i)
			{
				if (i < 10)
					itow(i, intBuffer+1);
				else
					itow(i, intBuffer);

				int dd = list[i];
				getTextIntoBuffer(dd);
				convertTo(newList.data(), _kwlBuffer.c_str(), intBuffer);
			}

			_pUserLang->_keywordLists[SCE_USER_KWLIST_DELIMITERS] = wstring2string(newList).c_str();
			break;
		}
	}
}

// ============================================================================
// UserDefineDialog
// ============================================================================

UserDefineDialog::UserDefineDialog(QWidget* parent)
	: SharedParametersDialog(parent, false), _pCurrentUserLang(std::make_unique<UserLangContainer>())
{
	static constexpr int styles[] = {
		SCE_USER_STYLE_DEFAULT, SCE_USER_STYLE_COMMENT, SCE_USER_STYLE_COMMENTLINE, SCE_USER_STYLE_NUMBER,
		SCE_USER_STYLE_KEYWORD1, SCE_USER_STYLE_KEYWORD2, SCE_USER_STYLE_KEYWORD3, SCE_USER_STYLE_KEYWORD4,
		SCE_USER_STYLE_KEYWORD5, SCE_USER_STYLE_KEYWORD6, SCE_USER_STYLE_KEYWORD7, SCE_USER_STYLE_KEYWORD8,
		SCE_USER_STYLE_OPERATOR, SCE_USER_STYLE_FOLDER_IN_CODE1, SCE_USER_STYLE_FOLDER_IN_CODE2,
		SCE_USER_STYLE_FOLDER_IN_COMMENT, SCE_USER_STYLE_DELIMITER1, SCE_USER_STYLE_DELIMITER2,
		SCE_USER_STYLE_DELIMITER3, SCE_USER_STYLE_DELIMITER4, SCE_USER_STYLE_DELIMITER5, SCE_USER_STYLE_DELIMITER6,
		SCE_USER_STYLE_DELIMITER7, SCE_USER_STYLE_DELIMITER8
	};
	for (int style : styles)
		_pCurrentUserLang->_styles.addStyler(style, string2wstring(globalMappper().styleNameMapper[style]).c_str());
	ScintillaEditView::_pCurrentUserLang = _pCurrentUserLang.get(); // NPLX: see ScintillaEditView.h
}

UserDefineDialog::~UserDefineDialog()
{
	if (_pUserLang == _pCurrentUserLang.get())
		_pUserLang = nullptr;
	if (ScintillaEditView::_pCurrentUserLang == _pCurrentUserLang.get())
		ScintillaEditView::_pCurrentUserLang = nullptr;
}

void UserDefineDialog::doDialog(bool willBeShown, bool isRTL)
{
	if (!isCreated())
	{
		if (isRTL)
			setLayoutDirection(Qt::RightToLeft);
		create();
	}
	setVisible(willBeShown);
	if (willBeShown)
	{
		raise();
		activateWindow();
	}
}

void UserDefineDialog::reloadLangCombo()
{
	NppParameters& nppParam = NppParameters::getInstance();
	QComboBox* combo = comboBox(IDC_LANGNAME_COMBO);
	_isUpdatingControls = true;
	combo->clear();
	combo->addItem(QStringLiteral("User Defined Language"));
	for (int i = 0, nb = nppParam.getNbUserLang(); i < nb ; ++i)
	{
		const UserLangContainer* userLangContainer = nppParam.getULCFromIndex(i);
		combo->addItem(QString::fromWCharArray(userLangContainer->getName()));
	}
	_isUpdatingControls = false;
}

void UserDefineDialog::enableLangAndControlsBy(size_t index)
{
	_pUserLang = (index == 0) ? _pCurrentUserLang.get() : (NppParameters::getInstance().getULCFromIndex(index - 1));
	if (index != 0)
		setItemText(IDC_EXT_EDIT, _pUserLang->_ext);

	showItem(IDC_EXT_STATIC, index != 0);
	showItem(IDC_EXT_EDIT, index != 0);
	showItem(IDC_RENAME_BUTTON, index != 0);
	showItem(IDC_REMOVELANG_BUTTON, index != 0);
}

void UserDefineDialog::updateDlg()
{
	const int i = comboBox(IDC_LANGNAME_COMBO)->currentIndex();
	if (i > 0) // the first menu item is generic UDL
	{
		NppParameters& nppParam = NppParameters::getInstance();
		nppParam.setUdlXmlDirtyFromIndex(static_cast<size_t>(i) - 1);
	}

	setChecked(IDC_LANGNAME_IGNORECASE_CHECK, _pUserLang->_isCaseIgnored);
	_folderStyleDlg->updateDlg();
	_keyWordsStyleDlg->updateDlg();
	_commentStyleDlg->updateDlg();
	_symbolsStyleDlg->updateDlg();
}

void UserDefineDialog::create()
{
	// upstream WM_INITDIALOG
	NppParameters& nppParam = NppParameters::getInstance();

	buildFrom(IDD_GLOBAL_USERDEFINE_DLG_def);
	_pUserLang = _pCurrentUserLang.get();

	// upstream ControlsTab with the 4 child dialogs, below the Import button
	_folderStyleDlg = new FolderStyleDialog(this);
	_keyWordsStyleDlg = new KeyWordsStyleDialog(this);
	_commentStyleDlg = new CommentStyleDialog(this);
	_symbolsStyleDlg = new SymbolsStyleDialog(this);
	SharedParametersDialog* pages[] = { _folderStyleDlg, _keyWordsStyleDlg, _commentStyleDlg, _symbolsStyleDlg };

	_ctrlTab = new QTabBar(this);
	_ctrlTab->setDrawBase(true);
	// same strings as upstream: "&&" shows "&" (QTabBar reads mnemonics like the Win32 tab control)
	_ctrlTab->addTab(QStringLiteral("Folder && Default"));
	_ctrlTab->addTab(QStringLiteral("Keywords Lists"));
	_ctrlTab->addTab(QStringLiteral("Comment && Number"));
	_ctrlTab->addTab(QStringLiteral("Operators && Delimiters"));

	const QRect importRect = dlgItem(IDC_IMPORT_BUTTON)->geometry();
	const int padding = 6;
	const int tabTop = importRect.bottom() + padding;
	_ctrlTab->move(padding, tabTop);
	_ctrlTab->resize(width() - 2 * padding, _ctrlTab->sizeHint().height());
	const int pageTop = tabTop + _ctrlTab->height();
	for (SharedParametersDialog* page : pages)
	{
		page->move(padding, pageTop);
		page->hide();
	}
	// the dialog grows to hold the pages (the .rc page size is larger than the space left in the .rc dialog
	// once Qt's tab bar and fonts are counted)
	setFixedSize(std::max(width(), _folderStyleDlg->width() + 2 * padding), pageTop + _folderStyleDlg->height() + padding);
	_ctrlTab->resize(width() - 2 * padding, _ctrlTab->height());
	_folderStyleDlg->show();

	connect(_ctrlTab, &QTabBar::currentChanged, this, [pages](int index) {
		// upstream TCN_SELCHANGE → ControlsTab::clickedUpdate
		for (int i = 0; i < 4; ++i)
			pages[i]->setVisible(i == index);
	});

	reloadLangCombo();
	{
		QSignalBlocker blocker(comboBox(IDC_LANGNAME_COMBO));
		comboBox(IDC_LANGNAME_COMBO)->setCurrentIndex(0);
	}

	enableLangAndControlsBy(0);

	if (nppParam.isTransparentAvailable())
	{
		showItem(IDC_UD_TRANSPARENT_CHECK, true);
		showItem(IDC_UD_PERCENTAGE_SLIDER, true);

		slider(IDC_UD_PERCENTAGE_SLIDER)->setRange(20, 200);
		slider(IDC_UD_PERCENTAGE_SLIDER)->setValue(150);
		if (!isCheckedOrNot(IDC_UD_TRANSPARENT_CHECK))
			enableItem(IDC_UD_PERCENTAGE_SLIDER, false);
	}

	wstring udlVersion = L"User Defined Language v.";
	udlVersion += std::to_wstring(SCE_UDL_VERSION_MAJOR);
	udlVersion += L".";
	udlVersion += std::to_wstring(SCE_UDL_VERSION_MINOR);
	udlVersion += L".";
	udlVersion += std::to_wstring(SCE_UDL_VERSION_BUILD);
	udlVersion += L".";
	udlVersion += std::to_wstring(SCE_UDL_VERSION_REVISION);

	setWindowTitle(QString::fromStdWString(udlVersion));

	updateDlg();
}

void UserDefineDialog::changeStyle()
{
	_status = !_status;

	NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	NppXml::Element targetNode{};
	if (pNativeSpeaker && pNativeSpeaker->getNativeLang())
	{
		targetNode = NppXml::firstChildElement(pNativeSpeaker->getNativeLang(), "Dialog");
		if (targetNode)
			targetNode = NppXml::firstChildElement(targetNode, "UserDefine");
	}

	wstring defauleLabel;
	string idStr;
	if (_status == DOCK)
	{
		defauleLabel = L"Undock";
		idStr = std::to_string(IDC_UNDOCK_BUTTON);
	}
	else
	{
		defauleLabel = L"Dock";
		idStr = std::to_string(IDC_DOCK_BUTTON);
	}

	const wstring dockButtonLabel = targetNode ? NativeLangSpeaker::getAttrNameByIdStr(defauleLabel.c_str(), targetNode, idStr.c_str()) : defauleLabel;
	setItemText(IDC_DOCK_BUTTON, dockButtonLabel);
	// NPLX: a window, or a page in the main window (upstream WS_POPUP / WS_CHILD styles) - done by Notepad_plus
}

void UserDefineDialog::onSliderChanged(int id, int value)
{
	// upstream WM_HSCROLL
	if (id == IDC_UD_PERCENTAGE_SLIDER)
		setWindowOpacity(1.0 - value / 255.0); // NPLX: was nppParam.setTransparent
}

bool UserDefineDialog::askName(const std::wstring& title, const std::wstring& label, const std::wstring& initial, std::wstring& result)
{
	if (_hasNextNameAnswer)
	{
		_hasNextNameAnswer = false;
		result = _nextNameAnswer;
		return !result.empty();
	}
	StringDlg strDlg(this);
	strDlg.init(title.c_str(), label.c_str(), initial.c_str(), langNameLenMax - 1);
	return strDlg.doDialog(result);
}

void UserDefineDialog::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NativeLangSpeaker * pNativeSpeaker = nppParam.getNativeLangSpeaker();

	if (notification == RcNotifyEditChange)
	{
		// upstream: EN_CHANGE of IDC_EXT_EDIT
		_pUserLang->_ext = getItemText(IDC_EXT_EDIT).substr(0, extsLenMax - 1);
		return;
	}
	else if (notification == RcNotifySelChange)
	{
		switch (id)
		{
			case IDC_LANGNAME_COMBO:
			{
				auto i = comboBox(IDC_LANGNAME_COMBO)->currentIndex();
				enableLangAndControlsBy(i);
				updateDlg();
				return;
			}

			default:
				return;
		}
	}
	else if (notification != RcNotifyClicked)
		return;

	switch (id)
	{
		case IDC_DOCK_BUTTON :
		{
			const bool toDock = (_status == UNDOCK);

			if (toDock)
			{
				if (nppParam.isTransparentAvailable())
				{
					setWindowOpacity(1.0); // NPLX: was nppParam.removeTransparent
					showItem(IDC_UD_TRANSPARENT_CHECK, false);
					showItem(IDC_UD_PERCENTAGE_SLIDER, false);
				}
			}

			changeStyle();

			if (_status == UNDOCK)
			{
				if (nppParam.isTransparentAvailable())
				{
					bool isChecked = isCheckedOrNot(IDC_UD_TRANSPARENT_CHECK);
					showItem(IDC_UD_TRANSPARENT_CHECK, true);
					showItem(IDC_UD_PERCENTAGE_SLIDER, true);
					if (isChecked)
					{
						const int percent = slider(IDC_UD_PERCENTAGE_SLIDER)->value();
						setWindowOpacity(1.0 - percent / 255.0); // NPLX: was nppParam.setTransparent (applied once a window again)
					}
				}
			}

			// upstream ::SendMessage(_hParent, WM_DOCK_USERDEFINE_DLG / WM_UNDOCK_USERDEFINE_DLG, 0, 0)
			if (toDock && _menuHandler._dock)
				_menuHandler._dock();
			else if (!toDock && _menuHandler._undock)
				_menuHandler._undock();
			return;
		}

		case IDCANCEL:
		{
			hide();
			if (_menuHandler._closed)
				_menuHandler._closed(); // upstream ::SendMessage(_hParent, WM_CLOSE_USERDEFINE_DLG, 0, 0)
			return;
		}

		case IDC_REMOVELANG_BUTTON :
		{
			int result = nppParam.getNativeLangSpeaker()->messageBox("UDLRemoveCurrentLang",
				L"Are you sure?",
				L"Remove the current language",
				MB_YESNO);

			if (result == IDYES)
			{
				auto i = comboBox(IDC_LANGNAME_COMBO)->currentIndex();
				const wstring langName = comboBox(IDC_LANGNAME_COMBO)->itemText(i).toStdWString();
				if (langName.size() > 256)
					return;

				//remove current language from combobox
				_isUpdatingControls = true;
				comboBox(IDC_LANGNAME_COMBO)->removeItem(i);
				comboBox(IDC_LANGNAME_COMBO)->setCurrentIndex(i - 1);
				_isUpdatingControls = false;
				onCommand(IDC_LANGNAME_COMBO, RcNotifySelChange);

				//remove current language from userLangArray
				nppParam.removeUserLang(i-1);

				//remove current language from langMenu
				if (_menuHandler._remove)
					_menuHandler._remove(IDM_LANG_USER + i);
				if (_menuHandler._removed)
					_menuHandler._removed(langName.c_str());
			}
			return;
		}
		case IDC_RENAME_BUTTON :
		{
			auto i = comboBox(IDC_LANGNAME_COMBO)->currentIndex();
			const wstring langName = comboBox(IDC_LANGNAME_COMBO)->itemText(i).toStdWString();
			if (langName.size() > 256)
				return;

			wstring strName = pNativeSpeaker->getLocalizedStrFromID("common-name", L"Name");
			wstring strTitle = pNativeSpeaker->getLocalizedStrFromID("userdefined-title-rename", L"Rename Current Language Name");

			wstring newNameStr;
			if (askName(strTitle, strName, langName, newNameStr))
			{
				const wchar_t* newName = newNameStr.c_str();
				if (nppParam.isExistingUserLangName(newName))
				{
					nppParam.getNativeLangSpeaker()->messageBox("UDLNewNameError",
						L"This name is used by another language,\rplease give another one.",
						L"UDL Error",
						MB_OK);
					// NPLX: upstream posts IDC_RENAME_BUTTON again; here the user clicks Rename again
					return;
				}
				if (nppParam.getNbUserLang() >= NB_MAX_USER_LANG)
					return;

				//rename current language name in combobox
				_isUpdatingControls = true;
				comboBox(IDC_LANGNAME_COMBO)->setItemText(i, QString::fromStdWString(newNameStr));
				comboBox(IDC_LANGNAME_COMBO)->setCurrentIndex(i);
				_isUpdatingControls = false;

				//rename current language name in userLangArray
				UserLangContainer* userLangContainer = nppParam.getULCFromIndex(i - 1);
				userLangContainer->_name = newName;

				//rename current language name in langMenu
				if (_menuHandler._rename)
					_menuHandler._rename(IDM_LANG_USER + i, newName);
				if (_menuHandler._renamed)
					_menuHandler._renamed(newName, langName.c_str());
			}

			return;
		}

		case IDC_ADDNEW_BUTTON :
		case IDC_SAVEAS_BUTTON :
		{
			auto i = comboBox(IDC_LANGNAME_COMBO)->currentIndex();
			if (i == 0)
				id = IDC_ADDNEW_BUTTON;


			wstring strName = pNativeSpeaker->getLocalizedStrFromID("common-name", L"Name");
			wstring strTitle = (id == IDC_SAVEAS_BUTTON) ?
				pNativeSpeaker->getLocalizedStrFromID("userdefined-title-save", L"Save Current Language Name As...") :
				pNativeSpeaker->getLocalizedStrFromID("userdefined-title-new", L"Create New Language...");

			wstring newNameString;
			if (askName(strTitle, strName, L"", newNameString) && !newNameString.empty())
			{
				const wchar_t *newName = newNameString.c_str();

				if (nppParam.isExistingUserLangName(newName))
				{
					pNativeSpeaker->messageBox("UDLNewNameError",
						L"This name is used by another language,\rplease give another one.",
						L"UDL Error",
						MB_OK);
					return;
				}
				if (nppParam.getNbUserLang() >= NB_MAX_USER_LANG)
					return;

				//add current language in userLangArray at the end as a new lang
				const UserLangContainer* userLang = (id == IDC_SAVEAS_BUTTON) ? nppParam.getULCFromIndex(i - 1) : _pCurrentUserLang.get();
				int newIndex = nppParam.addUserLangToEnd(userLang, newName);

				//add new language name in combobox
				_isUpdatingControls = true;
				comboBox(IDC_LANGNAME_COMBO)->addItem(QString::fromStdWString(newNameString));
				comboBox(IDC_LANGNAME_COMBO)->setCurrentIndex(newIndex + 1);
				_isUpdatingControls = false;
				onCommand(IDC_LANGNAME_COMBO, RcNotifySelChange);

				//add new language name in langMenu
				if (_menuHandler._insert)
					_menuHandler._insert(IDM_LANG_USER + newIndex + 1, newName);
			}

			return;
		}

		case IDC_IMPORT_BUTTON :
		{
			// NPLX: QFileDialog in place of CustomFileDialog
			const QString file = QFileDialog::getOpenFileName(this, QString(), QString(), QStringLiteral("UDL (*.xml)"));
			wstring sourceFile = file.toStdWString();
			if (sourceFile.empty()) break;

			auto udlNumbersBeforeAfter = nppParam.importUDLFromFile(sourceFile);
			if (udlNumbersBeforeAfter.second - udlNumbersBeforeAfter.first != 0)
			{
				auto i = comboBox(IDC_LANGNAME_COMBO)->currentIndex();
				reloadLangCombo();
				{
					QSignalBlocker blocker(comboBox(IDC_LANGNAME_COMBO));
					comboBox(IDC_LANGNAME_COMBO)->setCurrentIndex(i);
				}

				pNativeSpeaker->messageBox("UDL_importSuccessful",
					L"Import successful.",
					L"User Defined Language",
					MB_OK | MB_APPLMODAL,
					0);

				// Update language menu
				for (auto j = udlNumbersBeforeAfter.first + 1; j <= udlNumbersBeforeAfter.second; ++j)
				{
					const wchar_t* newName = nppParam.getULCFromIndex(j - 1)->getName();
					if (_menuHandler._insert)
						_menuHandler._insert(IDM_LANG_USER + static_cast<int>(j), newName);
				}
			}
			else
			{
				pNativeSpeaker->messageBox("UDL_importFails",
					L"Failed to import.",
					L"User Defined Language",
					MB_OK | MB_APPLMODAL | MB_ICONEXCLAMATION,
					0);
			}
			break;
		}

		case IDC_EXPORT_BUTTON :
		{
			auto i2Export = comboBox(IDC_LANGNAME_COMBO)->currentIndex();
			if (i2Export == 0)
			{
				// maybe a better option would be to simply send IDC_SAVEAS_BUTTON message, and display "Save As..." dialog?
				pNativeSpeaker->messageBox("UDL_saveBeforeImport",
					L"Before exporting, save your language definition by clicking \"Save As...\" button",
					L"User Defined Language",
					MB_OK | MB_APPLMODAL,
					0);
				break;
			}

			// NPLX: QFileDialog in place of CustomFileDialog (".xml" added like the upstream filter index)
			QString file = QFileDialog::getSaveFileName(this, QString(), QString(), QStringLiteral("UDL (*.xml)"));
			if (!file.isEmpty() && !file.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive))
				file += QStringLiteral(".xml");
			wstring fileName2save = file.toStdWString();
			if (fileName2save.empty()) break;

			if (i2Export > 0)
			{
				bool isSuccessful = nppParam.exportUDLToFile(i2Export - 1, fileName2save);
				if (isSuccessful)
				{
					pNativeSpeaker->messageBox("UDL_exportSuccessful",
						L"Export successful.",
						L"User Defined Language",
						MB_OK | MB_APPLMODAL,
						0);
				}
				else
				{
					pNativeSpeaker->messageBox("UDL_exportFails",
						L"Failed to export.",
						L"User Defined Language",
						MB_OK | MB_APPLMODAL | MB_ICONEXCLAMATION,
						0);
				}
			}
			break;
		}

		case IDC_UD_TRANSPARENT_CHECK :
		{
			bool isChecked = isCheckedOrNot(IDC_UD_TRANSPARENT_CHECK);
			if (isChecked)
			{
				const int percent = slider(IDC_UD_PERCENTAGE_SLIDER)->value();
				setWindowOpacity(1.0 - percent / 255.0); // NPLX: was nppParam.setTransparent
			}
			else
				setWindowOpacity(1.0); // NPLX: was nppParam.removeTransparent

			enableItem(IDC_UD_PERCENTAGE_SLIDER, isChecked);
			return;
		}

		case IDC_LANGNAME_IGNORECASE_CHECK :
			setPropertyByCheck(id, _pUserLang->_isCaseIgnored);
			return;

		default :
			break;
	}
}

// ============================================================================
// StringDlg
// ============================================================================

StringDlg::StringDlg(QWidget* parent)
	: RcDialog(parent)
{
	buildFrom(IDD_STRING_DLG_def);
	setModal(true);
}

void StringDlg::init(const wchar_t *title, const wchar_t *staticName, const wchar_t *text2Set, int txtLen, const wchar_t* restrictedChars, bool bGotoCenter)
{
	_title = title;
	_static = staticName;
	_textValue = text2Set;
	_txtLen = txtLen;
	_shouldGotoCenter = bGotoCenter;
	if (restrictedChars && wcslen(restrictedChars))
	{
		_restrictedChars = restrictedChars;
	}
}

bool StringDlg::doDialog(std::wstring& result)
{
	// upstream WM_INITDIALOG
	setWindowTitle(QString::fromStdWString(_title));
	setItemText(IDC_STRING_STATIC, _static);
	setItemText(IDC_STRING_EDIT, _textValue);
	auto* edit = qobject_cast<QLineEdit*>(dlgItem(IDC_STRING_EDIT));
	if (_txtLen)
		edit->setMaxLength(_txtLen);
	if (!_restrictedChars.empty())
	{
		// upstream CustomEditProc: refuse restricted characters (typed or pasted)
		const QString restricted = QRegularExpression::escape(QString::fromStdWString(_restrictedChars));
		edit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[^%1]*").arg(restricted)), edit));
	}

	// localization for OK and Cancel
	NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	if (pNativeSpeaker)
	{
		wstring ok = pNativeSpeaker->getLocalizedStrFromID("common-ok", L"OK");
		wstring cancel = pNativeSpeaker->getLocalizedStrFromID("common-cancel", L"Cancel");

		setItemText(IDOK, ok);
		setItemText(IDCANCEL, cancel);
	}
	edit->setFocus();
	edit->selectAll();

	if (exec() != QDialog::Accepted)
		return false;
	result = _textValue;
	return true;
}

void StringDlg::onCommand(int id, int /*notification*/)
{
	switch (id)
	{
		case IDOK :
		{
			_textValue = getItemText(IDC_STRING_EDIT).substr(0, langNameLenMax - 1);
			accept();
			return;
		}

		case IDCANCEL :
			QDialog::reject();
			return;

		default:
			return;
	}
}

bool StringDlg::isAllowed(const wstring & txt)
{
	for (auto ch : txt)
	{
		if (std::find(_restrictedChars.cbegin(), _restrictedChars.cend(), ch) != _restrictedChars.cend())
			return false;
	}
	return true;
}

// ============================================================================
// StylerDlg
// ============================================================================

StylerDlg::StylerDlg(QWidget* parent, int stylerIndex, int enabledNesters)
	: RcDialog(parent)
	, _stylerIndex(stylerIndex), _enabledNesters(enabledNesters)
	, _initialStyle(SharedParametersDialog::_pUserLang->_styles.getStyler(stylerIndex))
{
	buildFrom(IDD_STYLER_POPUP_DLG_def);
	setModal(true);
	_pFgColour = new ColourPicker(this);
	_pBgColour = new ColourPicker(this);
	connect(_pFgColour, &ColourPicker::colourPicked, this, [this]() { colourPicked(0); });
	connect(_pBgColour, &ColourPicker::colourPicked, this, [this]() { colourPicked(0); });
}

StylerDlg::~StylerDlg() = default;

int StylerDlg::doDialog()
{
	initDialog();
	return exec() == QDialog::Accepted ? IDOK : IDCANCEL;
}

void StylerDlg::move2CtrlRight(int ctrlID, QWidget* handle2Move, int handle2MoveWidth, int handle2MoveHeight)
{
	const QRect rc = dlgItem(ctrlID)->geometry();
	handle2Move->setGeometry(rc.right() + 5, rc.top() + rc.height() / 2 - handle2MoveHeight / 2, handle2MoveWidth, handle2MoveHeight);
}

void StylerDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	// NPLX_TODO(Phase 8): pNativeLangSpeaker->changeUserDefineLangPopupDlg

	Style & style = SharedParametersDialog::_pUserLang->_styles.getStyler(_stylerIndex);

	// if it's a new styler (not yet any styles set), use the theme's default fg colors and transparent background
	if (SharedParametersDialog::_pUserLang->_isUnset)
	{
		SharedParametersDialog::_pUserLang->startAtTheme();

		// also needs to update dlg->_initialStyle, which was set wrong, above
		_initialStyle = SharedParametersDialog::_pUserLang->_styles.getStyler(_stylerIndex);
	}

	_isUpdatingControls = true;

	int sfs = (style._fontStyle == STYLE_NOT_USED) ? FONTSTYLE_NONE : style._fontStyle; // STYLE_NOT_USED=1, so STYLE_NOT_USED & FONTSTYLE_UNDERLINE is 1 (etc); we don't want underline/italic/bold for STYLE_NOT_USED
	setChecked(IDC_STYLER_CHECK_UNDERLINE, sfs & FONTSTYLE_UNDERLINE);
	setChecked(IDC_STYLER_CHECK_ITALIC, sfs & FONTSTYLE_ITALIC);
	setChecked(IDC_STYLER_CHECK_BOLD, sfs & FONTSTYLE_BOLD);

	// for the font size combo
	QComboBox* hFontSizeCombo = comboBox(IDC_STYLER_COMBO_FONT_SIZE);
	for (size_t j = 0 ; j < int(sizeof(fontSizeStrs))/(3*sizeof(wchar_t)) ; ++j)
		hFontSizeCombo->addItem(QString::fromWCharArray(fontSizeStrs[j]));

	const QString size = (style._fontSize == -1) ? QString() : QString::number(style._fontSize);
	int i = hFontSizeCombo->findText(size, Qt::MatchExactly);
	if (i != -1)
		hFontSizeCombo->setCurrentIndex(i);

	// for the font name combo
	QComboBox* hFontNameCombo = comboBox(IDC_STYLER_COMBO_FONT_NAME);
	const std::vector<wstring> & fontlist = nppParam.getFontList();
	for (size_t j = 0, len = fontlist.size() ; j < len ; ++j)
		hFontNameCombo->addItem(QString::fromStdWString(fontlist[j]));

	i = hFontNameCombo->findText(QString::fromStdWString(style._fontName), Qt::MatchExactly);
	if (i == -1)
		i = 0;
	hFontNameCombo->setCurrentIndex(i);

	if (style._fgColor == static_cast<COLORREF>(-1))
		style._fgColor = black;

	if (style._bgColor == static_cast<COLORREF>(-1))
		style._bgColor = white;

	_pFgColour->setColour(style._fgColor);
	bool isFgEnabled = (style._colorStyle & COLORSTYLE_FOREGROUND) != 0;
	_pFgColour->setEnabled(isFgEnabled);
	setChecked(IDC_STYLER_CHECK_FG_TRANSPARENT, !isFgEnabled);
	_pBgColour->setColour(style._bgColor);
	bool isBgEnabled = (style._colorStyle & COLORSTYLE_BACKGROUND) != 0;
	_pBgColour->setEnabled(isBgEnabled);
	setChecked(IDC_STYLER_CHECK_BG_TRANSPARENT, !isBgEnabled);

	const int moveSize = 25;
	move2CtrlRight(IDC_STYLER_FG_STATIC, _pFgColour, moveSize, moveSize);
	move2CtrlRight(IDC_STYLER_BG_STATIC, _pBgColour, moveSize, moveSize);

	for (const auto& [ctrlID, nestingMask] : globalMappper().nestingMapper)
	{
		setChecked(ctrlID, style._nesting & nestingMask);
		enableItem(ctrlID, _enabledNesters & nestingMask);
	}

	_isUpdatingControls = false;
}

void StylerDlg::reject()
{
	// upstream WM_CLOSE
	Style& style = SharedParametersDialog::_pUserLang->_styles.getStyler(_stylerIndex);
	style = _initialStyle;

	// show changes to user, re-color document
	if (SharedParametersDialog::_pScintilla->getCurrentBuffer()->getLangType() == L_USER)
		SharedParametersDialog::_pScintilla->styleChange();

	QDialog::reject();
}

void StylerDlg::onCommand(int id, int notification)
{
	Style & style = SharedParametersDialog::_pUserLang->_styles.getStyler(_stylerIndex);
	if (notification == RcNotifySelChange)
	{
		QComboBox* combo = comboBox(id);
		auto i = combo->currentIndex();
		if (id == IDC_STYLER_COMBO_FONT_SIZE)
		{
			if (i != 0)
			{
				const std::wstring intStr = combo->itemText(i).toStdWString();
				if (intStr.size() > 2)
					return;

				if (intStr.empty())
					style._fontSize = -1;
				else
				{
					wchar_t *finStr = nullptr;
					style._fontSize = wcstol(intStr.c_str(), &finStr, 10);
					if (*finStr != '\0')
						style._fontSize = -1;
				}
			}
			else
			{
				style._fontSize = -1;
			}
		}
		else if (id == IDC_STYLER_COMBO_FONT_NAME)
		{
			style._fontName = (i <= 0) ? std::wstring() : combo->itemText(i).toStdWString();
		}

		// show changes to user, re-color document
		if (SharedParametersDialog::_pScintilla->getCurrentBuffer()->getLangType() == L_USER)
			SharedParametersDialog::_pScintilla->styleChange();
		return;
	}
	else if (notification == RcNotifyClicked)    // #define CPN_COLOURPICKED (BN_CLICKED)
	{
		if (id == IDCANCEL)
		{
			reject();
			return;
		}

		if (id == IDOK)
		{
			accept();
			return;
		}

		colourPicked(id);
	}
}

void StylerDlg::colourPicked(int id)
{
	Style & style = SharedParametersDialog::_pUserLang->_styles.getStyler(_stylerIndex);

	style._fgColor = _pFgColour->getColour();
	style._bgColor = _pBgColour->getColour();

	if (id == IDC_STYLER_CHECK_FG_TRANSPARENT || id == IDC_STYLER_CHECK_BG_TRANSPARENT)
	{
		if (id == IDC_STYLER_CHECK_FG_TRANSPARENT)
		{
			bool isTransparent = isCheckedOrNot(IDC_STYLER_CHECK_FG_TRANSPARENT);
			_pFgColour->setEnabled(!isTransparent);
			if (isTransparent)
				style._colorStyle &= ~COLORSTYLE_FOREGROUND;
			else
				style._colorStyle |= COLORSTYLE_FOREGROUND;
		}

		if (id == IDC_STYLER_CHECK_BG_TRANSPARENT)
		{
			bool isTransparent = isCheckedOrNot(IDC_STYLER_CHECK_BG_TRANSPARENT);
			_pBgColour->setEnabled(!isTransparent);
			if (isTransparent)
				style._colorStyle &= ~COLORSTYLE_BACKGROUND;
			else
				style._colorStyle |= COLORSTYLE_BACKGROUND;
		}
	}
	else
	{
		if (_pFgColour->isEnabled())
		{
			style._colorStyle |= COLORSTYLE_FOREGROUND;
		}
		else
		{
			style._colorStyle &= ~COLORSTYLE_FOREGROUND;
		}
		setChecked(IDC_STYLER_CHECK_FG_TRANSPARENT, !_pFgColour->isEnabled());

		if (_pBgColour->isEnabled())
		{
			style._colorStyle |= COLORSTYLE_BACKGROUND;
		}
		else
		{
			style._colorStyle &= ~COLORSTYLE_BACKGROUND;
		}
		setChecked(IDC_STYLER_CHECK_BG_TRANSPARENT, !_pBgColour->isEnabled());
	}
	style._fontStyle = FONTSTYLE_NONE;
	if (isCheckedOrNot(IDC_STYLER_CHECK_BOLD))
		style._fontStyle |= FONTSTYLE_BOLD;
	if (isCheckedOrNot(IDC_STYLER_CHECK_ITALIC))
		style._fontStyle |= FONTSTYLE_ITALIC;
	if (isCheckedOrNot(IDC_STYLER_CHECK_UNDERLINE))
		style._fontStyle |= FONTSTYLE_UNDERLINE;

	style._nesting = SCE_USER_MASK_NESTING_NONE;
	for (const auto& [ctrlID, nestingMask] : globalMappper().nestingMapper)
	{
		if (isCheckedOrNot(ctrlID))
			style._nesting |= nestingMask;
	}

	// show changes to user, re-color document
	if (SharedParametersDialog::_pScintilla->getCurrentBuffer()->getLangType() == L_USER)
		SharedParametersDialog::_pScintilla->styleChange();
}
