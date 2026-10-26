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

// NPLX: port of upstream ScintillaComponent/FindReplaceDlg.cpp (see FindReplaceDlg.h).

#include "FindReplaceDlg.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QTabBar>
#include <QVBoxLayout>

#include "LocalizationQt.h"
#include "SciLexer.h"

#include "Buffer.h"
#include "NppConstants.h"
#include "NppDarkMode.h"
#include "Utf8.h"
#include "UiServices.h"
#include "localization.h"
#include "menuCmdID.h"
#include "resource.h"

using namespace std;

FindOption * FindReplaceDlg::_env;
FindOption FindReplaceDlg::_options;

namespace
{
	std::mutex findOps_mutex;

	const wstring noFoundPotentialReason = L"The given occurrence cannot be found. You may have forgotten to check \"Wrap around\" (to ON), \"Match case\" (to OFF), or \"Match whole word only\" (to OFF).";
	const wstring regexEmptyFoundReason = L"The given regular expression matches empty strings.";

	// upstream addText2Combo(txt2add, hCombo)
	void addText2Combo(const wchar_t* txt2add, QComboBox* hCombo)
	{
		if (!hCombo) return;
		if (!txt2add || !txt2add[0]) return;

		const QString text = QString::fromWCharArray(txt2add);
		int i = hCombo->findText(text, Qt::MatchExactly | Qt::MatchCaseSensitive);
		if (i != -1) // found
		{
			hCombo->removeItem(i);
		}

		hCombo->insertItem(0, text);
		hCombo->setCurrentIndex(0);
	}

	// upstream getTextFromCombo(hCombo)
	wstring getTextFromCombo(QComboBox* hCombo)
	{
		if (!hCombo)
			return wstring();
		return hCombo->currentText().left(FINDREPLACE_MAXLENGTH - 1).toStdWString();
	}
}

// ============================================================================
// Searching
// ============================================================================

int Searching::convertExtendedToString(const wchar_t * query, wchar_t * result, int length)
{	//query may equal to result, since it always gets smaller
	int i = 0, j = 0;
	int charLeft = length;

	while (i < length)
	{	//because the backslash escape quences always reduce the size of the wstring, no overflow checks have to be made for target, assuming parameters are correct
		wchar_t current = query[i];
		--charLeft;
		if (current == '\\' && charLeft)
		{	//possible escape sequence
			++i;
			--charLeft;
			current = query[i];
			switch(current)
			{
				case 'r':
					result[j] = '\r';
					break;
				case 'n':
					result[j] = '\n';
					break;
				case '0':
					result[j] = '\0';
					break;
				case 't':
					result[j] = '\t';
					break;
				case '\\':
					result[j] = '\\';
					break;
				case 'b':
				case 'd':
				case 'o':
				case 'x':
				case 'u':
				{
					int size = 0, base = 0;
					if (current == 'b')
					{	//11111111
						size = 8, base = 2;
					}
					else if (current == 'o')
					{	//377
						size = 3, base = 8;
					}
					else if (current == 'd')
					{	//255
						size = 3, base = 10;
					}
					else if (current == 'x')
					{	//0xFF
						size = 2, base = 16;
					}
					else if (current == 'u')
					{	//0xCDCD
						size = 4, base = 16;
					}

					if (charLeft >= size)
					{
						int res = 0;
						if (Searching::readBase(query+(i+1), &res, base, size))
						{
							result[j] = static_cast<wchar_t>(res);
							i += size;
							break;
						}
					}
					//not enough chars to make parameter, use default method as fallback
					[[fallthrough]];
				}

				default:
				{	//unknown sequence, treat as regular text
					result[j] = '\\';
					++j;
					result[j] = current;
					break;
				}
			}
		}
		else
		{
			result[j] = query[i];
		}
		++i;
		++j;
	}
	result[j] = 0;
	return j;
}

bool Searching::readBase(const wchar_t * str, int * value, int base, int size)
{
	int i = 0, temp = 0;
	*value = 0;
	wchar_t max = '0' + static_cast<wchar_t>(base) - 1;

	while (i < size)
	{
		wchar_t current = str[i];
		if (current >= 'A')
		{
			current &= 0xdf;
			current -= ('A' - '0' - 10);
		}
		else if (current > '9')
			return false;

		if (current >= '0' && current <= max)
		{
			temp *= base;
			temp += (current - '0');
		}
		else
		{
			return false;
		}
		++i;
	}
	*value = temp;
	return true;
}

void Searching::displaySectionCentered(size_t posStart, size_t posEnd, ScintillaEditView * pEditView, bool isDownwards)
{
	// Make sure target lines are unfolded
	pEditView->execute(SCI_ENSUREVISIBLE, pEditView->execute(SCI_LINEFROMPOSITION, posStart));
	pEditView->execute(SCI_ENSUREVISIBLE, pEditView->execute(SCI_LINEFROMPOSITION, posEnd));

	// Jump-scroll to center, if current position is out of view
	pEditView->execute(SCI_SETVISIBLEPOLICY, CARET_JUMPS | CARET_EVEN);
	pEditView->execute(SCI_ENSUREVISIBLEENFORCEPOLICY, pEditView->execute(SCI_LINEFROMPOSITION, isDownwards ? posEnd : posStart));
	// When searching up, the beginning of the (possible multiline) result is important, when scrolling down the end
	pEditView->execute(SCI_GOTOPOS, isDownwards ? posEnd : posStart);
	pEditView->execute(SCI_SETVISIBLEPOLICY, CARET_EVEN);
	pEditView->execute(SCI_ENSUREVISIBLEENFORCEPOLICY, pEditView->execute(SCI_LINEFROMPOSITION, isDownwards ? posEnd : posStart));

	// Adjust so that we see the entire match; primarily horizontally
	pEditView->execute(SCI_SCROLLRANGE, posStart, posEnd);

	// make sure won't start/end the selection in the middle of a multibyte character,
	// or in between a CR/LF pair for Windows files
	// (needed for stale search-results where user has made doc edits after the search)
	if (posStart > 0)
	{
		posStart = pEditView->execute(SCI_POSITIONBEFORE, posStart);
		posStart = pEditView->execute(SCI_POSITIONAFTER, posStart);
	}
	if (posEnd > 0)
	{
		posEnd = pEditView->execute(SCI_POSITIONBEFORE, posEnd);
		posEnd = pEditView->execute(SCI_POSITIONAFTER, posEnd);
	}

	// Move cursor to end of result and select result
	pEditView->execute(SCI_GOTOPOS, posEnd);
	pEditView->execute(SCI_SETANCHOR, posStart);

	// Update Scintilla's knowledge about what column the caret is in, so that if user
	// does up/down arrow as first navigation after the search result is selected,
	// the caret doesn't jump to an unexpected column
	pEditView->execute(SCI_CHOOSECARETX);
}

// ============================================================================
// Finder (search results)
// ============================================================================

FoundInfo Finder::EmptyFoundInfo(0, 0, 0, L"");
SearchResultMarkingLine Finder::EmptySearchResultMarking;

Finder::Finder(QWidget* parent)
	: QDockWidget(parent)
{
	_markingsStruct._length = 0;
	_markingsStruct._markings = nullptr;

	setObjectName(QStringLiteral("SearchResults"));
	setWindowTitle(QStringLiteral("Search results"));
	setAllowedAreas(Qt::AllDockWidgetAreas);

	_scintView = new ScintillaEditView(false, this);
	_scintView->init();
	setWidget(_scintView);

	// upstream: Finder::notify(), called from its WM_NOTIFY
	connect(_scintView, &ScintillaEditBase::notify, this, [this](Scintilla::NotificationData* scn) {
		notify(reinterpret_cast<SCNotification*>(scn));
	});
	_scintView->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(_scintView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		showContextMenu(_scintView->mapToGlobal(pos));
	});
	_scintView->installEventFilter(this);
}

void Finder::init(ScintillaEditView** ppEditView, ParentMessenger sendToParent)
{
	_ppEditView = ppEditView;
	_sendToParent = std::move(sendToParent);
}

void Finder::display(bool toShow)
{
	setVisible(toShow);
	if (toShow)
		raise();
}

bool Finder::eventFilter(QObject* watched, QEvent* event)
{
	// upstream FindReplaceDlg::FinderProc: Enter opens the result, Delete removes it
	if (watched == _scintView && event->type() == QEvent::KeyPress)
	{
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		if (keyEvent->key() == Qt::Key_Delete && keyEvent->modifiers() == Qt::NoModifier)
		{
			deleteResult();
			return true;
		}
		if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) && keyEvent->modifiers() == Qt::NoModifier)
		{
			std::pair<intptr_t, intptr_t> newPos = gotoFoundLine();
			auto lno = _scintView->execute(SCI_LINEFROMPOSITION, _scintView->execute(SCI_GETCURRENTPOS));
			auto lineStartAbsPos = _scintView->execute(SCI_POSITIONFROMLINE, lno);
			intptr_t lineEndAbsPos = _scintView->execute(SCI_GETLINEENDPOSITION, lno);
			intptr_t begin = newPos.first + lineStartAbsPos;
			intptr_t end = std::min(newPos.second + lineStartAbsPos, lineEndAbsPos);
			if (begin < end)
				_scintView->execute(SCI_SETSEL, begin, end);
			if (*_ppEditView)
				(*_ppEditView)->setFocus();
			return true;
		}
	}
	return QDockWidget::eventFilter(watched, event);
}

bool Finder::notify(SCNotification *notification)
{
	switch (notification->nmhdr.code)
	{
		case SCN_MARGINCLICK:
			if (notification->margin == ScintillaEditView::_SC_MARGE_FOLDER)
			{
				_scintView->marginClick(notification->position, notification->modifiers);
			}
			break;

		case SCN_DOUBLECLICK:
		{
			// remove selection from the finder
			_isDoubleClicked = true;

			size_t pos = notification->position;
			if (static_cast<intptr_t>(pos) == INVALID_POSITION)
				pos = _scintView->execute(SCI_GETLINEENDPOSITION, notification->line);
			_scintView->execute(SCI_SETSEL, pos, pos);

			std::pair<intptr_t, intptr_t> newPos = gotoFoundLine();
			auto lineStartAbsPos = _scintView->execute(SCI_POSITIONFROMLINE, notification->line);
			intptr_t lineEndAbsPos = _scintView->execute(SCI_GETLINEENDPOSITION, notification->line);

			intptr_t begin = newPos.first + lineStartAbsPos;
			intptr_t end = newPos.second + lineStartAbsPos;

			if (end > lineEndAbsPos)
				end = lineEndAbsPos;

			if (begin < end)
			{
				_scintView->execute(SCI_SETSEL, begin, end);
				_scintView->execute(SCI_SCROLLRANGE, begin, end);
			}

		}
		break;

		case SCN_PAINTED :
			if (_isDoubleClicked)
			{
				(*_ppEditView)->setFocus();
				_isDoubleClicked = false;
			}
			break;

		default:
			break;
	}
	return false;
}

std::pair<intptr_t, intptr_t> Finder::gotoFoundLine(size_t nOccurrence)
{
	std::pair<intptr_t, intptr_t> emptyResult(0, 0);
	auto currentPos = _scintView->execute(SCI_GETCURRENTPOS);
	auto lno = _scintView->execute(SCI_LINEFROMPOSITION, currentPos);
	auto start = _scintView->execute(SCI_POSITIONFROMLINE, lno);
	auto end = _scintView->execute(SCI_GETLINEENDPOSITION, lno);

	if (start + 2 >= end) return emptyResult; // avoid empty lines

	if (_scintView->execute(SCI_GETFOLDLEVEL, lno) & SC_FOLDLEVELHEADERFLAG)
	{
		_scintView->execute(SCI_TOGGLEFOLD, lno);
		return  emptyResult;
	}

	if (static_cast<size_t>(lno) >= _pMainFoundInfos->size())
		return std::pair<intptr_t, intptr_t>(0, 0);

	const FoundInfo& fInfo = *(_pMainFoundInfos->begin() + lno);
	const SearchResultMarkingLine& markingLine = *(_pMainMarkings->begin() + lno);

	// Switch to another document
	if (!_sendToParent(WM_DOOPEN, 0, reinterpret_cast<sptr_t>(fInfo._fullPath.c_str()))) return emptyResult;

	(*_ppEditView)->_positionRestoreNeeded = false;

	size_t index = 0;

	if (nOccurrence > 0)
	{
		index = nOccurrence - 1;
	}
	else // nOccurrence == 0 -> not used: use current line relative pos to check if it's inside of a marked occurrence
	{
		intptr_t currentPosInLine = currentPos - start;

		for (std::pair<intptr_t, intptr_t> range : markingLine._segmentPostions)
		{
			if (range.first <= currentPosInLine && currentPosInLine <= range.second)
				break;

			++index;
		}
	}

	if (index >= fInfo._ranges.size())
		index = 0;

	Searching::displaySectionCentered(fInfo._ranges[index].first, fInfo._ranges[index].second, *_ppEditView);

	return markingLine._segmentPostions[index];
}

void Finder::deleteResult()
{
	auto currentPos = _scintView->execute(SCI_GETCURRENTPOS); // yniq - add handling deletion of multiple lines?

	auto lno = _scintView->execute(SCI_LINEFROMPOSITION, currentPos);
	auto start = _scintView->execute(SCI_POSITIONFROMLINE, lno);
	auto end = _scintView->execute(SCI_GETLINEENDPOSITION, lno);
	if (start + 2 >= end) return; // avoid empty lines

	if (_scintView->execute(SCI_GETLEXER) == SCLEX_NULL)
	{
		_scintView->setLexer(L_SEARCHRESULT, LIST_NONE); // Restore searchResult lexer in case the lexer was changed to SCLEX_NULL in GotoFoundLine()
	}

	if (_scintView->execute(SCI_GETFOLDLEVEL, lno) & SC_FOLDLEVELHEADERFLAG)  // delete a folder
	{
		auto endline = _scintView->execute(SCI_GETLASTCHILD, lno, -1) + 1;
		if ((size_t)endline > _pMainFoundInfos->size()) return;

		_pMainFoundInfos->erase(_pMainFoundInfos->begin() + lno, _pMainFoundInfos->begin() + endline); // remove found info
		_pMainMarkings->erase(_pMainMarkings->begin() + lno, _pMainMarkings->begin() + endline);

		auto end2 = _scintView->execute(SCI_POSITIONFROMLINE, endline);
		_scintView->execute(SCI_SETSEL, start, end2);
		setFinderReadOnly(false);
		_scintView->execute(SCI_CLEAR);
		setFinderReadOnly(true);
	}
	else // delete one line
	{
		if ((size_t)lno >= _pMainFoundInfos->size()) return;

		_pMainFoundInfos->erase(_pMainFoundInfos->begin() + lno); // remove found info
		_pMainMarkings->erase(_pMainMarkings->begin() + lno);

		setFinderReadOnly(false);
		_scintView->execute(SCI_LINEDELETE);
		setFinderReadOnly(true);
	}
	_markingsStruct._length = static_cast<long>(_pMainMarkings->size());
	if (!_pMainMarkings->empty())
		_markingsStruct._markings = &((*_pMainMarkings)[0]);

	assert(_pMainFoundInfos->size() == _pMainMarkings->size());
	assert(size_t(_scintView->execute(SCI_GETLINECOUNT)) == _pMainFoundInfos->size() + 1);
}

vector<wstring> Finder::getResultFilePaths(bool onlyInSelectedText) const
{
	size_t fromLine = 0, toLine = 0;

	if (onlyInSelectedText)
	{
		const pair<size_t, size_t> lineRange = _scintView->getSelectionLinesRange();
		fromLine = lineRange.first;
		toLine = lineRange.second;
	}
	else
	{
		toLine = _scintView->execute(SCI_GETLINECOUNT) - 1;
	}

	size_t len = _pMainFoundInfos->size();
	vector<wstring> paths;

	for (size_t line = fromLine; line <= toLine; ++line)
	{
		const int lineFoldLevel = _scintView->execute(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELNUMBERMASK;
		if (lineFoldLevel == fileHeaderLevel)
		{
			// fileHeaderLevel lines don't have path info; have to look into the NEXT line for it,
			// but only need to do something special here if we are on the LAST line of the selection
			if (line == toLine)
			{
				++line;
			}
		}

		if (line < len)
		{
			wstring& path2add = (*_pMainFoundInfos)[line]._fullPath;
			if (!path2add.empty())
			{
				// make sure that path is not already in
				if (std::find(paths.begin(), paths.end(), path2add) == paths.end())
				{
					paths.push_back(path2add);
				}
			}
		}
	}

	return paths;
}


bool Finder::canFind(const wchar_t *fileName, size_t lineNumber, size_t* indexToStartFrom) const
{
	size_t len = _pMainFoundInfos->size();
	for (size_t i = *indexToStartFrom; i < len; ++i)
	{
		if ((*_pMainFoundInfos)[i]._fullPath == fileName)
		{
			if (lineNumber == (*_pMainFoundInfos)[i]._lineNumber)
			{
				*indexToStartFrom = i;
				return true;
			}
		}
	}
	return false;
}

// Y     : current pos
// X     : current sel
// XXXXY : current sel + current pos
//
//                occurrence 1   occur. 2         occur. 3  occur. 4              Status      auxiliaryInfo
// =========================================================================================================
// situation  1:     Y [     ]    [     ]          [     ]  [     ]            : pos_infront        -1
// situation  2:       [     ]    [     ]          [     ]  [  Y  ]            : pos_inside          4
// situation  3:       [     ]  XXY     ]          [     ]  [     ]            : pos_inside          2
// situation  4:       [     ]    [     ]          [     ]  [  XXXY            : pos_inside          4
// situation  5:       [     ]    [     ]          [XXXXXY  [     ]            : pos_inside          3
// situation  6:       [     Y    [     ]          [     ]  [     ]            : pos_between         1
// situation  7:       [     ]    [     ]          [     ]  Y     ]            : pos_between         3
// situation  8:       [     ]    [     ]          [     Y  [     ]            : pos_between         3
// situation  9:       [     ]    [     ]  Y       [     ]  [     ]            : pos_between         2
// situation 10:       [     ]    [     ]          [     ]  [     ]    Y       : pos_behind          4

Finder::CurrentPosInLineInfo Finder::getCurrentPosInLineInfo(intptr_t currentPosInLine, const SearchResultMarkingLine& markingLine) const
{
	CurrentPosInLineInfo cpili;
	size_t count = 0;
	intptr_t lastEnd = 0;
	auto selStart = _scintView->execute(SCI_GETSELECTIONSTART);
	auto selEnd = _scintView->execute(SCI_GETSELECTIONEND);
	bool hasSel = (selEnd - selStart) != 0;

	for (std::pair<intptr_t, intptr_t> range : markingLine._segmentPostions)
	{
		++count;

		if (lastEnd <= currentPosInLine && currentPosInLine < range.first)
		{
			if (count == 1)
			{
				cpili._status = pos_infront;
				break;
			}
			else
			{
				cpili._status = pos_between;
				cpili.auxiliaryInfo = count - 1;
				break;
			}
		}

		if (range.first <= currentPosInLine && currentPosInLine <= range.second)
		{
			if (currentPosInLine == range.first && !hasSel)
			{
				cpili._status = pos_between;
				cpili.auxiliaryInfo = count - 1; //  c1      c2
				                                 // [   ]  I[   ]      : I is recongnized with c2, so auxiliaryInfo should be c1 (c2-1)
			}
			else if (currentPosInLine == range.second && !hasSel)
			{
				cpili._status = pos_between;
				cpili.auxiliaryInfo = count;     //  c1      c2
				                                 // [   ]I  [   ]      : I is recongnized with c1, so auxiliaryInfo should be c1
			}
			else
			{
				cpili._status = pos_inside;
				cpili.auxiliaryInfo = count;
			}
			break;
		}

		if (range.second < currentPosInLine)
		{
			if (markingLine._segmentPostions.size() == count)
			{
				cpili._status = pos_behind;
				cpili.auxiliaryInfo = count;
				break;
			}
		}

		lastEnd = range.second;
	}

	return cpili;
}

void Finder::anchorWithNoHeaderLines(intptr_t& currentL, intptr_t initL, intptr_t minL, intptr_t maxL, int direction)
{
	if (currentL > maxL && direction == 0)
		currentL = minL;

	while (_scintView->execute(SCI_GETFOLDLEVEL, currentL) & SC_FOLDLEVELHEADERFLAG)
	{
		currentL += direction == -1 ? -1 : 1;

		if (currentL > maxL)
			currentL = minL;
		else if (currentL < minL)
			currentL = maxL;

		if (currentL == initL)
			break;
	}

	auto extremityAbsoltePos = _scintView->execute(direction == -1 ? SCI_GETLINEENDPOSITION : SCI_POSITIONFROMLINE, currentL);
	_scintView->execute(SCI_SETSEL, extremityAbsoltePos, extremityAbsoltePos);
}

void Finder::gotoNextFoundResult(int direction)
{
	//
	// Get currentLine & currentPosInLine from CurrentPos
	//
	auto currentPos = _scintView->execute(SCI_GETCURRENTPOS);
	intptr_t lno = _scintView->execute(SCI_LINEFROMPOSITION, currentPos);
	auto total_lines = _scintView->execute(SCI_GETLINECOUNT);
	if (total_lines <= 1) return;

	auto lineStartAbsPos = _scintView->execute(SCI_POSITIONFROMLINE, lno);
	intptr_t currentPosInLine = currentPos - lineStartAbsPos;

	auto init_lno = lno;
	auto max_lno = _scintView->execute(SCI_GETLASTCHILD, lno, searchHeaderLevel);

	if (max_lno > total_lines - 2) return;

	// get the line number of the current search (searchHeaderLevel)
	int level = _scintView->execute(SCI_GETFOLDLEVEL, lno) & SC_FOLDLEVELNUMBERMASK;
	auto min_lno = lno;
	while (level-- >= fileHeaderLevel)
	{
		min_lno = _scintView->execute(SCI_GETFOLDPARENT, min_lno);
		assert(min_lno >= 0);
	}

	if (min_lno < 0)
		min_lno = lno; // when lno is a search header line

	assert(min_lno <= max_lno);

	if (lno > max_lno && direction == 0)
		lno = min_lno;
	else if (lno < min_lno)
		lno = max_lno;

	//
	// Set anchor and make sure that achor is not on the last (empty) line or head lines
	//
	while (_scintView->execute(SCI_GETFOLDLEVEL, lno) & SC_FOLDLEVELHEADERFLAG)
	{
		lno += direction == -1 ? -1 : 1;

		if (lno > max_lno)
			lno = min_lno;
		else if (lno < min_lno)
			lno = max_lno;

		if (lno == init_lno)
			break;
	}

	if (lno != init_lno)
	{
		auto extremityAbsoltePos = _scintView->execute(direction == -1 ? SCI_GETLINEENDPOSITION : SCI_POSITIONFROMLINE, lno);
		_scintView->execute(SCI_SETSEL, extremityAbsoltePos, extremityAbsoltePos);
		currentPos = extremityAbsoltePos;
		auto start = _scintView->execute(SCI_POSITIONFROMLINE, lno);
		currentPosInLine = currentPos - start;
	}


	size_t occurrenceNumberInLine_base1 = 0;
	const SearchResultMarkingLine& markingLine = *(_pMainMarkings->begin() + lno);

	//
	// Determine currentPosInLine status among pos_infront, pose_between, pos_inside and pos_behind
	//
	CurrentPosInLineInfo cpili  = getCurrentPosInLineInfo(currentPosInLine, markingLine);

	//
	// According CurrentPosInLineInfo and direction, set position and get number of occurrence
	//
	if (direction == 0) // Next
	{
		switch (cpili._status)
		{
			case pos_infront: // 2 situation:
			{
				if (!markingLine._segmentPostions.size())
					return;

				if (markingLine._segmentPostions[0].second < SC_SEARCHRESULT_LINEBUFFERMAXLENGTH) // 1. The occurrence is displayed in the line
				{
					occurrenceNumberInLine_base1 = 1;
				}
				else // 2. The occurrence is NOT displayed in the line
				{
					lno++;
					anchorWithNoHeaderLines(lno, init_lno, min_lno, max_lno, direction);
					occurrenceNumberInLine_base1 = 1;
				}
			}
			break;

			case pos_between:
			case pos_inside:
			{
				occurrenceNumberInLine_base1 = cpili.auxiliaryInfo + 1;
				if (occurrenceNumberInLine_base1 > markingLine._segmentPostions.size())
				{
					lno++;
					anchorWithNoHeaderLines(lno, init_lno, min_lno, max_lno, direction);
					occurrenceNumberInLine_base1 = 1;
				}
			}
			break;

			case pos_behind:
			{
				lno++;
				anchorWithNoHeaderLines(lno, init_lno, min_lno, max_lno, direction);
				occurrenceNumberInLine_base1 = 1;
			}
			break;
		}
	}
	else if (direction == -1) // Previous
	{
		switch (cpili._status)
		{
			case pos_infront:
			{
				lno--;
				if (lno < 0) return;
				anchorWithNoHeaderLines(lno, init_lno, min_lno, max_lno, direction);
				const SearchResultMarkingLine& newMarkingLine = *(_pMainMarkings->begin() + lno);
				occurrenceNumberInLine_base1 = newMarkingLine._segmentPostions.size();
			}
			break;

			case pos_between:
			{
				occurrenceNumberInLine_base1 = cpili.auxiliaryInfo;
			}
			break;

			case pos_inside:
			{
				if (cpili.auxiliaryInfo > 1)
					occurrenceNumberInLine_base1 = cpili.auxiliaryInfo - 1;
				else
				{
					lno--;
					if (lno < 0) return;
					anchorWithNoHeaderLines(lno, init_lno, min_lno, max_lno, direction);
					const SearchResultMarkingLine& newMarkingLine = *(_pMainMarkings->begin() + lno);
					occurrenceNumberInLine_base1 = newMarkingLine._segmentPostions.size();
				}
			}
			break;

			case pos_behind:
			{
				occurrenceNumberInLine_base1 = cpili.auxiliaryInfo;
			}
			break;
		}
	}
	else // invalid
	{
		return;
	}

	_scintView->execute(SCI_ENSUREVISIBLE, lno);
	std::pair<intptr_t, intptr_t> newPos = gotoFoundLine(occurrenceNumberInLine_base1);

	lineStartAbsPos = _scintView->execute(SCI_POSITIONFROMLINE, lno);
	intptr_t lineEndAbsPos = _scintView->execute(SCI_GETLINEENDPOSITION, lno);

	intptr_t begin = newPos.first + lineStartAbsPos;
	intptr_t end = newPos.second + lineStartAbsPos;

	if (end > lineEndAbsPos)
		end = lineEndAbsPos;

	if (begin < end)
	{
		_scintView->execute(SCI_SETSEL, begin, end);
		_scintView->execute(SCI_SCROLLRANGE, begin, end);
	}
}

wstring Finder::getHitsString(int count) const
{
	NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	wstring text = pNativeSpeaker->getLocalizedStrFromID("find-result-hits", L"");

	if (text.empty())
	{
		if (count == 1)
		{
			text = L"(1 hit)";
		}
		else
		{
			text = L"(";
			text += std::to_wstring(count);
			text += L" hits)";
		}
	}
	else
	{
		text = stringReplace(text, L"$INT_REPLACE$", std::to_wstring(count));
	}

	return text;
}

void Finder::addSearchLine(const wchar_t *searchName)
{
	wstring oneLineSearchName = searchName;
	oneLineSearchName.erase(std::remove(oneLineSearchName.begin(), oneLineSearchName.end(), L'\r'), oneLineSearchName.end());
	oneLineSearchName.erase(std::remove(oneLineSearchName.begin(), oneLineSearchName.end(), L'\n'), oneLineSearchName.end());

	NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	wstring str = pNativeSpeaker->getLocalizedStrFromID("find-result-title", L"Search");
	str += L" \"";
	str += oneLineSearchName;
	str += L"\" \r\n";

	setFinderReadOnly(false);
	_scintView->addGenericText(str.c_str());
	setFinderReadOnly(true);
	_lastSearchHeaderPos = _scintView->execute(SCI_GETCURRENTPOS) - 2;

	_pMainFoundInfos->push_back(EmptyFoundInfo);
	_pMainMarkings->push_back(EmptySearchResultMarking);
	_previousLineNumber = -1;
}

void Finder::addFileNameTitle(const wchar_t * fileName)
{
	wstring str = L"  ";
	str += fileName;
	str += L"\r\n";

	setFinderReadOnly(false);
	_scintView->addGenericText(str.c_str());
	setFinderReadOnly(true);
	_lastFileHeaderPos = _scintView->execute(SCI_GETCURRENTPOS) - 2;

	_pMainFoundInfos->push_back(EmptyFoundInfo);
	_pMainMarkings->push_back(EmptySearchResultMarking);
	_previousLineNumber = -1;
}

void Finder::addFileHitCount(int count)
{
	wstring text = L" ";
	text += getHitsString(count);
	setFinderReadOnly(false);
	_scintView->insertGenericTextFrom(_lastFileHeaderPos, text.c_str());
	setFinderReadOnly(true);
	++_nbFoundFiles;
}

void Finder::addSearchResultInfo(int count, int countSearched, bool searchedEntireNotSelection, const FindOption* pFindOpt)
{
	wstring nbResStr = commafyInt(count);
	wstring nbFoundFilesStr = commafyInt(_nbFoundFiles);
	wstring nbSearchedFilesStr = commafyInt(countSearched);

	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

	wstring text = pNativeSpeaker->getLocalizedStrFromID(searchedEntireNotSelection ? "find-result-title-info" : "find-result-title-info-selections", L"");

	if (text.empty())
	{
		// create default output: "(1 hit in 1 file of 1 searched)"
		//  variation             "(1 hit in 2 files of 2 searched)"
		//  variation             "(2 hits in 1 file of 3 searched)"
		//  variation             "(0 hits in 0 files of 1 searched)"
		// selection variations:  "(1 hit in 1 selection of 1 searched)"     " (0 hits in 0 selections of 1 searched)"
		// line filter variation: "(1 hit in 1 file of 1 searched) - Line Filter Mode: only display the filtered results"

		wstring hitsIn = count == 1 ? L"hit" : L"hits";

		wstring fileOrSelection = searchedEntireNotSelection ? L"file" : L"selection";
		if (_nbFoundFiles != 1)
		{
			fileOrSelection += L"s";
		}

		text = L"(" + nbResStr + L" " + hitsIn + L" in " + nbFoundFilesStr + L" " +
			fileOrSelection + L" of " + nbSearchedFilesStr + L" searched" + L")";
	}
	else
	{
		text = stringReplace(text, L"$INT_REPLACE1$", nbResStr);
		text = stringReplace(text, L"$INT_REPLACE2$", nbFoundFilesStr);
		text = stringReplace(text, L"$INT_REPLACE3$", nbSearchedFilesStr);
	}

	wstring searchModeText;
	if (pFindOpt->_searchType == FindExtended)
	{
		searchModeText += pNativeSpeaker->getLocalizedStrFromID("find-result-title-info-options-searchmode-extended", L"Extended");
	}
	else if (pFindOpt->_searchType == FindRegex)
	{
		searchModeText += pNativeSpeaker->getLocalizedStrFromID("find-result-title-info-options-searchmode-regexp", L"RegEx");
		if (pFindOpt->_dotMatchesNewline) searchModeText += L".";
	}
	else
	{
		searchModeText += pNativeSpeaker->getLocalizedStrFromID("find-result-title-info-options-searchmode-normal", L"Normal");
	}

	wstring searchOptionsText;
	if (pFindOpt->_isMatchCase)
	{
		searchOptionsText += pNativeSpeaker->getLocalizedStrFromID("find-result-title-info-options-case", L"Case");
	}
	if (pFindOpt->_isWholeWord)
	{
		if (!searchOptionsText.empty()) searchOptionsText += L"/";
		searchOptionsText += pNativeSpeaker->getLocalizedStrFromID("find-result-title-info-options-word", L"Word");
	}

	if (!searchOptionsText.empty()) searchModeText += L": ";

	text += L" [" + searchModeText + searchOptionsText + L"]";

	if (pFindOpt->_isMatchLineNumber)
	{
		wstring lineFilterModeInfo = pNativeSpeaker->getLocalizedStrFromID("find-result-title-info-extra", L" - Line Filter Mode: only display the filtered results");
		text += lineFilterModeInfo;
	}

	setFinderReadOnly(false);
	_scintView->insertGenericTextFrom(_lastSearchHeaderPos, text.c_str());
	setFinderReadOnly(true);
}

string Finder::foundLine(FoundInfo fi, SearchResultMarkingLine miLine, const wchar_t* foundline, size_t foundLineLen, size_t totalLineNumber)
{
	bool isRepeatedLine = false;

	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	if (nppGUI._finderShowOnlyOneEntryPerFoundLine)
	{
		if (_previousLineNumber == -1)
		{
			_previousLineNumber = fi._lineNumber;
		}
		else if (_previousLineNumber == static_cast<intptr_t>(fi._lineNumber))
		{
			isRepeatedLine = true;
		}
		else // previousLine != fi._lineNumber
		{
			_previousLineNumber = fi._lineNumber;
		}
	}

	wstring headerStr = L"\t";
	headerStr += _prefixLineStr;
	headerStr += L" ";

	size_t totalLineNumberDigit = static_cast<size_t>(nbDigitsFromNbLines(totalLineNumber) + 1);
	size_t currentLineNumberDigit = static_cast<size_t>(nbDigitsFromNbLines(fi._lineNumber) + 1);

	wstring lineNumberStr = L"";
	lineNumberStr.append(totalLineNumberDigit - currentLineNumberDigit, ' ');
	lineNumberStr.append(std::to_wstring(fi._lineNumber));
	headerStr += lineNumberStr;
	headerStr += L": ";

	miLine._segmentPostions[0].first += headerStr.length();
	miLine._segmentPostions[0].second += headerStr.length();

	headerStr += wstring(foundline, foundLineLen);

	WcharMbcsConvertor& wmc = WcharMbcsConvertor::getInstance();
	int text2AddUtf8Len = 0;
	const char* text2AddUtf8 = wmc.wchar2char(headerStr.c_str(), SC_CP_UTF8, &miLine._segmentPostions[0].first, &miLine._segmentPostions[0].second, static_cast<int>(headerStr.length()), &text2AddUtf8Len); // certainly utf8 here


	// if current line is the repeated line of previous one, and settings make per found line show ONCE in the result even there are several found occurences in the same line, for:
	if ((isRepeatedLine &&
		((text2AddUtf8Len < SC_SEARCHRESULT_LINEBUFFERMAXLENGTH) ||                                                                              // 1. All displayed whole lines, in which it contains all the occurrences.
		((text2AddUtf8Len >= SC_SEARCHRESULT_LINEBUFFERMAXLENGTH && miLine._segmentPostions[0].second < SC_SEARCHRESULT_LINEBUFFERMAXLENGTH))))) // 2. or the cut lines but the segments are displayed. For the segments displayed beyond displayed (non-cut part), go to default mode.
	{
		// Add start and end markers into the previous line's info for colourizing
		_pMainMarkings->back()._segmentPostions.push_back(std::pair<intptr_t, intptr_t>(miLine._segmentPostions[0].first, miLine._segmentPostions[0].second));
		_pMainFoundInfos->back()._ranges.push_back(fi._ranges.back());

		return "";
	}
	else // default mode: allow same found line has several entries in search result if the searched occurrence is matched several times in the same line
	{
		_pMainFoundInfos->push_back(fi);


		if (text2AddUtf8Len >= SC_SEARCHRESULT_LINEBUFFERMAXLENGTH)
		{
			const char* endOfLongLine = " ...\r\n"; // perfectly Utf8-encoded already
			size_t lenEndOfLongLine = strlen(endOfLongLine);
			size_t cut = SC_SEARCHRESULT_LINEBUFFERMAXLENGTH - lenEndOfLongLine - 1;

			while ((cut > 0) && (!Utf8::isValid(&text2AddUtf8[cut], (int)(text2AddUtf8Len - cut))))
				cut--;

			memcpy(static_cast<void*>(const_cast<char*>(&text2AddUtf8[cut])), endOfLongLine, lenEndOfLongLine + 1);
			text2AddUtf8Len = static_cast<int>(cut + lenEndOfLongLine);
		}

		_pMainMarkings->push_back(miLine);

		return string(text2AddUtf8, text2AddUtf8Len);
	}
}

void Finder::removeAll()
{
	_pMainFoundInfos->clear();
	_pMainMarkings->clear();
	_markingsStruct._length = 0;
	_markingsStruct._markings = nullptr;
	setFinderReadOnly(false);
	_scintView->execute(SCI_CLEARALL);
	setFinderReadOnly(true);
}

void Finder::openAll()
{
	for (auto&& path : getResultFilePaths(true))
	{
		_sendToParent(WM_DOOPEN, 0, reinterpret_cast<sptr_t>(path.c_str()));
	}
}

void Finder::copyPathnames()
{
	wstring toClipboard;
	for (auto&& path : getResultFilePaths(true))
	{
		toClipboard += path + L"\n"; // NPLX: LF (upstream \r\n)
	}
	if (!toClipboard.empty())
	{
		QGuiApplication::clipboard()->setText(QString::fromStdWString(toClipboard)); // NPLX: was str2Clipboard
	}
}

void Finder::wrapLongLinesToggle()
{
	_longLinesAreWrapped = !_longLinesAreWrapped;
	_scintView->wrap(_longLinesAreWrapped);

	if (!_canBeVolatiled)
	{
		// only remember this setting from the original finder
		NppParameters& nppParam = NppParameters::getInstance();
		NppGUI& nppGUI = nppParam.getNppGUI();
		nppGUI._finderLinesAreCurrentlyWrapped = _longLinesAreWrapped;
	}
}

void Finder::purgeToggle()
{
	_purgeBeforeEverySearch = !_purgeBeforeEverySearch;

	if (!_canBeVolatiled)
	{
		// only remember this setting from the original finder
		NppParameters& nppParam = NppParameters::getInstance();
		NppGUI& nppGUI = nppParam.getNppGUI();
		nppGUI._finderPurgeBeforeEverySearch = _purgeBeforeEverySearch;
	}
}

bool Finder::isLineActualSearchResult(const wstring& s)
{
	// actual-search-result lines are the only type that start with a tab character
	// sample: "\tLine 123: xxxxxxHITxxxxxx"
	return (s.find(L"\t") == 0);
}

wstring & Finder::prepareStringForClipboard(wstring & s) const
{
	// Input: a string like "\tLine 3: search result".
	// Output: "search result"
	s = stringReplace(s, L"\r", L"");
	s = stringReplace(s, L"\n", L"");
	const auto firstColon = s.find(L':');
	if (firstColon == std::string::npos)
	{
		// Should never happen.
		assert(false);
		return s;
	}
	else
	{
		// Plus 2 in order to deal with ": ".
		s = s.substr(2 + firstColon);
		return s;
	}
}

void Finder::copy()
{
	const pair<size_t, size_t> lineRange = _scintView->getSelectionLinesRange();
	size_t fromLine = lineRange.first;
	size_t toLine = lineRange.second;

	if (_scintView->execute(SCI_GETSELECTIONEMPTY) || fromLine == toLine)
	{
		const int selectedLineFoldLevel = _scintView->execute(SCI_GETFOLDLEVEL, fromLine) & SC_FOLDLEVELNUMBERMASK;
		if (selectedLineFoldLevel != resultLevel)
		{
			// caret on Search "..." header line
			// or
			// caret is on a line with a pathname

			// locate the final resultLevel line under its parent grouping
			toLine = _scintView->execute(SCI_GETLASTCHILD, fromLine, selectedLineFoldLevel);
			const int toLineFoldLevel = _scintView->execute(SCI_GETFOLDLEVEL, toLine) & SC_FOLDLEVELNUMBERMASK;
			if (toLineFoldLevel != resultLevel)
			{
				return;  // the search had 0 hits, so no resultLevel lines, nothing to copy
			}
		}
	}

	std::vector<wstring> lines;
	wstring previousResultLineStr(L"");
	for (size_t line = fromLine; line <= toLine; ++line)
	{
		wstring lineStr = _scintView->getLine(line);
		if (isLineActualSearchResult(lineStr))
		{
			if (lineStr != previousResultLineStr)
			{
				previousResultLineStr = lineStr;
				lines.push_back(prepareStringForClipboard(lineStr));
			}
		}
	}
	wstring toClipboard;
	stringJoin(lines, L"\n", toClipboard); // NPLX: LF (upstream \r\n)
	toClipboard += L"\n";
	QGuiApplication::clipboard()->setText(QString::fromStdWString(toClipboard)); // NPLX: was str2Clipboard
}

void Finder::beginNewFilesSearch()
{
	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	_prefixLineStr = pNativeSpeaker->getLocalizedStrFromID("find-result-line-prefix", L"Line");

	// Use SCI_SETSEL(0, 0) instead of SCI_SETCURRENTPOS(0) to workaround
	// an eventual regression or a change of behaviour in Scintilla 4.4.6
	// ref: https://github.com/notepad-plus-plus/notepad-plus-plus/issues/9595#issuecomment-789824579
	//
	_scintView->execute(SCI_SETSEL, 0, 0);

	_pMainFoundInfos = _pMainFoundInfos == &_foundInfos1 ? &_foundInfos2 : &_foundInfos1;
	_pMainMarkings = _pMainMarkings == &_markings1 ? &_markings2 : &_markings1;
	_nbFoundFiles = 0;

	// fold all old searches (1st level only)
	_scintView->foldLevel(searchHeaderLevel - SC_FOLDLEVELBASE, fold_collapse);
}

void Finder::finishFilesSearch(int count, int searchedCount, bool searchedEntireNotSelection, const FindOption* pFindOpt)
{
	std::vector<FoundInfo>* _pOldFoundInfos;
	std::vector<SearchResultMarkingLine>* _pOldMarkings;
	_pOldFoundInfos = _pMainFoundInfos == &_foundInfos1 ? &_foundInfos2 : &_foundInfos1;
	_pOldMarkings = _pMainMarkings == &_markings1 ? &_markings2 : &_markings1;

	_pOldFoundInfos->insert(_pOldFoundInfos->begin(), _pMainFoundInfos->begin(), _pMainFoundInfos->end());
	_pOldMarkings->insert(_pOldMarkings->begin(), _pMainMarkings->begin(), _pMainMarkings->end());
	_pMainFoundInfos->clear();
	_pMainMarkings->clear();
	_pMainFoundInfos = _pOldFoundInfos;
	_pMainMarkings = _pOldMarkings;

	_markingsStruct._length = static_cast<long>(_pMainMarkings->size());
	if (_pMainMarkings->size() > 0)
		_markingsStruct._markings = &((*_pMainMarkings)[0]);

	addSearchResultInfo(count, searchedCount, searchedEntireNotSelection, pFindOpt);
	_scintView->execute(SCI_SETSEL, 0, 0);

	//SCI_SETILEXER resets the lexer property @MarkingsStruct and then no data could be exchanged with the searchResult lexer
	char ptrword[sizeof(void*) * 2 + 3];
	snprintf(ptrword, sizeof(ptrword), "%p", static_cast<void*>(&_markingsStruct));
	_scintView->execute(SCI_SETPROPERTY, reinterpret_cast<WPARAM>("@MarkingsStruct"), reinterpret_cast<LPARAM>(ptrword));

	//previous code: _scintView.execute(SCI_SETILEXER, 0, reinterpret_cast<LPARAM>(CreateLexer("searchResult")));
	_scintView->execute(SCI_SETPROPERTY, reinterpret_cast<WPARAM>("fold"), reinterpret_cast<LPARAM>("1"));

	// NPLX: the lexer only re-reads the markings when the text is styled again
	_scintView->execute(SCI_COLOURISE, 0, -1);

	_previousLineNumber = -1;
}


void Finder::setFinderStyle()
{
	// Set global styles for the finder
	_scintView->performGlobalStyles();

	// Set current line background color for the finder
	const wchar_t * lexerName = ScintillaEditView::_langNameInfoArray[L_SEARCHRESULT]._langName;
	LexerStyler *pStyler = (NppParameters::getInstance().getLStylerArray()).getLexerStylerByName(lexerName);
	if (pStyler)
	{
		const Style * pStyle = pStyler->findByID(SCE_SEARCHRESULT_CURRENT_LINE);
		if (pStyle)
		{
			_scintView->setElementColour(SC_ELEMENT_CARET_LINE_BACK, pStyle->_bgColor);
			_scintView->execute(SCI_SETCARETLINEFRAME, 0);
			_scintView->execute(SCI_SETCARETLINEVISIBLEALWAYS, true);
		}
	}
	_scintView->setSearchResultLexer();

	// Override foreground & background colour by default foreground & background coulour
	StyleArray & stylers = NppParameters::getInstance().getMiscStylerArray();
	Style * pStyleDefault = stylers.findByID(STYLE_DEFAULT);
	if (pStyleDefault)
	{
		_scintView->setStyle(*pStyleDefault);

		GlobalOverride & go = NppParameters::getInstance().getGlobalOverrideStyle();
		if (go.isEnable())
		{
			const Style * pStyleGlobalOverride = stylers.findByName(L"Global override");
			if (pStyleGlobalOverride)
			{
				if (go.enableFg)
				{
					pStyleDefault->_fgColor = pStyleGlobalOverride->_fgColor;
				}
				if (go.enableBg)
				{
					pStyleDefault->_bgColor = pStyleGlobalOverride->_bgColor;
				}
			}
		}

		_scintView->execute(SCI_STYLESETFORE, SCE_SEARCHRESULT_DEFAULT, pStyleDefault->_fgColor);
		_scintView->execute(SCI_STYLESETBACK, SCE_SEARCHRESULT_DEFAULT, pStyleDefault->_bgColor);
	}

	_scintView->execute(SCI_COLOURISE, 0, -1);

	// finder fold style follows user preference but use box when user selects none
	const ScintillaViewParams& svp = NppParameters::getInstance().getSVP();
	_scintView->setMakerStyle(svp._folderStyle == FOLDER_STYLE_NONE ? FOLDER_STYLE_BOX : svp._folderStyle);
}

void Finder::setFinderStyleForNpc(bool onlyColor)
{
	NppParameters& nppParam = NppParameters::getInstance();
	const bool isShown = nppParam.getSVP()._npcShow;
	if (!onlyColor)
	{
		_scintView->showNpc(isShown, true);
	}
	else if (isShown)
	{
		_scintView->setNpcAndCcUniEOL();
	}
}

void Finder::runCommand(int cmd)
{
	// upstream Finder::run_dlgProc WM_COMMAND
	switch (cmd)
	{
		case NPPM_INTERNAL_FINDINFINDERDLG:
			// NPLX_TODO(Phase 4): FindInFinderDlg
			break;
		case NPPM_INTERNAL_REMOVEFINDER:
			if (_canBeVolatiled)
			{
				display(false);
				deleteLater();
			}
			break;
		case NPPM_INTERNAL_SCINTILLAFINDERCOLLAPSE :
			_scintView->foldAll(fold_collapse);
			break;
		case NPPM_INTERNAL_SCINTILLAFINDERUNCOLLAPSE :
			_scintView->foldAll(fold_expand);
			break;
		case NPPM_INTERNAL_SCINTILLAFINDERCOPY :
			copy();
			break;
		case NPPM_INTERNAL_SCINTILLAFINDERCOPYVERBATIM:
			_scintView->execute(SCI_COPY);
			break;
		case NPPM_INTERNAL_SCINTILLAFINDERCOPYPATHS:
			copyPathnames();
			break;
		case NPPM_INTERNAL_SCINTILLAFINDERSELECTALL :
			_scintView->execute(SCI_SELECTALL);
			break;
		case NPPM_INTERNAL_SCINTILLAFINDERCLEARALL:
			removeAll();
			break;
		case NPPM_INTERNAL_SCINTILLAFINDEROPENALL:
			openAll();
			break;
		case NPPM_INTERNAL_SCINTILLAFINDERWRAP:
			wrapLongLinesToggle();
			break;
		case NPPM_INTERNAL_SCINTILLAFINDERPURGE:
			purgeToggle();
			break;
		default:
			break;
	}
}

void Finder::showContextMenu(const QPoint& globalPos)
{
	// upstream Finder::run_dlgProc WM_CONTEXTMENU
	NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	wstring findInFinder = pNativeSpeaker->getLocalizedStrFromID("finder-find-in-finder", L"Find in these search results...");
	wstring closeThis = pNativeSpeaker->getLocalizedStrFromID("finder-close-this", L"Close these search results");
	wstring collapseAll = pNativeSpeaker->getLocalizedStrFromID("finder-collapse-all", L"Fold all");
	wstring uncollapseAll = pNativeSpeaker->getLocalizedStrFromID("finder-uncollapse-all", L"Unfold all");
	wstring copyLines = pNativeSpeaker->getLocalizedStrFromID("finder-copy", L"Copy Selected Line(s)");
	wstring copyVerbatim = pNativeSpeaker->getNativeLangMenuString(IDM_EDIT_COPY, L"Copy", true);
	copyVerbatim += L"\tCtrl+C";
	wstring copyPaths = pNativeSpeaker->getLocalizedStrFromID("finder-copy-selected-paths", L"Copy Selected Pathname(s)");
	wstring selectAll = pNativeSpeaker->getNativeLangMenuString(IDM_EDIT_SELECTALL, L"Select all", true);
	selectAll += L"\tCtrl+A";
	wstring clearAll = pNativeSpeaker->getLocalizedStrFromID("finder-clear-all", L"Clear all");
	wstring purgeForEverySearch = pNativeSpeaker->getLocalizedStrFromID("finder-purge-for-every-search", L"Purge for every search");
	wstring openSelectedPath = pNativeSpeaker->getLocalizedStrFromID("finder-open-selected-paths", L"Open Selected Pathname(s)");
	wstring wrapLongLines = pNativeSpeaker->getLocalizedStrFromID("finder-wrap-long-lines", L"Word wrap long lines");

	QMenu menu(this);
	auto add = [&](int cmd, const wstring& text) {
		QAction* action = menu.addAction(QString::fromStdWString(text));
		connect(action, &QAction::triggered, this, [this, cmd]() { runCommand(cmd); });
		return action;
	};

	QAction* findInFinderAction = add(NPPM_INTERNAL_FINDINFINDERDLG, findInFinder);
	findInFinderAction->setEnabled(false);
	findInFinderAction->setToolTip(QStringLiteral("Not yet ported"));
	if (_canBeVolatiled)
		add(NPPM_INTERNAL_REMOVEFINDER, closeThis);
	menu.addSeparator();
	add(NPPM_INTERNAL_SCINTILLAFINDERCOLLAPSE, collapseAll);
	add(NPPM_INTERNAL_SCINTILLAFINDERUNCOLLAPSE, uncollapseAll);
	menu.addSeparator();
	QAction* copyVerbatimAction = add(NPPM_INTERNAL_SCINTILLAFINDERCOPYVERBATIM, copyVerbatim);
	add(NPPM_INTERNAL_SCINTILLAFINDERCOPY, copyLines);
	add(NPPM_INTERNAL_SCINTILLAFINDERCOPYPATHS, copyPaths);
	add(NPPM_INTERNAL_SCINTILLAFINDERSELECTALL, selectAll);
	QAction* clearAllAction = add(NPPM_INTERNAL_SCINTILLAFINDERCLEARALL, clearAll);
	menu.addSeparator();
	add(NPPM_INTERNAL_SCINTILLAFINDEROPENALL, openSelectedPath);
	// configuration items go at the bottom:
	menu.addSeparator();
	QAction* wrapAction = add(NPPM_INTERNAL_SCINTILLAFINDERWRAP, wrapLongLines);
	QAction* purgeAction = add(NPPM_INTERNAL_SCINTILLAFINDERPURGE, purgeForEverySearch);

	bool hasSomeSelectedText = _scintView->getSelectedTextCount() > 0;
	copyVerbatimAction->setEnabled(hasSomeSelectedText);
	clearAllAction->setEnabled(!_canBeVolatiled);
	purgeAction->setEnabled(!_canBeVolatiled);
	purgeAction->setCheckable(true);
	purgeAction->setChecked(_purgeBeforeEverySearch && !_canBeVolatiled);
	wrapAction->setCheckable(true);
	wrapAction->setChecked(_longLinesAreWrapped);

	menu.exec(globalPos);
}

// ============================================================================
// FindReplaceDlg: search engine
// ============================================================================

FindReplaceDlg::FindReplaceDlg()
{
	_env = &_options;
}

FindReplaceDlg::~FindReplaceDlg() = default;

void FindReplaceDlg::init(QWidget* parent, ScintillaEditView** ppEditView, ParentMessenger sendToParent)
{
	setParent(parent, Qt::Dialog);
	_ppEditView = ppEditView;
	_sendToParent = std::move(sendToParent);
}

bool FindReplaceDlg::processFindNext(const wchar_t* txt2find,
	const FindOption* options, FindStatus* oFindStatus, FindNextType findNextType /* = FINDNEXTTYPE_FINDNEXT */, MatchPosition* pMatch)
{
	if (oFindStatus)
		*oFindStatus = FSFound;

	if (!txt2find || !txt2find[0])
		return false;

	const FindOption *pOptions = options?options:_env;
	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

	// If txt2find contains non-ANSI characters and document type is ANSI, search cannot match
	if (isSearchUnicodeCharOnAnsi(txt2find))
	{
		if (oFindStatus)
			*oFindStatus = FSNotFound;

		// Show warning for regex (could work in theory, but we disallow it to prevent unexpected behavior)
		if (pOptions->_incrementalType == NotIncremental) //incremental search doesn't trigger messages
		{
			setStatusMessageNotFound(txt2find);

			// if the dialog is not shown, pass the focus to his parent(ie. Notepad++)
			if (!isVisible())
			{
				(*_ppEditView)->setFocus();
			}
			else
			{
				ctrl(IDFINDWHAT)->setFocus();
			}
		}
		return false;

	}

	(*_ppEditView)->execute(SCI_CALLTIPCANCEL);

	int stringSizeFind = static_cast<int>(wcslen(txt2find));
	wchar_t *pText = new wchar_t[stringSizeFind + 1];
	wcscpy_s(pText, stringSizeFind + 1, txt2find);

	if (pOptions->_searchType == FindExtended)
	{
		stringSizeFind = Searching::convertExtendedToString(txt2find, pText, stringSizeFind);
	}

	intptr_t docLength = (*_ppEditView)->execute(SCI_GETLENGTH);
	Sci_CharacterRangeFull cr = (*_ppEditView)->getSelection();


	//The search "zone" is relative to the selection, so search happens 'outside'
	intptr_t startPosition = cr.cpMax;
	intptr_t endPosition = docLength;


	if (pOptions->_whichDirection == DIR_UP)
	{
		//When searching upwards, start is the lower part, end the upper, for backwards search
		startPosition = cr.cpMax - 1;
		endPosition = 0;
	}

	if (FirstIncremental==pOptions->_incrementalType)
	{
		// the text to find is modified so use the current position
		startPosition = cr.cpMin;
		endPosition = docLength;

		if (pOptions->_whichDirection == DIR_UP)
		{
			//When searching upwards, start is the lower part, end the upper, for backwards search
			startPosition = cr.cpMax;
			endPosition = 0;
		}
	}
	else if (NextIncremental==pOptions->_incrementalType)
	{
		// text to find is not modified, so use current position +1
		startPosition = cr.cpMin + 1;
		endPosition = docLength;

		if (pOptions->_whichDirection == DIR_UP)
		{
			//When searching upwards, start is the lower part, end the upper, for backwards search
			startPosition = cr.cpMax - 1;
			endPosition = 0;
		}
	}

	int flags = Searching::buildSearchFlags(pOptions);
	switch (findNextType)
	{
		case FINDNEXTTYPE_FINDNEXT:
			flags |= SCFIND_REGEXP_EMPTYMATCH_ALL | SCFIND_REGEXP_SKIPCRLFASONE;
		break;

		case FINDNEXTTYPE_REPLACENEXT:
			flags |= SCFIND_REGEXP_EMPTYMATCH_NOTAFTERMATCH | SCFIND_REGEXP_SKIPCRLFASONE;
			break;

		case FINDNEXTTYPE_FINDNEXTFORREPLACE:
			flags |= SCFIND_REGEXP_EMPTYMATCH_ALL | SCFIND_REGEXP_EMPTYMATCH_ALLOWATSTART | SCFIND_REGEXP_SKIPCRLFASONE;
			break;
	}

	intptr_t start, end;
	intptr_t posFind;

	// Never allow a zero length match in the middle of a line end marker
	if ((*_ppEditView)->execute(SCI_GETCHARAT, startPosition - 1) == '\r'
		&& (*_ppEditView)->execute(SCI_GETCHARAT, startPosition) == '\n')
	{
		flags = (flags & ~SCFIND_REGEXP_EMPTYMATCH_MASK) | SCFIND_REGEXP_EMPTYMATCH_NONE;
	}

	(*_ppEditView)->execute(SCI_SETSEARCHFLAGS, flags);

	posFind = (*_ppEditView)->searchInTarget(pText, stringSizeFind, startPosition, endPosition);
	if (posFind == -1) //no match found in target, check if a new target should be used
	{
		if (pOptions->_isWrapAround)
		{
			//when wrapping, use the rest of the document (entire document is usable)
			if (pOptions->_whichDirection == DIR_DOWN)
			{
				startPosition = 0;
				endPosition = docLength;
				if (oFindStatus)
					*oFindStatus = FSEndReached;
			}
			else
			{
				startPosition = docLength;
				endPosition = 0;
				if (oFindStatus)
					*oFindStatus = FSTopReached;
			}

			//new target, search again
			posFind = (*_ppEditView)->searchInTarget(pText, stringSizeFind, startPosition, endPosition);
		}

		if (posFind == -1)
		{ // not found
			if (oFindStatus)
				*oFindStatus = FSNotFound;
			//failed, or failed twice with wrap
			if (pOptions->_incrementalType == NotIncremental) //incremental search doesn't trigger messages
			{
				wstring reasonMsg;
				bool isTheMostLaxMode = _options._isWrapAround && !_options._isMatchCase && !_options._isWholeWord;
				if (!isTheMostLaxMode)
				{
					reasonMsg = pNativeSpeaker->getLocalizedStrFromID("find-status-cannot-find-pebkac-maybe", noFoundPotentialReason);
				}
				setStatusMessageNotFound(txt2find, reasonMsg);

				// if the dialog is not shown, pass the focus to his parent(ie. Notepad++)
				if (!isVisible())
				{
					(*_ppEditView)->setFocus();
				}
				else
				{
					ctrl(IDFINDWHAT)->setFocus();
				}
			}
			delete [] pText;
			return false;
		}
	}
	else if (posFind == FIND_INVALID_REGULAR_EXPRESSION)
	{ // error
		setStatusbarMessageWithRegExprErr(*_ppEditView);
		delete[] pText;
		return false;
	}

	if (posFind == FIND_INVALID_REGULAR_EXPRESSION) // NPLX: the wrap-around search can also report it
	{
		setStatusbarMessageWithRegExprErr(*_ppEditView);
		delete[] pText;
		return false;
	}

	start =	posFind;
	end = (*_ppEditView)->execute(SCI_GETTARGETEND);

	setStatusbarMessage(L"", FSNoMessage);

	// to make sure the found result is visible:
	// prevent recording of absolute positioning commands issued in the process
	(*_ppEditView)->execute(SCI_STOPRECORD);
	Searching::displaySectionCentered(start, end, *_ppEditView, pOptions->_whichDirection == DIR_DOWN);
	// Show a calltip for a zero length match
	if (start == end)
	{
		const std::string msg = "^ " + pNativeSpeaker->getLocalizedStrFromID("find-regex-zero-length-match", std::string("zero length match"));
		(*_ppEditView)->showCallTip(start, msg);
	}
	if (_sendToParent && static_cast<MacroStatus>(_sendToParent(NPPM_GETCURRENTMACROSTATUS, 0, 0)) == MacroStatus::RecordInProgress)
		(*_ppEditView)->execute(SCI_STARTRECORD);

	delete [] pText;

	if (pMatch)
	{
		pMatch->start = start;
		pMatch->end = end;
	}

	return true;
}

// return value :
// true  : the text is replaced, and find the next occurrence
// false : the text2find is not found, so the text is NOT replace
//      || the text is replaced, and do NOT find the next occurrence
bool FindReplaceDlg::processReplace(const wchar_t *txt2find, const wchar_t *txt2replace, const FindOption *options)
{
	bool moreMatches;

	if (!txt2find || !txt2find[0] || !txt2replace)
		return false;

	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

	if ((*_ppEditView)->getCurrentBuffer()->isReadOnly())
	{
		wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replace-readonly", L"Replace: Cannot replace text. The current document is read only.");
		setStatusbarMessage(msg, FSNotFound);
		return false;
	}

	FindOption replaceOptions = options ? *options : *_env;
	replaceOptions._incrementalType = FirstIncremental;

	// If txt2find contains non-ANSI characters and document type is ANSI, search cannot match
	if (isSearchUnicodeCharOnAnsi(txt2find))
	{
		wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replace-not-found", L"Replace: no occurrence was found");

		msg += L" ";
		msg += getScopeInfoForStatusBar(&_options);

		setStatusbarMessage(msg, FSNotFound);

		return false;
	}
	else if (isSearchUnicodeCharOnAnsi(txt2replace))
	{
		wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replace-invalid-replace-chars", L"Replace: can't replace with non-ANSI text in ANSI document");
		setStatusbarMessage(msg, FSNotFound);
		return false;
	}

	Sci_CharacterRangeFull currentSelection = (*_ppEditView)->getSelection();
	FindStatus status;
	moreMatches = processFindNext(txt2find, &replaceOptions, &status, FINDNEXTTYPE_FINDNEXTFORREPLACE);

	if (moreMatches)
	{
		Sci_CharacterRangeFull nextFind = (*_ppEditView)->getSelection();

		// If the next find is the same as the last, then perform the replacement
		if (nextFind.cpMin == currentSelection.cpMin && nextFind.cpMax == currentSelection.cpMax)
		{
			bool isRegExp = replaceOptions._searchType == FindRegex;

			intptr_t start = currentSelection.cpMin;
			intptr_t replacedLen = 0;
			if (isRegExp)
			{
				replacedLen = (*_ppEditView)->replaceTargetRegExMode(txt2replace);
			}
			else
			{
				if (replaceOptions._searchType == FindExtended)
				{
					int stringSizeReplace = static_cast<int>(wcslen(txt2replace));
					wchar_t *pText2ReplaceExtended = new wchar_t[stringSizeReplace + 1];
					Searching::convertExtendedToString(txt2replace, pText2ReplaceExtended, stringSizeReplace);

					replacedLen = (*_ppEditView)->replaceTarget(pText2ReplaceExtended);

					delete[] pText2ReplaceExtended;
				}
				else
				{
					replacedLen = (*_ppEditView)->replaceTarget(txt2replace);
				}
			}
			(*_ppEditView)->execute(SCI_SETSEL, start + replacedLen, start + replacedLen);

			NppParameters& nppParam = NppParameters::getInstance();
			const NppGUI& nppGui = nppParam.getNppGUI();

			if (nppGui._replaceStopsWithoutFindingNext)
			{
				wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replaced-without-continuing", L"Replace: 1 occurrence was replaced.");
				setStatusbarMessage(msg, FSMessage);
			}
			else
			{
				// Do the next find
				moreMatches = processFindNext(txt2find, &replaceOptions, &status, FINDNEXTTYPE_REPLACENEXT);

				if (status == FSEndReached)
				{
					wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replace-end-reached", FIND_STATUS_REPLACE_END_REACHED_TEXT);
					setStatusbarMessage(msg, FSEndReached);
				}
				else if (status == FSTopReached)
				{
					wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replace-top-reached", FIND_STATUS_REPLACE_TOP_REACHED_TEXT);
					setStatusbarMessage(msg, FSTopReached);
				}
				else
				{
					wstring msg;
					if (moreMatches)
						msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replaced-next-found", L"Replace: 1 occurrence was replaced. The next occurrence found.");
					else
						msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replaced-next-not-found", L"Replace: 1 occurrence was replaced. No more occurrences were found.");

					setStatusbarMessage(msg, FSMessage);
				}
			}
		}
	}
	else
	{
		if (_statusbarTooltipMsg.empty()) // Tooltip message non-empty means there's a find problem - so we keep the message as it is and not erase it
		{
			wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replace-not-found", L"Replace: no occurrence was found");

			msg += L" ";
			msg += getScopeInfoForStatusBar(&_options);

			wstring reasonMsg;
			bool isTheMostLaxMode = _options._isWrapAround && !_options._isMatchCase && !_options._isWholeWord;
			if (!isTheMostLaxMode)
			{
				reasonMsg = pNativeSpeaker->getLocalizedStrFromID("find-status-cannot-find-pebkac-maybe", noFoundPotentialReason);
			}
			setStatusbarMessage(msg, FSNotFound, reasonMsg);
		}
	}

	return moreMatches;
}



int FindReplaceDlg::markAll(const wchar_t *txt2find, int styleID)
{
	const NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	FindOption markAllOpt;

	markAllOpt._isMatchCase = nppGUI._markAllCaseSensitive;
	markAllOpt._isWholeWord = nppGUI._markAllWordOnly;
	markAllOpt._str2Search = txt2find;

	int nbFound = processAll(ProcessMarkAllExt, &markAllOpt, true, NULL, styleID);
	return nbFound;
}


int FindReplaceDlg::markAllInc(FindOption *opt)
{
	int nbFound = processAll(ProcessMarkAll_IncSearch, opt,  true);
	return nbFound;
}

int FindReplaceDlg::processAll(ProcessOperation op, FindOption* opt,
	bool isEntire, const FindersInfo* pFindersInfo, int colourStyleID, std::vector<MatchPosition>* pMatches)
{
	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	if (op == ProcessReplaceAll && (*_ppEditView)->getCurrentBuffer()->isReadOnly())
	{
		wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replaceall-readonly", L"Replace All: Cannot replace text. The current document is read only.");
		setStatusbarMessage(msg, FSNotFound);
		return 0;
	}

	FindOption* pOptions = opt ? opt : _env;
	const wchar_t* txt2find = pOptions->_str2Search.c_str();
	const wchar_t* txt2replace = pOptions->_str4Replace.c_str();

	// If txt2find contains non-ANSI characters and document type is ANSI, search cannot match
	if (isSearchUnicodeCharOnAnsi(txt2find))
	{
		return 0;
	}

	// If txt2replace contains non-ANSI characters and document type is ANSI, the match cannot be replaced
	if (isSearchUnicodeCharOnAnsi(txt2replace))
	{
		wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replace-invalid-replace-chars", L"Replace: can't replace with non-ANSI text in ANSI document");
		setStatusbarMessage(msg, FSNotFound);
		return 0;
	}

	// Turn OFF all the notification of modification (SCN_MODIFIED) for the sake of performance
	LRESULT notifFlag = (*_ppEditView)->execute(SCI_GETMODEVENTMASK);
	(*_ppEditView)->execute(SCI_SETMODEVENTMASK, 0);

	Sci_CharacterRangeFull cr = (*_ppEditView)->getSelection();
	size_t docLength = (*_ppEditView)->execute(SCI_GETLENGTH);

	// Default :
	//        direction : down
	//        begin at : 0
	//        end at : end of doc
	size_t startPosition = 0;
	size_t endPosition = docLength;

	bool direction = pOptions->_whichDirection;

	//first try limiting scope by direction
	if (direction == DIR_UP)
	{
		startPosition = 0;
		endPosition = cr.cpMax;
	}
	else
	{
		startPosition = cr.cpMin;
		endPosition = docLength;
	}

	//then adjust scope if the full document needs to be changed
	if (op == ProcessCountAll && pOptions->_isInSelection)
	{
		startPosition = cr.cpMin;
		endPosition = cr.cpMax;
	}
	else if (pOptions->_isWrapAround || isEntire)	//entire document needs to be scanned
	{
		startPosition = 0;
		endPosition = docLength;
	}

	//then readjust scope if the selection override is active and allowed
	if ((pOptions->_isInSelection) && ((op == ProcessMarkAll) || ((op == ProcessReplaceAll || op == ProcessFindAll) && (!isEntire))))
		//if selection limiter and either mark all or replace all or find all w/o entire document override
	{
		startPosition = cr.cpMin;
		endPosition = cr.cpMax;
	}

	if ((op == ProcessMarkAllExt) && (colourStyleID != -1))
	{
		startPosition = 0;
		endPosition = docLength;
	}

	FindReplaceInfo findReplaceInfo;
	findReplaceInfo._txt2find = txt2find;
	findReplaceInfo._txt2replace = txt2replace;
	findReplaceInfo._startRange = startPosition;
	findReplaceInfo._endRange = endPosition;

	int nbProcessed = processRange(op, findReplaceInfo, pFindersInfo, pOptions, colourStyleID, nullptr, pMatches);

	// Turn ON the notifications after operations
	(*_ppEditView)->execute(SCI_SETMODEVENTMASK, notifFlag);
	if (op == ProcessReplaceAll && nbProcessed > 0) // All the notification of modification (SCN_MODIFIED) were removed during the operations, so we set modified status true here
	{
		Buffer* buf = (*_ppEditView)->getCurrentBuffer();
		buf->setModifiedStatus(true);
		if (_sendToParent)
			_sendToParent(NPPM_INTERNAL_DOCMODIFIEDBYREPLACEALL, reinterpret_cast<uptr_t>(buf), 0);
	}


	if (nbProcessed == FIND_INVALID_REGULAR_EXPRESSION)
		return FIND_INVALID_REGULAR_EXPRESSION;

	if (nbProcessed > 0 && op == ProcessReplaceAll && pOptions->_isInSelection)
	{
		size_t newDocLength = (*_ppEditView)->execute(SCI_GETLENGTH);
		endPosition += newDocLength - docLength;
		(*_ppEditView)->execute(SCI_SETSELECTION, endPosition, startPosition);
		(*_ppEditView)->execute(SCI_SCROLLRANGE, startPosition, endPosition);
		if (startPosition == endPosition)
		{
			const NppGUI& nppGui = (NppParameters::getInstance()).getNppGUI();
			if (nppGui._inSelectionAutocheckThreshold != 0)
			{
				setChecked(IDC_IN_SELECTION_CHECK, false);
			}
			enableFindDlgItem(IDC_IN_SELECTION_CHECK, false);
		}
	}
	return nbProcessed;
}

int FindReplaceDlg::processRange(ProcessOperation op, FindReplaceInfo& findReplaceInfo, const FindersInfo* pFindersInfo,
	FindOption* opt, int colourStyleID, ScintillaEditView* view2Process, std::vector<MatchPosition>* pMatches)
{
	int nbProcessed = 0;

	if (!isCreated() && !findReplaceInfo._txt2find)
		return 0;

	if (!_ppEditView)
		return 0;

	ScintillaEditView *pEditView = *_ppEditView;

	if (view2Process)
		pEditView = view2Process;

	if ((op == ProcessReplaceAll) && pEditView->getCurrentBuffer()->isReadOnly())
		return nbProcessed;

	if (findReplaceInfo._startRange == findReplaceInfo._endRange)
		return nbProcessed;

	const FindOption *pOptions = opt ? opt : _env;

	LRESULT stringSizeFind = 0;
	LRESULT stringSizeReplace = 0;

	wchar_t *pTextFind = NULL;
	if (!findReplaceInfo._txt2find)
	{
		wstring str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
		stringSizeFind = str2Search.length();
		pTextFind = new wchar_t[stringSizeFind + 1];
		wcscpy_s(pTextFind, stringSizeFind + 1, str2Search.c_str());
	}
	else
	{
		stringSizeFind = wcslen(findReplaceInfo._txt2find);
		pTextFind = new wchar_t[stringSizeFind + 1];
		wcscpy_s(pTextFind, stringSizeFind + 1, findReplaceInfo._txt2find);
	}

	if (!pTextFind[0])
	{
		delete [] pTextFind;
		return nbProcessed;
	}

	wchar_t *pTextReplace = NULL;
	if (op == ProcessReplaceAll)
	{
		if (!findReplaceInfo._txt2replace)
		{
			wstring str2Replace = getTextFromCombo(comboBox(IDREPLACEWITH));
			stringSizeReplace = str2Replace.length();
			pTextReplace = new wchar_t[stringSizeReplace + 1];
			wcscpy_s(pTextReplace, stringSizeReplace + 1, str2Replace.c_str());
		}
		else
		{
			stringSizeReplace = wcslen(findReplaceInfo._txt2replace);
			pTextReplace = new wchar_t[stringSizeReplace + 1];
			wcscpy_s(pTextReplace, stringSizeReplace + 1, findReplaceInfo._txt2replace);
		}
	}

	if (pOptions->_searchType == FindExtended)
	{
		stringSizeFind = Searching::convertExtendedToString(pTextFind, pTextFind, static_cast<int32_t>(stringSizeFind));
		if (op == ProcessReplaceAll)
			stringSizeReplace = Searching::convertExtendedToString(pTextReplace, pTextReplace, static_cast<int32_t>(stringSizeReplace));
	}

	bool isRegExp = pOptions->_searchType == FindRegex;
	int flags = Searching::buildSearchFlags(pOptions) | SCFIND_REGEXP_SKIPCRLFASONE;

	// Allow empty matches, but not immediately after previous match, so Count and Mark All
	// (like Replace All and Find All already do) don't silently skip zero-length matches.
	if (op == ProcessReplaceAll || op == ProcessFindAll || op == ProcessCountAll || op == ProcessMarkAll)
		flags |= SCFIND_REGEXP_EMPTYMATCH_NOTAFTERMATCH;



	if (op == ProcessMarkAll && colourStyleID == -1)	//if marking, check if purging is needed
	{
		if (_env->_doPurge)
		{
			clearMarks(*_env);
		}
	}

	intptr_t targetStart = 0;

	//Initial range for searching
	pEditView->execute(SCI_SETSEARCHFLAGS, flags);


	bool findAllFileNameAdded = false;

	// A temporary string which is used to populate the search result window
	std::unique_ptr<std::string> text2AddUtf8(new std::string());
	size_t indexBuffer = 0;

	while (targetStart >= 0)
	{
		targetStart = pEditView->searchInTarget(pTextFind, stringSizeFind, findReplaceInfo._startRange, findReplaceInfo._endRange);

		// If we've not found anything, just break out of the loop
		if (targetStart == -1)
			break;

		if (targetStart == FIND_INVALID_REGULAR_EXPRESSION)
		{
			delete[] pTextFind; // NPLX: upstream leaks here
			delete[] pTextReplace;
			return FIND_INVALID_REGULAR_EXPRESSION;
		}

		intptr_t targetEnd = pEditView->execute(SCI_GETTARGETEND);

		if (targetEnd > findReplaceInfo._endRange)
		{
			//we found a result but outside our range, therefore do not process it
			break;
		}

		if (pMatches)
		{
			MatchPosition match;
			match.start = targetStart;
			match.end = targetEnd;
			pMatches->push_back(match);
		}

		intptr_t foundTextLen = targetEnd - targetStart;
		intptr_t replaceDelta = 0;
		bool processed = true;

		switch (op)
		{
			case ProcessFindAll:
			{
				const wchar_t *pFileName = L"";
				if (pFindersInfo && pFindersInfo->_pFileName)
					pFileName = pFindersInfo->_pFileName;

				if (!findAllFileNameAdded)	//add new filetitle in hits if we haven't already
				{
					_pFinder->addFileNameTitle(pFileName);
					findAllFileNameAdded = true;
				}

				auto totalLineNumber = pEditView->execute(SCI_GETLINECOUNT);
				auto lineNumber = pEditView->execute(SCI_LINEFROMPOSITION, targetStart);
				intptr_t lend = pEditView->execute(SCI_GETLINEENDPOSITION, lineNumber);
				intptr_t lstart = pEditView->execute(SCI_POSITIONFROMLINE, lineNumber);
				intptr_t nbChar = lend - lstart;

				// use the static buffer
				wchar_t lineBuf[SC_SEARCHRESULT_LINEBUFFERMAXLENGTH]{};

				if (nbChar > SC_SEARCHRESULT_LINEBUFFERMAXLENGTH - 3)
				{
					lend = lstart + SC_SEARCHRESULT_LINEBUFFERMAXLENGTH - 4;
					nbChar = SC_SEARCHRESULT_LINEBUFFERMAXLENGTH - 4;
				}

				intptr_t start_mark = targetStart - lstart;
				intptr_t end_mark = targetEnd - lstart;

				pEditView->getGenericText(lineBuf, SC_SEARCHRESULT_LINEBUFFERMAXLENGTH, lstart, lend, &start_mark, &end_mark, &nbChar);

				lineBuf[nbChar++] = '\r';
				lineBuf[nbChar++] = '\n';

				SearchResultMarkingLine srml;
				srml._segmentPostions.push_back(std::pair<intptr_t, intptr_t>(start_mark, end_mark));
				text2AddUtf8->append(_pFinder->foundLine(FoundInfo(targetStart, targetEnd, lineNumber + 1, pFileName), srml, lineBuf, nbChar, totalLineNumber));

				if (text2AddUtf8->length() > FINDTEMPSTRING_MAXSIZE)
				{
					_pFinder->setFinderReadOnly(false);
					_pFinder->_scintView->execute(SCI_ADDTEXT, text2AddUtf8->length(), reinterpret_cast<LPARAM>(text2AddUtf8->c_str()));
					_pFinder->setFinderReadOnly(true);
					text2AddUtf8->clear();
				}
				break;
			}

			case ProcessFindInFinder:
			{
				if (!pFindersInfo || !pFindersInfo->_pSourceFinder || !pFindersInfo->_pDestFinder)
					break;

				const wchar_t *pFileName = pFindersInfo->_pFileName ? pFindersInfo->_pFileName : L"";

				auto totalLineNumber = pEditView->execute(SCI_GETLINECOUNT);
				auto lineNumber = pEditView->execute(SCI_LINEFROMPOSITION, targetStart);
				intptr_t lend = pEditView->execute(SCI_GETLINEENDPOSITION, lineNumber);
				intptr_t lstart = pEditView->execute(SCI_POSITIONFROMLINE, lineNumber);
				intptr_t nbChar = lend - lstart;

				// use the static buffer
				wchar_t lineBuf[SC_SEARCHRESULT_LINEBUFFERMAXLENGTH]{};

				if (nbChar > SC_SEARCHRESULT_LINEBUFFERMAXLENGTH - 3)
				{
					lend = lstart + SC_SEARCHRESULT_LINEBUFFERMAXLENGTH - 4;
					nbChar = SC_SEARCHRESULT_LINEBUFFERMAXLENGTH - 4;
				}

				intptr_t start_mark = targetStart - lstart;
				intptr_t end_mark = targetEnd - lstart;

				pEditView->getGenericText(lineBuf, SC_SEARCHRESULT_LINEBUFFERMAXLENGTH, lstart, lend, &start_mark, &end_mark, &nbChar);

				lineBuf[nbChar++] = '\r';
				lineBuf[nbChar++] = '\n';

				SearchResultMarkingLine srml;
				srml._segmentPostions.push_back(std::pair<intptr_t, intptr_t>(start_mark, end_mark));

				processed = (!pOptions->_isMatchLineNumber) || (pFindersInfo->_pSourceFinder->canFind(pFileName, lineNumber + 1, &indexBuffer));
				if (processed)
				{
					if (!findAllFileNameAdded)	//add new filetitle in hits if we haven't already
					{
						pFindersInfo->_pDestFinder->addFileNameTitle(pFileName);
						findAllFileNameAdded = true;
					}
					text2AddUtf8->append(pFindersInfo->_pDestFinder->foundLine(FoundInfo(targetStart, targetEnd, lineNumber + 1, pFileName), srml, lineBuf, nbChar, totalLineNumber));

					if (text2AddUtf8->length() > FINDTEMPSTRING_MAXSIZE)
					{
						pFindersInfo->_pDestFinder->setFinderReadOnly(false);
						pFindersInfo->_pDestFinder->_scintView->execute(SCI_ADDTEXT, text2AddUtf8->length(), reinterpret_cast<LPARAM>(text2AddUtf8->c_str()));
						pFindersInfo->_pDestFinder->setFinderReadOnly(true);
						text2AddUtf8->clear();
					}
				}
				break;
			}

			case ProcessReplaceAll:
			{
				intptr_t replacedLength;
				if (isRegExp)
					replacedLength = pEditView->replaceTargetRegExMode(pTextReplace);
				else
					replacedLength = pEditView->replaceTarget(pTextReplace);

				replaceDelta = replacedLength - foundTextLen;
				break;
			}

			case ProcessMarkAll:
			{
				// Zero-length matches are possible (e.g. "^$"), but scintilla gets upset if we call
				// INDICATORFILLRANGE with a length of 0, so we protect against it here.
				// At least in version 2.27, after calling INDICATORFILLRANGE with length 0, further indicators
				// on the same line would simply not be shown.  This may have been fixed in later version of Scintilla.
				if (foundTextLen > 0)
				{
					pEditView->execute(SCI_SETINDICATORCURRENT, SCE_UNIVERSAL_FOUND_STYLE);
					pEditView->execute(SCI_INDICATORFILLRANGE,  targetStart, foundTextLen);
				}
				else if (foundTextLen == 0 && opt->_searchType == FindRegex)
				{
					opt->_regexEmptyStringFound = true;
				}

				if (_env->_doMarkLine)
				{
					auto lineNumber = pEditView->execute(SCI_LINEFROMPOSITION, targetStart);
					// For a zero-length match, "targetEnd - 1" would be the line *before* targetStart
					// (or even underflow at document start), so fall back to lineNumber in that case.
					auto lineNumberEnd = (foundTextLen > 0) ? pEditView->execute(SCI_LINEFROMPOSITION, targetEnd - 1) : lineNumber;

					for (auto i = lineNumber; i <= lineNumberEnd; ++i)
					{
						auto state = pEditView->execute(SCI_MARKERGET, i);

						if (!(state & (1 << MARK_BOOKMARK)))
							pEditView->execute(SCI_MARKERADD, i, MARK_BOOKMARK);
					}
				}

				break;
			}

			case ProcessMarkAllExt:
			{
				// See comment by ProcessMarkAll
				if (foundTextLen > 0)
				{
					pEditView->execute(SCI_SETINDICATORCURRENT,  colourStyleID);
					pEditView->execute(SCI_INDICATORFILLRANGE,  targetStart, foundTextLen);
				}
				break;
			}

			case ProcessHighLightAll:
			{
				// See comment by ProcessMarkAll
				if (foundTextLen > 0)
				{
					pEditView->execute(SCI_SETINDICATORCURRENT,  SCE_UNIVERSAL_FOUND_STYLE_SMART);
					pEditView->execute(SCI_INDICATORFILLRANGE,  targetStart, foundTextLen);
				}
				break;
			}

			case ProcessMarkAll_IncSearch:
			{
				// See comment by ProcessMarkAll
				if (foundTextLen > 0)
				{
					pEditView->execute(SCI_SETINDICATORCURRENT,  SCE_UNIVERSAL_FOUND_STYLE_INC);
					pEditView->execute(SCI_INDICATORFILLRANGE,  targetStart, foundTextLen);
				}
				break;
			}

			case ProcessCountAll:
			{
				if (foundTextLen == 0 && opt->_searchType == FindRegex)
					opt->_regexEmptyStringFound = true;

				break;
			}

			default:
			{
				delete[] pTextFind;
				if (pTextReplace)
					delete[] pTextReplace;
				return nbProcessed;
			}

		}
		if (processed) ++nbProcessed;

		// After the processing of the last string occurrence the search loop should be stopped
		// This helps to avoid the endless replacement during the EOL ("$") searching
		if (targetStart + foundTextLen == findReplaceInfo._endRange)
			break;

		findReplaceInfo._startRange = targetStart + foundTextLen + replaceDelta;		//search from result onwards
		findReplaceInfo._endRange += replaceDelta;									//adjust end of range in case of replace
	}

	delete[] pTextFind;
	if (pTextReplace)
		delete[] pTextReplace;

	if (nbProcessed > 0)
	{
		Finder *pFinder = nullptr;
		if (op == ProcessFindAll)
		{
			_pFinder->setFinderReadOnly(false);
			_pFinder->_scintView->execute(SCI_ADDTEXT, text2AddUtf8->length(), reinterpret_cast<LPARAM>(text2AddUtf8->c_str()));
			_pFinder->setFinderReadOnly(true);
			text2AddUtf8->clear();
			pFinder = _pFinder;
		}
		else if (op == ProcessFindInFinder)
		{
			if (pFindersInfo && pFindersInfo->_pDestFinder)
			{
				pFindersInfo->_pDestFinder->setFinderReadOnly(false);
				pFindersInfo->_pDestFinder->_scintView->execute(SCI_ADDTEXT, text2AddUtf8->length(), reinterpret_cast<LPARAM>(text2AddUtf8->c_str()));
				pFindersInfo->_pDestFinder->setFinderReadOnly(true);
				text2AddUtf8->clear();
				pFinder = pFindersInfo->_pDestFinder;
			}
			else
				pFinder = _pFinder;
		}

		if (pFinder != nullptr)
			pFinder->addFileHitCount(nbProcessed);
	}
	return nbProcessed;
}

void FindReplaceDlg::replaceAllInOpenedDocs()
{
	_sendToParent(WM_REPLACEALL_INOPENEDDOC, 0, 0);
}

void FindReplaceDlg::findAllIn(InWhat op)
{
	bool justCreated = false;
	if (!_pFinder)
	{
		NppParameters& nppParam = NppParameters::getInstance();

		_pFinder = new Finder(parentWidget());
		_pFinder->init(_ppEditView, _sendToParent);
		_pFinder->setVolatiled(false);

		NativeLangSpeaker *pNativeSpeaker = nppParam.getNativeLangSpeaker();
		wstring text = pNativeSpeaker->getLocalizedStrFromID("find-result-caption", L"");

		if (!text.empty())
		{
			_findResTitle = text;
			_pFinder->setWindowTitle(QString::fromStdWString(_findResTitle));
		}

		// upstream: NPPM_DMMREGASDCKDLG with DWS_DF_CONT_BOTTOM
		_sendToParent(NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<sptr_t>(_pFinder));

		_pFinder->setFinderReadOnly(true);
		_pFinder->_scintView->execute(SCI_SETCODEPAGE, SC_CP_UTF8);
		_pFinder->_scintView->execute(SCI_USEPOPUP, SC_POPUP_NEVER);
		_pFinder->_scintView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF); // Turn off the modification event
		_pFinder->_scintView->execute(SCI_SETUNDOCOLLECTION, false);	//don't store any undo information
		_pFinder->_scintView->execute(SCI_SETCARETWIDTH, 1);
		_pFinder->_scintView->showMargin(ScintillaEditView::_SC_MARGE_FOLDER, true);

		_pFinder->_scintView->execute(SCI_SETUSETABS, true);
		_pFinder->_scintView->execute(SCI_SETTABWIDTH, 4);

		const NppGUI& nppGUI = nppParam.getNppGUI();
		_pFinder->_longLinesAreWrapped = nppGUI._finderLinesAreCurrentlyWrapped;
		_pFinder->_scintView->wrap(_pFinder->_longLinesAreWrapped);
		_pFinder->_scintView->setWrapMode(LINEWRAP_INDENT);
		_pFinder->_scintView->showWrapSymbol(true);

		_pFinder->_purgeBeforeEverySearch = nppGUI._finderPurgeBeforeEverySearch;

		// allow user to start selecting as a stream block, then switch to a column block by adding Alt keypress
		_pFinder->_scintView->execute(SCI_SETMOUSESELECTIONRECTANGULARSWITCH, true);

		// overwrite some default settings
		_pFinder->_scintView->showMargin(ScintillaEditView::_SC_MARGE_SYMBOL, false);
		_pFinder->_scintView->showMargin(ScintillaEditView::_SC_MARGE_LINENUMBER, false); // NPLX: init() shows it
		_pFinder->_scintView->setMakerStyle(FOLDER_STYLE_SIMPLE);

		_pFinder->setFinderStyle();
		_pFinder->setFinderStyleForNpc();

		_pFinder->display(false);
		justCreated = true;

	}

	if (_pFinder->_purgeBeforeEverySearch)
	{
		_pFinder->removeAll();
	}

	if (justCreated)
	{
		// Send the address of _MarkingsStruct to the lexer
		char ptrword[sizeof(void*) * 2 + 3];
		snprintf(ptrword, sizeof(ptrword), "%p", static_cast<void*>(&_pFinder->_markingsStruct));
		_pFinder->_scintView->execute(SCI_SETPROPERTY, reinterpret_cast<WPARAM>("@MarkingsStruct"), reinterpret_cast<LPARAM>(ptrword));

		//enable "Search Results Window" under Search Menu
		_sendToParent(NPPM_INTERNAL_ENABLECOMMAND, IDM_FOCUS_ON_FOUND_RESULTS, TRUE);
		_sendToParent(NPPM_INTERNAL_ENABLECOMMAND, IDM_SEARCH_GOTONEXTFOUND, TRUE);
		_sendToParent(NPPM_INTERNAL_ENABLECOMMAND, IDM_SEARCH_GOTOPREVFOUND, TRUE);
	}

	bool toRTL = (*_ppEditView)->isTextDirectionRTL();
	bool isRTL = _pFinder->_scintView->isTextDirectionRTL();

	if ((toRTL && !isRTL) || (!toRTL && isRTL))
		_pFinder->_scintView->changeTextDirection(toRTL);

	int cmdid = 0;
	if (op == ALL_OPEN_DOCS)
		cmdid = WM_FINDALL_INOPENEDDOC;
	else if (op == FILES_IN_DIR)
		cmdid = WM_FINDINFILES;
	else if (op == FILES_IN_PROJECTS)
		cmdid = WM_FINDINPROJECTS;
	else if ((op == CURRENT_DOC) || (op == CURR_DOC_SELECTION))
		cmdid = WM_FINDALL_INCURRENTDOC;

	if (!cmdid) return;

	bool limitSearchScopeToSelection = op == CURR_DOC_SELECTION;
	if (_sendToParent(cmdid, static_cast<uptr_t>(limitSearchScopeToSelection ? 1 : 0), 0))
	{
		wstring text = _pFinder->getHitsString(_findAllResult);
		wcscpy_s(_findAllResultStr, sizeof(_findAllResultStr) / sizeof(wchar_t), text.c_str());
		_pFinder->setWindowTitle(QString::fromStdWString((_findResTitle.empty() ? std::wstring(L"Search results") : _findResTitle) + L" - " + text)); // upstream: pszAddInfo

		if (_findAllResult)
		{
			focusOnFinder();
		}
		else
		{
			// Show finder
			_pFinder->display();
			activateWindow(); // no hits
		}
	}
	else // error - search folder doesn't exist
		ctrl(IDD_FINDINFILES_DIR_COMBO)->setFocus();
}

Finder* FindReplaceDlg::createFinder()
{
	// NPLX_TODO(Phase 4): FindInFinderDlg creates additional finders (upstream WM_FINDALL_INCURRENTFINDER)
	return nullptr;
}

bool FindReplaceDlg::removeFinder(Finder *finder2remove)
{
	for (vector<Finder *>::iterator i = _findersOfFinder.begin(); i != _findersOfFinder.end(); ++i)
	{
		if (*i == finder2remove)
		{
			finder2remove->deleteLater();
			_findersOfFinder.erase(i);
			return true;
		}
	}
	return false;
}

int FindReplaceDlg::regexBackwardMsgBox()
{
	NppParameters& nppParam = NppParameters::getInstance();

	const int msgboxID = nppParam.getNativeLangSpeaker()->messageBox("FindRegexBackwardDisabled",
		L"By default, backward regex searching is disabled due to potentially unexpected results. " \
		L"To perform a backward search, open the Find dialog and select either normal or extended search mode instead of regular expression.\r\n" \
		L"Press the OK button to open the Find dialog or set focus on it.\r\n" \
		L"\r\n" \
		L"If you require the backward regex searching feature, consult the user manual for instructions on enabling it.",
		L"Regex backward search disabled",
		MB_OKCANCEL | MB_APPLMODAL | MB_ICONINFORMATION);

	switch (msgboxID)
	{
		case IDOK:
		{
			doDialog(FIND_DLG, nppParam.getNativeLangSpeaker()->isRTL());
			ctrl(IDREGEXP)->setFocus();
			break;
		}

		case IDCANCEL:
		default:
			break;
	}

	return msgboxID;
}

void FindReplaceDlg::setSearchText(const wchar_t * txt2find)
{
	QComboBox* hCombo = comboBox(IDFINDWHAT);
	if (!hCombo)
		return;
	if (txt2find && txt2find[0])
	{
		// We got a valid search string
		hCombo->setEditText(QString::fromWCharArray(txt2find));
	}
	hCombo->lineEdit()->selectAll(); // select all text - fast edit
}

void FindReplaceDlg::getPatterns(vector<wstring> & patternVect)
{
	cutString(_env->_filters.c_str(), patternVect);
}

void FindReplaceDlg::getAndValidatePatterns(vector<wstring> & patternVect)
{
	getPatterns(patternVect);
	if (patternVect.size() == 0)
	{
		setFindInFilesDirFilter(NULL, L"*.*");
		getPatterns(patternVect);
	}
	else if (allPatternsAreExclusion(patternVect))
	{
		patternVect.insert(patternVect.begin(), L"*.*");
	}
}

void FindReplaceDlg::setFindInFilesDirFilter(const wchar_t *dir, const wchar_t *filters)
{
	if (dir)
	{
		_env->_directory = dir;
		if (QComboBox* combo = comboBox(IDD_FINDINFILES_DIR_COMBO))
			combo->setEditText(QString::fromWCharArray(dir));
	}

	if (filters)
	{
		_env->_filters = filters;
		if (QComboBox* combo = comboBox(IDD_FINDINFILES_FILTERS_COMBO))
			combo->setEditText(QString::fromWCharArray(filters));
	}
}

void FindReplaceDlg::setStatusbarMessage(const wstring & msg, FindStatus status, const wstring& tooltipMsg)
{
	_statusbarTooltipMsg = tooltipMsg;

	if (status == FSNotFound)
	{
		if (!NppParameters::getInstance().getNppGUI()._muteSounds)
			QApplication::beep();

		QApplication::alert(isVisible() ? static_cast<QWidget*>(this) : parentWidget()); // NPLX: was FlashWindowEx
	}
	else if (status == FSTopReached || status == FSEndReached)
	{
		if (!isVisible() && parentWidget())
			QApplication::alert(parentWidget());
	}

	if (_statusBar)
	{
		_statusbarFindStatus = status;

		// upstream drawStatusBarItem: colour by status
		NppParameters& nppParamInst = NppParameters::getInstance();
		int colourIndex = -1;
		if (_statusbarFindStatus == FSNotFound)
			colourIndex = 0;
		else if (_statusbarFindStatus == FSMessage)
			colourIndex = 1;
		else if (_statusbarFindStatus == FSTopReached || _statusbarFindStatus == FSEndReached)
			colourIndex = 2;
		else if (_statusbarFindStatus == FSWarning)
			colourIndex = 3;

		QString styleSheet;
		if (colourIndex != -1)
		{
			HLSColour hls(nppParamInst.getFindDlgStatusMsgColor(colourIndex));
			COLORREF fgColor = NppDarkMode::isEnabled() ? hls.toRGB4DarkMod() : nppParamInst.getFindDlgStatusMsgColor(colourIndex);
			styleSheet = QStringLiteral("color: #%1%2%3;")
				.arg(GetRValue(fgColor), 2, 16, QLatin1Char('0'))
				.arg(GetGValue(fgColor), 2, 16, QLatin1Char('0'))
				.arg(GetBValue(fgColor), 2, 16, QLatin1Char('0'));
		}
		_statusBar->setStyleSheet(styleSheet);
		_statusBar->setText(QString::fromStdWString(msg));
		_statusBar->setToolTip(QString::fromStdWString(tooltipMsg));
	}
}

void FindReplaceDlg::setStatusbarMessageWithRegExprErr(ScintillaEditView* pEditView)
{
	if (!pEditView)
		return;

	char msg[511] {};
	pEditView->execute(SCI_GETBOOSTREGEXERRMSG, _countof(msg), reinterpret_cast<LPARAM>(msg));

	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	std::wstring result = pNativeSpeaker->getLocalizedStrFromID("find-status-invalid-re", L"Find: Invalid Regular Expression");
	string s = msg;

	setStatusbarMessage(result, FSNotFound, string2wstring(s, CP_UTF8));
}

void FindReplaceDlg::setStatusMessageNotFound(const std::wstring& textNotFound, const std::wstring& tooltipMsg)
{
	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	wstring warningMsg = pNativeSpeaker->getLocalizedStrFromID("find-status-cannot-find", L"Find: Can't find the text \"$STR_REPLACE$\"");
	wstring newTxt2find = textNotFound; // NPLX: no '&' doubling, the Qt label has no mnemonics

	if (newTxt2find.length() > 32) // truncate the search string to display, if the search string is too long
	{
		newTxt2find.erase(28);
		newTxt2find += L"...";
	}

	warningMsg = stringReplace(warningMsg, L"$STR_REPLACE$", newTxt2find);

	warningMsg += L" ";
	warningMsg += getScopeInfoForStatusBar(&_options);

	setStatusbarMessage(warningMsg, FSNotFound, tooltipMsg);
}

wstring FindReplaceDlg::getScopeInfoForStatusBar(FindOption const *pFindOpt) const
{
	wstring scope;

	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

	if (pFindOpt->_isInSelection)
	{
		scope += pNativeSpeaker->getLocalizedStrFromID("find-status-scope-selection", L"in selected text");
	}
	else if (pFindOpt->_isWrapAround)
	{
		scope += pNativeSpeaker->getLocalizedStrFromID("find-status-scope-all", L"in entire file");
	}
	else if (pFindOpt->_whichDirection == DIR_UP)
	{
		scope += pNativeSpeaker->getLocalizedStrFromID("find-status-scope-backward", L"from start-of-file to caret");
	}
	else
	{
		scope += pNativeSpeaker->getLocalizedStrFromID("find-status-scope-forward", L"from caret to end-of-file");
	}

	return scope;
}

void FindReplaceDlg::clearMarks(const FindOption& opt)
{
	if (opt._isInSelection)
	{
		Sci_CharacterRangeFull cr = (*_ppEditView)->getSelection();

		intptr_t startPosition = cr.cpMin;
		intptr_t endPosition = cr.cpMax;

		(*_ppEditView)->execute(SCI_SETINDICATORCURRENT, SCE_UNIVERSAL_FOUND_STYLE);
		(*_ppEditView)->execute(SCI_INDICATORCLEARRANGE, startPosition, endPosition - startPosition);

		if (opt._doMarkLine)
		{
			auto lineNumber = (*_ppEditView)->execute(SCI_LINEFROMPOSITION, startPosition);
			auto lineNumberEnd = (*_ppEditView)->execute(SCI_LINEFROMPOSITION, endPosition - 1);

			for (auto i = lineNumber; i <= lineNumberEnd; ++i)
			{
				auto state = (*_ppEditView)->execute(SCI_MARKERGET, i);

				if (state & (1 << MARK_BOOKMARK))
					(*_ppEditView)->execute(SCI_MARKERDELETE, i, MARK_BOOKMARK);
			}
		}
	}
	else
	{
		(*_ppEditView)->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE);
		if (opt._doMarkLine)
		{
			(*_ppEditView)->execute(SCI_MARKERDELETEALL, MARK_BOOKMARK);
		}
	}

	setStatusbarMessage(L"", FSNoMessage);
}

bool FindReplaceDlg::isSearchUnicodeCharOnAnsi(const wchar_t* text) const
{
	if ((*_ppEditView)->execute(SCI_GETCODEPAGE) == CP_ACP)
	{
		// NPLX: upstream WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, ...) reports a default char;
		// here the round trip through the ANSI code page must give back the same text
		WcharMbcsConvertor& wmc = WcharMbcsConvertor::getInstance();
		const std::string ansi = wmc.wchar2char(text, CP_ACP);
		const std::wstring back = wmc.char2wchar(ansi.c_str(), CP_ACP);
		return back != text;
	}
	return false;
}

// Expand selection (if needed) and set the selected text in Find What field.
// Return empty string if nothing to set in find field.
// Otherwise return string in which the selected text was copied.
wstring FindReplaceDlg::setSearchText()
{
	const NppGUI& nppGui = NppParameters::getInstance().getNppGUI();
	Sci_Position selStrCharNum = 0;
	auto selStr = (*_ppEditView)->getSelectedTextToWChar(true, &selStrCharNum);

	if (!selStr.empty() && selStrCharNum <= nppGui._fillFindWhatThreshold)
	{
		setSearchText(selStr.c_str());
		return selStr;
	}
	return L"";
}

// Set the selected text in Find What field, according the Search settings.
// Return empty string if nothing to set in find field.
// Otherwise return string in which the selected text was copied.
wstring FindReplaceDlg::setSearchTextWithSettings()
{
	const NppGUI& nppGui = NppParameters::getInstance().getNppGUI();
	if (nppGui._fillFindFieldWithSelected)
	{
		Sci_Position selStrCharNum = 0;
		auto selStr = (*_ppEditView)->getSelectedTextToWChar(nppGui._fillFindFieldSelectCaret, &selStrCharNum);

		if (!selStr.empty() && selStrCharNum <= nppGui._fillFindWhatThreshold)
		{
			setSearchText(selStr.c_str());
			return selStr;
		}
	}
	return L"";
}

// ============================================================================
// FindReplaceDlg: Qt dialog (upstream FindReplaceDlg.rc + run_dlgProc)
// ============================================================================

QWidget* FindReplaceDlg::ctrl(int id) const
{
	auto it = _ctrls.find(id);
	return it == _ctrls.end() ? nullptr : it->second;
}

QComboBox* FindReplaceDlg::comboBox(int id) const
{
	return qobject_cast<QComboBox*>(ctrl(id));
}

std::wstring FindReplaceDlg::statusbarMessage() const
{
	return _statusBar ? _statusBar->text().toStdWString() : std::wstring();
}

bool FindReplaceDlg::isCheckedOrNot(int checkControlID) const
{
	auto* button = qobject_cast<QAbstractButton*>(ctrl(checkControlID));
	return button && button->isChecked();
}

void FindReplaceDlg::setChecked(int checkControlID, bool checkOrNot) const
{
	if (auto* button = qobject_cast<QAbstractButton*>(ctrl(checkControlID)))
	{
		QSignalBlocker blocker(button);
		button->setChecked(checkOrNot);
	}
}

void FindReplaceDlg::enableFindDlgItem(int dlgItemID, bool isEnable /* = true*/)
{
	if (QWidget* w = ctrl(dlgItemID))
		w->setEnabled(isEnable);
}

void FindReplaceDlg::showFindDlgItem(int dlgItemID, bool isShow /* = true*/)
{
	if (QWidget* w = ctrl(dlgItemID))
		w->setVisible(isShow);
}

void FindReplaceDlg::create()
{
	if (_isCreated)
		return;

	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	setWindowFlag(Qt::WindowContextHelpButtonHint, false);
	setModal(false);
	setObjectName(QStringLiteral("FindReplaceDlg"));

	auto* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(8, 6, 8, 4);
	mainLayout->setSpacing(6);

	_tab = new QTabBar(this);
	_tab->setDrawBase(false);
	_tab->setExpanding(false);
	// upstream tab order: Find, Replace, Find in Files, Find in Projects, Mark
	_tab->addTab(QStringLiteral("Find"));
	_tab->addTab(QStringLiteral("Replace"));
	_tab->addTab(QStringLiteral("Find in Files"));
	_tab->addTab(QStringLiteral("Find in Projects"));
	_tab->addTab(QStringLiteral("Mark"));
	mainLayout->addWidget(_tab);

	auto* body = new QHBoxLayout;
	body->setSpacing(10);
	mainLayout->addLayout(body, 1);

	// NPLX: upstream IDD_FIND_REPLACE_DLG is one fixed-size dialog for all the tabs: every control has its place and
	// the controls of the other tabs are only hidden. In Qt layouts a hidden control gives its space away, so the
	// places are kept here: a control that only some tabs show keeps its size when hidden (keepPlace), controls of
	// different tabs share one grid cell (upstream: same position), and the dialog keeps the size of the largest tab
	auto keepPlace = [](QWidget* w) {
		QSizePolicy policy = w->sizePolicy();
		policy.setRetainSizeWhenHidden(true);
		w->setSizePolicy(policy);
		return w;
	};

	// --- left: fields, options and search mode
	auto* left = new QVBoxLayout;
	left->setSpacing(4);
	body->addLayout(left, 1);

	// the rows have a fixed height and no spacing: a grid leaves out the spacing of a row whose controls are all
	// hidden, even when they keep their place
	auto* fields = new QGridLayout;
	fields->setHorizontalSpacing(6);
	fields->setVerticalSpacing(0);
	fields->setColumnStretch(1, 1);
	left->addLayout(fields);

	auto makeLabel = [this](int id, const QString& text) {
		auto* label = new QLabel(text, this);
		_ctrls[id] = label;
		return label;
	};
	auto makeCombo = [this](int id) {
		auto* combo = new QComboBox(this);
		combo->setEditable(true);
		combo->setInsertPolicy(QComboBox::NoInsert);
		combo->setMinimumWidth(320);
		combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		combo->lineEdit()->setMaxLength(FINDREPLACE_MAXLENGTH - 1);
		combo->lineEdit()->installEventFilter(this);
		_ctrls[id] = combo;
		return combo;
	};
	auto makeButton = [this](int id, const QString& text) {
		auto* button = new QPushButton(text, this);
		button->setAutoDefault(false);
		_ctrls[id] = button;
		connect(button, &QPushButton::clicked, this, [this, id]() { command(id); });
		return button;
	};
	auto makeCheck = [this](int id, const QString& text) {
		auto* check = new QCheckBox(text, this);
		_ctrls[id] = check;
		connect(check, &QCheckBox::toggled, this, [this, id]() { command(id); });
		return check;
	};
	auto makeRadio = [this](int id, const QString& text) {
		auto* radio = new QRadioButton(text, this);
		_ctrls[id] = radio;
		connect(radio, &QRadioButton::toggled, this, [this, id](bool checked) { if (checked) command(id); });
		return radio;
	};

	// row 0: Find what (all tabs)
	auto* findWhatLabel = makeLabel(IDFINDWHAT_STATIC, QStringLiteral("&Find what:"));
	QComboBox* findWhat = makeCombo(IDFINDWHAT);
	findWhatLabel->setBuddy(findWhat);
	fields->addWidget(findWhatLabel, 0, 0, Qt::AlignRight);
	fields->addWidget(findWhat, 0, 1);

	// row 1: Replace with (Replace, Find in Files, Find in Projects)
	auto* replaceLabel = makeLabel(ID_STATICTEXT_REPLACE, QStringLiteral("Rep&lace with:"));
	QComboBox* replaceWith = makeCombo(IDREPLACEWITH);
	replaceLabel->setBuddy(replaceWith);
	fields->addWidget(keepPlace(replaceLabel), 1, 0, Qt::AlignRight);
	fields->addWidget(keepPlace(replaceWith), 1, 1);

	QPushButton* swapButton = makeButton(IDD_FINDREPLACE_SWAP_BUTTON, QStringLiteral("⇅"));
	swapButton->setFixedWidth(32);
	swapButton->setToolTip(QStringLiteral("Swap Find with Replace"));
	// upstream BS_SPLITBUTTON drop-down: swap, copy down, copy up
	swapButton->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(swapButton, &QWidget::customContextMenuRequested, this, [this, swapButton](const QPoint& pos) {
		QMenu menu(this);
		menu.addAction(QStringLiteral("⇅ Swap Find with Replace"), this, [this]() { command(IDC_SWAP_FIND_REPLACE); });
		menu.addAction(QStringLiteral("⤵ Copy from Find to Replace"), this, [this]() { command(IDC_COPY_FIND2REPLACE); });
		menu.addAction(QStringLiteral("⤴ Copy from Replace to Find"), this, [this]() { command(IDC_COPY_REPLACE2FIND); });
		menu.exec(swapButton->mapToGlobal(pos));
	});
	fields->addWidget(keepPlace(swapButton), 0, 2, 2, 1, Qt::AlignVCenter);

	// rows 2 and 3: Filters and Directory (Find in Files, Find in Projects), or Bookmark line and Purge (Mark), as
	// upstream where they take the same place
	auto* filtersLabel = makeLabel(IDD_FINDINFILES_FILTERS_STATIC, QStringLiteral("Filter&s:"));
	QComboBox* filters = makeCombo(IDD_FINDINFILES_FILTERS_COMBO);
	filtersLabel->setBuddy(filters);
	auto* filterTip = makeLabel(IDC_FIF_FILTER_TIP_STATIC, QStringLiteral("(?)"));
	filterTip->setAlignment(Qt::AlignCenter);
	filterTip->setToolTip(QString::fromStdWString(pNativeSpeaker->getLocalizedStrFromID("find-in-files-filter-tip",
		L"Find in cpp, cxx, h, hxx && hpp:\r*.cpp *.cxx *.h *.hxx *.hpp\r\rFind in all files except exe, obj && log:\r*.* !*.exe !*.obj !*.log\r\rFind in all files but exclude folders tests, bin && bin64:\r*.* !\\tests !\\bin*\r\rFind in all files but exclude all folders log or logs recursively:\r*.* !+\\log*")).replace(QLatin1Char('\r'), QLatin1Char('\n')).replace(QStringLiteral("&&"), QStringLiteral("&")));
	fields->addWidget(keepPlace(filtersLabel), 2, 0, Qt::AlignRight);
	fields->addWidget(keepPlace(filters), 2, 1);
	fields->addWidget(keepPlace(filterTip), 2, 2);

	auto* dirLabel = makeLabel(IDD_FINDINFILES_DIR_STATIC, QStringLiteral("Dir&ectory:"));
	QComboBox* dirCombo = makeCombo(IDD_FINDINFILES_DIR_COMBO);
	dirLabel->setBuddy(dirCombo);
	auto* dirButtons = new QHBoxLayout;
	dirButtons->setSpacing(2);
	QPushButton* fromDoc = makeButton(IDD_FINDINFILES_SETDIRFROMDOC_BUTTON, QStringLiteral("<<"));
	fromDoc->setFixedWidth(32);
	fromDoc->setToolTip(QString::fromStdWString(pNativeSpeaker->getLocalizedStrFromID("find-in-files-dir-from-active-doc-tip", L"Fill directory field based on active document")));
	QPushButton* browse = makeButton(IDD_FINDINFILES_BROWSE_BUTTON, QStringLiteral("..."));
	browse->setFixedWidth(32);
	dirButtons->addWidget(keepPlace(fromDoc));
	dirButtons->addWidget(keepPlace(browse));
	fields->addWidget(keepPlace(dirLabel), 3, 0, Qt::AlignRight);
	fields->addWidget(keepPlace(dirCombo), 3, 1);
	fields->addLayout(dirButtons, 3, 2);

	fields->addWidget(makeCheck(IDC_MARKLINE_CHECK, QStringLiteral("Book&mark line")), 2, 0, 1, 3, Qt::AlignVCenter);
	fields->addWidget(makeCheck(IDC_PURGE_CHECK, QStringLiteral("Purge for each search")), 3, 0, 1, 3, Qt::AlignVCenter);
	for (int row = 0; row <= 3; ++row)
		fields->setRowMinimumHeight(row, findWhat->sizeHint().height() + 4);

	// the search options (Backward direction and Wrap around are hidden in Find in Files: they keep their place)
	auto* checks = new QVBoxLayout;
	checks->setSpacing(2);
	checks->setContentsMargins(0, 6, 0, 0);
	left->addLayout(checks);
	checks->addWidget(keepPlace(makeCheck(IDC_BACKWARDDIRECTION, QStringLiteral("Backward direction"))));
	checks->addWidget(makeCheck(IDWHOLEWORD, QStringLiteral("Match &whole word only")));
	checks->addWidget(makeCheck(IDMATCHCASE, QStringLiteral("Match &case")));
	checks->addWidget(keepPlace(makeCheck(IDWRAP, QStringLiteral("Wra&p around"))));
	left->addStretch(1);

	auto* modeGroup = new QGroupBox(QStringLiteral("Search Mode"), this);
	_ctrls[IDC_MODE_STATIC] = modeGroup;
	auto* modeLayout = new QVBoxLayout(modeGroup);
	modeLayout->setSpacing(2);
	auto* modeButtons = new QButtonGroup(this);
	QRadioButton* normal = makeRadio(IDNORMAL, QStringLiteral("&Normal"));
	QRadioButton* extended = makeRadio(IDEXTENDED, QStringLiteral("E&xtended (\\n, \\r, \\t, \\0, \\x...)"));
	QRadioButton* regexp = makeRadio(IDREGEXP, QStringLiteral("Re&gular expression"));
	modeButtons->addButton(normal);
	modeButtons->addButton(extended);
	modeButtons->addButton(regexp);
	modeLayout->addWidget(normal);
	modeLayout->addWidget(extended);
	auto* regexRow = new QHBoxLayout;
	regexRow->addWidget(regexp);
	regexRow->addWidget(makeCheck(IDREDOTMATCHNL, QStringLiteral("&. matches newline")));
	regexRow->addStretch(1);
	modeLayout->addLayout(regexRow);
	left->addWidget(modeGroup);

	// --- right: action buttons (upstream: one column at x=298, the side check boxes at x=393), Close and
	// Transparency at the bottom
	auto* right = new QVBoxLayout;
	right->setSpacing(4);
	body->addLayout(right);

	auto* actions = new QGridLayout;
	actions->setHorizontalSpacing(4);
	actions->setVerticalSpacing(0);
	right->addLayout(actions);

	// row 0: In selection (Find, Replace, Mark)
	actions->addWidget(makeCheck(IDC_IN_SELECTION_CHECK, QStringLiteral("In select&ion")), 0, 0);

	// row 1: Find Next, or ▲ / ▼ Find Next (2 buttons mode), or Find All (Find in Files, Find in Projects)
	auto* findRow = new QHBoxLayout;
	findRow->setSpacing(2);
	QPushButton* findPrev = makeButton(IDC_FINDPREV, QStringLiteral("▲"));
	findPrev->setFixedWidth(40);
	QPushButton* findNextBtn = makeButton(IDC_FINDNEXT, QStringLiteral("▼ Find Next"));
	QPushButton* findNextOk = makeButton(IDOK, QStringLiteral("Find Next"));
	findNextOk->setToolTip(QString::fromStdWString(pNativeSpeaker->getLocalizedStrFromID("shift-change-direction-tip", L"Use Shift+Enter to search in the opposite direction.")));
	findRow->addWidget(findPrev);
	findRow->addWidget(findNextBtn, 1);
	findRow->addWidget(findNextOk, 1);
	actions->addLayout(findRow, 1, 0);
	QPushButton* fifFind = makeButton(IDD_FINDINFILES_FIND_BUTTON, QStringLiteral("Find All"));
	actions->addWidget(fifFind, 1, 0);
	auto* twoButtons = makeCheck(IDC_2_BUTTONS_MODE, QString());
	twoButtons->setToolTip(QString::fromStdWString(pNativeSpeaker->getLocalizedStrFromID("two-find-buttons-tip", L"2 find buttons mode")));
	actions->addWidget(keepPlace(twoButtons), 1, 1);

	// row 2: Count, Replace, Replace in Files, Replace in Projects, Mark All
	QPushButton* countAll = makeButton(IDCCOUNTALL, QStringLiteral("Coun&t"));
	QPushButton* replace = makeButton(IDREPLACE, QStringLiteral("&Replace"));
	QPushButton* replaceInFiles = makeButton(IDD_FINDINFILES_REPLACEINFILES, QStringLiteral("Replace in Files"));
	QPushButton* replaceInProjects = makeButton(IDD_FINDINFILES_REPLACEINPROJECTS, QStringLiteral("Replace in Projects"));
	QPushButton* markAll = makeButton(IDCMARKALL, QStringLiteral("Mark All"));
	for (QPushButton* button : { countAll, replace, replaceInFiles, replaceInProjects, markAll })
		actions->addWidget(button, 2, 0);
	auto* replaceAndStop = makeCheck(IDC_REPLACEANDSTOP, QString());
	replaceAndStop->setToolTip(QString::fromStdWString(pNativeSpeaker->getLocalizedStrFromID("replace-and-stop-tip", L"After replacing, don't move to the following occurrence")));
	actions->addWidget(replaceAndStop, 2, 1);

	// row 3: Find All in Current Document, Replace All, Clear all marks; In all sub-folders or Project Panel 1
	QPushButton* findAllCurrent = makeButton(IDC_FINDALL_CURRENTFILE, QStringLiteral("Find All in Current &Document"));
	QPushButton* replaceAll = makeButton(IDREPLACEALL, QStringLiteral("Replace &All"));
	QPushButton* clearAll = makeButton(IDC_CLEAR_ALL, QStringLiteral("Clear all marks"));
	for (QPushButton* button : { findAllCurrent, replaceAll, clearAll })
		actions->addWidget(button, 3, 0);
	actions->addWidget(makeCheck(IDD_FINDINFILES_RECURSIVE_CHECK, QStringLiteral("In all su&b-folders")), 3, 0, 1, 2);
	actions->addWidget(makeCheck(IDD_FINDINFILES_PROJECT1_CHECK, QStringLiteral("Project Panel 1")), 3, 0, 1, 2);

	// row 4: Find All in All Opened Documents, Replace All in All Opened Documents, Copy Marked Text; In hidden
	// folders or Project Panel 2
	QPushButton* findAllOpened = makeButton(IDC_FINDALL_OPENEDFILES, QStringLiteral("Find All in All &Opened Documents"));
	QPushButton* replaceOpened = makeButton(IDC_REPLACE_OPENEDFILES, QStringLiteral("Replace All in All Opened Doc&uments"));
	QPushButton* copyMarked = makeButton(IDC_COPY_MARKED_TEXT, QStringLiteral("Copy Marked Text"));
	for (QPushButton* button : { findAllOpened, replaceOpened, copyMarked })
		actions->addWidget(button, 4, 0);
	auto* confirmReplace = makeCheck(IDC_CONFIRMREPLACEOPENDOCS, QString());
	confirmReplace->setToolTip(QString::fromStdWString(pNativeSpeaker->getLocalizedStrFromID("confirm-replace-in-all-open-docs-tip", L"Confirm \"Replace All in All Opened Documents\" action")));
	actions->addWidget(confirmReplace, 4, 1);
	actions->addWidget(makeCheck(IDD_FINDINFILES_INHIDDENDIR_CHECK, QStringLiteral("In &hidden folders")), 4, 0, 1, 2);
	actions->addWidget(makeCheck(IDD_FINDINFILES_PROJECT2_CHECK, QStringLiteral("Project Panel 2")), 4, 0, 1, 2);

	// row 5: Project Panel 3
	actions->addWidget(makeCheck(IDD_FINDINFILES_PROJECT3_CHECK, QStringLiteral("Project Panel 3")), 5, 0, 1, 2);

	// the rows keep their height when a tab leaves them empty, and the buttons their width
	const int rowHeight = std::max({ findNextOk->sizeHint().height(), twoButtons->sizeHint().height(),
		replaceAndStop->sizeHint().height(), confirmReplace->sizeHint().height() });
	for (int row = 0; row <= 5; ++row)
		actions->setRowMinimumHeight(row, rowHeight + 4);
	int buttonWidth = 0;
	const QList<QPushButton*> actionButtons = { findNextOk, fifFind, countAll, replace, replaceInFiles, replaceInProjects,
		markAll, findAllCurrent, replaceAll, clearAll, findAllOpened, replaceOpened, copyMarked };
	for (QPushButton* button : actionButtons)
		buttonWidth = std::max(buttonWidth, button->sizeHint().width());
	for (QPushButton* button : actionButtons)
		button->setMinimumWidth(buttonWidth);
	actions->setColumnMinimumWidth(1, twoButtons->sizeHint().width());
	right->addStretch(1);

	QPushButton* close = makeButton(IDCANCEL, QStringLiteral("Close"));
	close->setMinimumWidth(buttonWidth);
	auto* closeRow = new QHBoxLayout;
	closeRow->addWidget(close);
	closeRow->addSpacing(actions->horizontalSpacing() + twoButtons->sizeHint().width());
	right->addLayout(closeRow);

	// Transparency (upstream IDC_TRANSPARENT_*)
	auto* transparentGroup = new QGroupBox(this);
	_ctrls[IDC_TRANSPARENT_GRPBOX] = transparentGroup;
	auto* transparentLayout = new QGridLayout(transparentGroup);
	transparentLayout->addWidget(makeCheck(IDC_TRANSPARENT_CHECK, QStringLiteral("Transparenc&y")), 0, 0, 1, 2);
	auto* transparentButtons = new QButtonGroup(this);
	QRadioButton* lossFocus = makeRadio(IDC_TRANSPARENT_LOSSFOCUS_RADIO, QStringLiteral("On losing focus"));
	QRadioButton* always = makeRadio(IDC_TRANSPARENT_ALWAYS_RADIO, QStringLiteral("Always"));
	transparentButtons->addButton(lossFocus);
	transparentButtons->addButton(always);
	transparentLayout->addWidget(lossFocus, 1, 0);
	transparentLayout->addWidget(always, 2, 0);
	auto* slider = new QSlider(Qt::Horizontal, this);
	slider->setRange(20, 200);
	slider->setMinimumWidth(90);
	_ctrls[IDC_PERCENTAGE_SLIDER] = slider;
	connect(slider, &QSlider::valueChanged, this, [this](int value) {
		// upstream WM_HSCROLL
		FindHistory& findHistory = NppParameters::getInstance().getFindHistory();
		findHistory._transparency = value;
		if (isCheckedOrNot(IDC_TRANSPARENT_ALWAYS_RADIO))
			setWindowOpacity(1.0 - value / 255.0);
	});
	transparentLayout->addWidget(slider, 1, 1, 2, 1);
	right->addWidget(transparentGroup);

	_statusBar = new QLabel(this);
	_statusBar->setFrameShape(QFrame::StyledPanel);
	_statusBar->setMinimumHeight(_statusBar->fontMetrics().height() + 6);
	_statusBar->setTextInteractionFlags(Qt::TextSelectableByMouse);
	mainLayout->addWidget(_statusBar);

	connect(_tab, &QTabBar::currentChanged, this, [this](int index) {
		// upstream WM_NOTIFY TCN_SELCHANGE
		if (index == FIND_DLG)
			enableReplaceFunc(false);
		else if (index == REPLACE_DLG)
			enableReplaceFunc(true);
		else if (index == FINDINFILES_DLG)
			enableFindInFilesFunc();
		else if (index == FINDINPROJECTS_DLG)
			enableFindInProjectsFunc();
		else if (index == MARK_DLG)
			enableMarkFunc();
		ctrl(IDFINDWHAT)->setFocus();
	});

	// "Find what" changes clear the status bar (upstream CBN_EDITCHANGE)
	connect(findWhat, &QComboBox::editTextChanged, this, [this]() {
		if (_statusbarFindStatus != FSWarning)
			setStatusbarMessage(L"", FSNoMessage);
	});

	_isCreated = true;

	fillFindHistory();
	initOptionsFromDlg();
	_options._isInSelection = false;

	// upstream: changeFindReplaceDlgLang the first time the dialog is shown (Find, Find in Files, Find in Projects...)
	nplx::changeFindReplaceDlgLang(*this);
}

void FindReplaceDlg::fillFindHistory()
{
	NppParameters& nppParams = NppParameters::getInstance();
	FindHistory & findHistory = nppParams.getFindHistory();

	fillComboHistory(IDFINDWHAT, findHistory._findHistoryFinds);
	fillComboHistory(IDREPLACEWITH, findHistory._findHistoryReplaces);
	fillComboHistory(IDD_FINDINFILES_FILTERS_COMBO, findHistory._findHistoryFilters);
	fillComboHistory(IDD_FINDINFILES_DIR_COMBO, findHistory._findHistoryPaths);

	setChecked(IDWRAP, findHistory._isWrap);
	setChecked(IDWHOLEWORD, findHistory._isMatchWord);
	setChecked(IDMATCHCASE, findHistory._isMatchCase);
	setChecked(IDC_BACKWARDDIRECTION, !findHistory._isDirectionDown);

	setChecked(IDD_FINDINFILES_INHIDDENDIR_CHECK, findHistory._isFifInHiddenFolder);
	setChecked(IDD_FINDINFILES_RECURSIVE_CHECK, findHistory._isFifRecursive);
	setChecked(IDD_FINDINFILES_PROJECT1_CHECK, findHistory._isFifProjectPanel_1);
	setChecked(IDD_FINDINFILES_PROJECT2_CHECK, findHistory._isFifProjectPanel_2);
	setChecked(IDD_FINDINFILES_PROJECT3_CHECK, findHistory._isFifProjectPanel_3);

	setChecked(IDNORMAL, findHistory._searchMode == FindHistory::normal);
	setChecked(IDEXTENDED, findHistory._searchMode == FindHistory::extended);
	setChecked(IDREGEXP, findHistory._searchMode == FindHistory::regExpr);
	setChecked(IDREDOTMATCHNL, findHistory._dotMatchesNewline);

	setChecked(IDC_MARKLINE_CHECK, findHistory._isBookmarkLine);
	setChecked(IDC_PURGE_CHECK, findHistory._isPurge);

	setChecked(IDC_2_BUTTONS_MODE, findHistory._isSearch2ButtonsMode);

	const NppGUI&	nppGUI = nppParams.getNppGUI();
	setChecked(IDC_CONFIRMREPLACEOPENDOCS, nppGUI._confirmReplaceInAllOpenDocs);
	setChecked(IDC_REPLACEANDSTOP, nppGUI._replaceStopsWithoutFindingNext);

	enableFindDlgItem(IDREDOTMATCHNL, findHistory._searchMode == FindHistory::regExpr);

	if (findHistory._searchMode == FindHistory::regExpr)
	{
		//regex doesn't allow wholeword
		setChecked(IDWHOLEWORD, false);
		enableFindDlgItem(IDWHOLEWORD, false);

		// regex upward search is disabled
		setChecked(IDC_BACKWARDDIRECTION, false);
		enableFindDlgItem(IDC_BACKWARDDIRECTION, nppParams.regexBackward4PowerUser());
		enableFindDlgItem(IDC_FINDPREV, nppParams.regexBackward4PowerUser());

		// If the search mode from history is regExp then enable the checkbox (. matches newline)
		enableFindDlgItem(IDREDOTMATCHNL);
	}

	if (nppParams.isTransparentAvailable())
	{
		showFindDlgItem(IDC_TRANSPARENT_CHECK);
		showFindDlgItem(IDC_TRANSPARENT_GRPBOX);
		showFindDlgItem(IDC_TRANSPARENT_LOSSFOCUS_RADIO);
		showFindDlgItem(IDC_TRANSPARENT_ALWAYS_RADIO);
		showFindDlgItem(IDC_PERCENTAGE_SLIDER);

		if (auto* slider = qobject_cast<QSlider*>(ctrl(IDC_PERCENTAGE_SLIDER)))
		{
			QSignalBlocker blocker(slider);
			slider->setValue(findHistory._transparency);
		}

		if (findHistory._transparencyMode == FindHistory::none)
		{
			enableFindDlgItem(IDC_TRANSPARENT_LOSSFOCUS_RADIO, false);
			enableFindDlgItem(IDC_TRANSPARENT_ALWAYS_RADIO, false);
			enableFindDlgItem(IDC_PERCENTAGE_SLIDER, false);
		}
		else
		{
			setChecked(IDC_TRANSPARENT_CHECK, true);

			int id;
			if (findHistory._transparencyMode == FindHistory::onLosingFocus)
			{
				id = IDC_TRANSPARENT_LOSSFOCUS_RADIO;
			}
			else
			{
				id = IDC_TRANSPARENT_ALWAYS_RADIO;
				setWindowOpacity(1.0 - findHistory._transparency / 255.0); // NPLX: was NppParameters::setTransparent

			}
			setChecked(id, true);
		}
	}
}

void FindReplaceDlg::fillComboHistory(int id, const vector<wstring> & strings)
{
	QComboBox* hCombo = comboBox(id);

	for (vector<wstring>::const_reverse_iterator i = strings.rbegin() ; i != strings.rend(); ++i)
	{
		addText2Combo(i->c_str(), hCombo);
	}

	//empty string is not added to CB items, so we need to set it manually
	if (!strings.empty() && strings.begin()->empty())
	{
		hCombo->setEditText(QString());
		return;
	}

	if (hCombo->count() > 0)
		hCombo->setCurrentIndex(0); // select first item
}


void FindReplaceDlg::saveFindHistory()
{
	if (! isCreated()) return;
	FindHistory& findHistory = (NppParameters::getInstance()).getFindHistory();

	saveComboHistory(IDD_FINDINFILES_DIR_COMBO, findHistory._nbMaxFindHistoryPath, findHistory._findHistoryPaths, false);
	saveComboHistory(IDD_FINDINFILES_FILTERS_COMBO, findHistory._nbMaxFindHistoryFilter, findHistory._findHistoryFilters, true);
	saveComboHistory(IDFINDWHAT,                    findHistory._nbMaxFindHistoryFind, findHistory._findHistoryFinds, false);
	saveComboHistory(IDREPLACEWITH,                 findHistory._nbMaxFindHistoryReplace, findHistory._findHistoryReplaces, true);
}

int FindReplaceDlg::saveComboHistory(int id, int maxcount, vector<wstring> & strings, bool saveEmpty)
{
	QComboBox* hCombo = comboBox(id);
	if (!hCombo) return 0;
	int count = hCombo->count();
	count = std::min<int>(count, maxcount);

	strings.clear();

	if (saveEmpty)
	{
		if (hCombo->currentText().isEmpty())
		{
			strings.push_back(wstring());
		}
	}

	for (int i = 0 ; i < count ; ++i)
	{
		const QString text = hCombo->itemText(i);
		if (text.size() <= FINDREPLACE_MAXLENGTH2SAVE - 1)
		{
			strings.push_back(text.toStdWString());
		}
	}
	return count;
}

void FindReplaceDlg::updateCombos()
{
	updateCombo(IDREPLACEWITH);
	updateCombo(IDFINDWHAT);
}

void FindReplaceDlg::updateCombo(int comboID)
{
	QComboBox* hCombo = comboBox(comboID);
	const QString text = hCombo->currentText();
	addText2Combo(text.toStdWString().c_str(), hCombo);
	hCombo->setEditText(text); // NPLX: keep the edit text (an empty one included) as it was
}

void FindReplaceDlg::initOptionsFromDlg()
{
	_options._isWholeWord = isCheckedOrNot(IDWHOLEWORD);
	_options._isMatchCase = isCheckedOrNot(IDMATCHCASE);
	_options._searchType = isCheckedOrNot(IDREGEXP)?FindRegex:isCheckedOrNot(IDEXTENDED)?FindExtended:FindNormal;
	_options._isWrapAround = isCheckedOrNot(IDWRAP);
	_options._isInSelection = isCheckedOrNot(IDC_IN_SELECTION_CHECK);

	_options._dotMatchesNewline = isCheckedOrNot(IDREDOTMATCHNL);
	_options._doPurge = isCheckedOrNot(IDC_PURGE_CHECK);
	_options._doMarkLine = isCheckedOrNot(IDC_MARKLINE_CHECK);

	_options._whichDirection = isCheckedOrNot(IDC_BACKWARDDIRECTION) ? DIR_UP : DIR_DOWN;

	_options._isRecursive = isCheckedOrNot(IDD_FINDINFILES_RECURSIVE_CHECK);
	_options._isInHiddenDir = isCheckedOrNot(IDD_FINDINFILES_INHIDDENDIR_CHECK);
	_options._isProjectPanel_1 =  isCheckedOrNot(IDD_FINDINFILES_PROJECT1_CHECK);
	_options._isProjectPanel_2 =  isCheckedOrNot(IDD_FINDINFILES_PROJECT2_CHECK);
	_options._isProjectPanel_3 =  isCheckedOrNot(IDD_FINDINFILES_PROJECT3_CHECK);
}

void FindReplaceDlg::doDialog(DIALOG_TYPE whichType, bool isRTL, bool toShow)
{
	if (!isCreated())
	{
		_isRTL = isRTL;
		if (isRTL)
			setLayoutDirection(Qt::RightToLeft);
		create();

		_sendToParent(NPPM_INTERNAL_ENABLECOMMAND, IDM_SEARCH_FINDNEXT, TRUE);
		_sendToParent(NPPM_INTERNAL_ENABLECOMMAND, IDM_SEARCH_FINDPREV, TRUE);
	}

	if (whichType == FINDINFILES_DLG)
		enableFindInFilesFunc();
	else if (whichType == FINDINPROJECTS_DLG)
		enableFindInProjectsFunc();
	else if (whichType == MARK_DLG)
		enableMarkFunc();
	else
		enableReplaceFunc(whichType == REPLACE_DLG);

	if (toShow)
	{
		show();
		raise();
		activateWindow();
		ctrl(IDFINDWHAT)->setFocus();
		updateInSelectionCheck();
	}
	else
	{
		(*_ppEditView)->setFocus();
	}
}

void FindReplaceDlg::changeTabName(DIALOG_TYPE index, const wchar_t* name2change)
{
	if (!_tab)
		return;
	_tab->setTabText(index, QString::fromWCharArray(name2change));
	updateTitle();
}

void FindReplaceDlg::updateTitle()
{
	if (_tab)
		setWindowTitle(_tab->tabText(_tab->currentIndex()));
}

void FindReplaceDlg::gotoCorrectTab()
{
	auto currentIndex = _tab->currentIndex();
	if (currentIndex != _currentStatus)
	{
		QSignalBlocker blocker(_tab);
		_tab->setCurrentIndex(_currentStatus);
	}
}

void FindReplaceDlg::enableReplaceFunc(bool isEnable)
{
	_currentStatus = isEnable ? REPLACE_DLG : FIND_DLG;

	const bool isLessModeOff = !NppParameters::getInstance().getNppGUI()._findWindowLessMode;

	enableFindInFilesControls(false, false);
	enableMarkAllControls(false);
	// replace controls
	showFindDlgItem(ID_STATICTEXT_REPLACE, isEnable);
	showFindDlgItem(IDREPLACE, isEnable);
	showFindDlgItem(IDC_REPLACEANDSTOP, isEnable && isLessModeOff);
	showFindDlgItem(IDREPLACEWITH, isEnable);
	showFindDlgItem(IDD_FINDREPLACE_SWAP_BUTTON, isEnable);
	showFindDlgItem(IDREPLACEALL, isEnable);
	showFindDlgItem(IDC_REPLACE_OPENEDFILES, isEnable && isLessModeOff);
	showFindDlgItem(IDC_CONFIRMREPLACEOPENDOCS, isEnable && isLessModeOff);
	showFindDlgItem(IDC_REPLACEINSELECTION, isLessModeOff);
	showFindDlgItem(IDC_IN_SELECTION_CHECK, isLessModeOff);
	showFindDlgItem(IDC_2_BUTTONS_MODE);
	bool is2ButtonMode = isCheckedOrNot(IDC_2_BUTTONS_MODE);
	showFindDlgItem(IDOK, !is2ButtonMode);
	showFindDlgItem(IDC_FINDPREV, is2ButtonMode);
	showFindDlgItem(IDC_FINDNEXT, is2ButtonMode);


	// find controls
	showFindDlgItem(IDC_FINDALL_OPENEDFILES, !isEnable && isLessModeOff);
	showFindDlgItem(IDCCOUNTALL, !isEnable);
	showFindDlgItem(IDC_FINDALL_CURRENTFILE, !isEnable && isLessModeOff);

	gotoCorrectTab();
	updateTitle();

	if (auto* ok = qobject_cast<QPushButton*>(ctrl(IDOK)))
		ok->setDefault(true);
}

void FindReplaceDlg::enableMarkAllControls(bool isEnable)
{
	const bool isLessModeOff = !NppParameters::getInstance().getNppGUI()._findWindowLessMode;

	showFindDlgItem(IDCMARKALL, isEnable);
	showFindDlgItem(IDC_MARKLINE_CHECK, isEnable && isLessModeOff);
	showFindDlgItem(IDC_PURGE_CHECK, isEnable && isLessModeOff);
	showFindDlgItem(IDC_CLEAR_ALL, isEnable);
	showFindDlgItem(IDC_IN_SELECTION_CHECK, isEnable && isLessModeOff);
	showFindDlgItem(IDC_COPY_MARKED_TEXT, isEnable && isLessModeOff);
}

void FindReplaceDlg::enableFindInFilesControls(bool isEnable, bool projectPanels)
{
	const bool isLessModeOff = !NppParameters::getInstance().getNppGUI()._findWindowLessMode;

	// Hide Items
	showFindDlgItem(IDC_BACKWARDDIRECTION, !isEnable && isLessModeOff);
	showFindDlgItem(IDWRAP, !isEnable && isLessModeOff);
	showFindDlgItem(IDCCOUNTALL, !isEnable);
	showFindDlgItem(IDC_FINDALL_OPENEDFILES, !isEnable && isLessModeOff);
	showFindDlgItem(IDC_FINDALL_CURRENTFILE, !isEnable && isLessModeOff);

	if (isEnable)
	{
		showFindDlgItem(IDC_2_BUTTONS_MODE, false);
		showFindDlgItem(IDOK, false);
		showFindDlgItem(IDC_FINDPREV, false);
		showFindDlgItem(IDC_FINDNEXT, false);
	}
	else
	{
		showFindDlgItem(IDC_2_BUTTONS_MODE);
		bool is2ButtonMode = isCheckedOrNot(IDC_2_BUTTONS_MODE);
		showFindDlgItem(IDOK, !is2ButtonMode);
		showFindDlgItem(IDC_FINDPREV, is2ButtonMode);
		showFindDlgItem(IDC_FINDNEXT, is2ButtonMode);
	}

	showFindDlgItem(IDC_MARKLINE_CHECK, !isEnable && isLessModeOff);
	showFindDlgItem(IDC_PURGE_CHECK, !isEnable && isLessModeOff);
	showFindDlgItem(IDC_IN_SELECTION_CHECK, !isEnable && isLessModeOff);
	showFindDlgItem(IDC_CLEAR_ALL, !isEnable);
	showFindDlgItem(IDCMARKALL, !isEnable);
	showFindDlgItem(IDC_COPY_MARKED_TEXT, !isEnable && isLessModeOff);

	showFindDlgItem(IDREPLACE, !isEnable);
	showFindDlgItem(IDC_REPLACEANDSTOP, !isEnable && isLessModeOff);
	showFindDlgItem(IDC_REPLACEINSELECTION, !isEnable && isLessModeOff);
	showFindDlgItem(IDREPLACEALL, !isEnable && isLessModeOff);
	showFindDlgItem(IDC_REPLACE_OPENEDFILES, !isEnable && isLessModeOff);
	showFindDlgItem(IDC_CONFIRMREPLACEOPENDOCS, !isEnable && isLessModeOff);

	// Show Items
	if (isEnable)
	{
		showFindDlgItem(ID_STATICTEXT_REPLACE);
		showFindDlgItem(IDREPLACEWITH);
		showFindDlgItem(IDD_FINDREPLACE_SWAP_BUTTON);
	}
	showFindDlgItem(IDD_FINDINFILES_REPLACEINFILES, isEnable && (!projectPanels));
	showFindDlgItem(IDD_FINDINFILES_REPLACEINPROJECTS, isEnable && projectPanels);
	showFindDlgItem(IDD_FINDINFILES_FILTERS_STATIC, isEnable && isLessModeOff);
	showFindDlgItem(IDD_FINDINFILES_FILTERS_COMBO, isEnable && isLessModeOff);
	showFindDlgItem(IDC_FIF_FILTER_TIP_STATIC, isEnable && isLessModeOff);
	showFindDlgItem(IDD_FINDINFILES_DIR_STATIC, isEnable && (!projectPanels) && isLessModeOff);
	showFindDlgItem(IDD_FINDINFILES_DIR_COMBO, isEnable && (!projectPanels) && isLessModeOff);
	showFindDlgItem(IDD_FINDINFILES_BROWSE_BUTTON, isEnable && (!projectPanels) && isLessModeOff);
	showFindDlgItem(IDD_FINDINFILES_FIND_BUTTON, isEnable);
	showFindDlgItem(IDD_FINDINFILES_RECURSIVE_CHECK, isEnable && (!projectPanels) && isLessModeOff);
	showFindDlgItem(IDD_FINDINFILES_INHIDDENDIR_CHECK, isEnable && (!projectPanels) && isLessModeOff);
	showFindDlgItem(IDD_FINDINFILES_PROJECT1_CHECK, isEnable && projectPanels && isLessModeOff);
	showFindDlgItem(IDD_FINDINFILES_PROJECT2_CHECK, isEnable && projectPanels && isLessModeOff);
	showFindDlgItem(IDD_FINDINFILES_PROJECT3_CHECK, isEnable && projectPanels && isLessModeOff);
	showFindDlgItem(IDD_FINDINFILES_SETDIRFROMDOC_BUTTON, isEnable && (!projectPanels) && isLessModeOff);
}

void FindReplaceDlg::enableFindInFilesFunc()
{
	enableFindInFilesControls(true, false);
	_currentStatus = FINDINFILES_DLG;
	gotoCorrectTab();
	updateTitle();
	if (auto* find = qobject_cast<QPushButton*>(ctrl(IDD_FINDINFILES_FIND_BUTTON)))
		find->setDefault(true);
	enableFindDlgItem (IDD_FINDINFILES_FIND_BUTTON, true);
}

void FindReplaceDlg::enableFindInProjectsFunc()
{
	enableFindInFilesControls(true, true);
	_currentStatus = FINDINPROJECTS_DLG;
	gotoCorrectTab();
	updateTitle();
	if (auto* find = qobject_cast<QPushButton*>(ctrl(IDD_FINDINFILES_FIND_BUTTON)))
		find->setDefault(true);
	bool enable = _options._isProjectPanel_1 || _options._isProjectPanel_2 || _options._isProjectPanel_3;
	enableFindDlgItem (IDD_FINDINFILES_FIND_BUTTON, enable);
	enableFindDlgItem (IDD_FINDINFILES_REPLACEINPROJECTS, enable);
}

void FindReplaceDlg::enableMarkFunc()
{
	enableFindInFilesControls(false, false);
	enableMarkAllControls(true);

	// Replace controls to hide
	showFindDlgItem(ID_STATICTEXT_REPLACE, false);
	showFindDlgItem(IDREPLACE, false);
	showFindDlgItem(IDC_REPLACEANDSTOP, false);
	showFindDlgItem(IDREPLACEWITH, false);
	showFindDlgItem(IDD_FINDREPLACE_SWAP_BUTTON, false);
	showFindDlgItem(IDREPLACEALL, false);
	showFindDlgItem(IDC_REPLACE_OPENEDFILES, false);
	showFindDlgItem(IDC_CONFIRMREPLACEOPENDOCS, false);

	// find controls to hide
	showFindDlgItem(IDC_FINDALL_OPENEDFILES, false);
	showFindDlgItem(IDCCOUNTALL, false);
	showFindDlgItem(IDC_FINDALL_CURRENTFILE, false);
	showFindDlgItem(IDOK, false);
	showFindDlgItem(IDC_2_BUTTONS_MODE, false);
	showFindDlgItem(IDC_FINDPREV, false);
	showFindDlgItem(IDC_FINDNEXT, false);

	_currentStatus = MARK_DLG;
	gotoCorrectTab();
	updateTitle();
	if (auto* mark = qobject_cast<QPushButton*>(ctrl(IDCMARKALL)))
		mark->setDefault(true);
}

void FindReplaceDlg::saveInMacro(size_t cmd, int cmdType)
{
	int booleans = 0;
	_sendToParent(WM_FRSAVE_INT, IDC_FRCOMMAND_INIT, 0);
	_sendToParent(WM_FRSAVE_STR, IDFINDWHAT,  reinterpret_cast<sptr_t>(cmd == IDC_CLEAR_ALL ? "" : wstring2string(_options._str2Search, CP_UTF8).c_str()));
	booleans |= _options._isWholeWord?IDF_WHOLEWORD:0;
	booleans |= _options._isMatchCase?IDF_MATCHCASE:0;
	booleans |= _options._dotMatchesNewline?IDF_REDOTMATCHNL:0;

	_sendToParent(WM_FRSAVE_INT, IDNORMAL, _options._searchType);
	if (cmd == IDCMARKALL)
	{
		booleans |= _options._doPurge?IDF_PURGE_CHECK:0;
		booleans |= _options._doMarkLine?IDF_MARKLINE_CHECK:0;
	}
	if (cmdType & FR_OP_REPLACE)
		_sendToParent(WM_FRSAVE_STR, IDREPLACEWITH, reinterpret_cast<sptr_t>(wstring2string(_options._str4Replace, CP_UTF8).c_str()));
	if (cmdType & FR_OP_FIF)
	{
		_sendToParent(WM_FRSAVE_STR, IDD_FINDINFILES_DIR_COMBO, reinterpret_cast<sptr_t>(wstring2string(_options._directory, CP_UTF8).c_str()));
		_sendToParent(WM_FRSAVE_STR, IDD_FINDINFILES_FILTERS_COMBO, reinterpret_cast<sptr_t>(wstring2string(_options._filters, CP_UTF8).c_str()));
		booleans |= _options._isRecursive?IDF_FINDINFILES_RECURSIVE_CHECK:0;
		booleans |= _options._isInHiddenDir?IDF_FINDINFILES_INHIDDENDIR_CHECK:0;
	}
	if (cmdType & FR_OP_FIP)
	{
		_sendToParent(WM_FRSAVE_STR, IDD_FINDINFILES_FILTERS_COMBO, reinterpret_cast<sptr_t>(wstring2string(_options._filters, CP_UTF8).c_str()));
		booleans |= _options._isProjectPanel_1?IDF_FINDINFILES_PROJECT1_CHECK:0;
		booleans |= _options._isProjectPanel_2?IDF_FINDINFILES_PROJECT2_CHECK:0;
		booleans |= _options._isProjectPanel_3?IDF_FINDINFILES_PROJECT3_CHECK:0;
	}
	else if (!(cmdType & FR_OP_GLOBAL))
	{
		booleans |= _options._isInSelection?IDF_IN_SELECTION_CHECK:0;
		booleans |= _options._isWrapAround?IDF_WRAP:0;
		booleans |= _options._whichDirection?IDF_WHICH_DIRECTION:0;
	}
	else if (cmdType == FR_OP_FIND + FR_OP_GLOBAL)
	{
		// find all in curr doc can be limited by selected text
		booleans |= _options._isInSelection ? IDF_IN_SELECTION_CHECK : 0;
	}
	if (cmd == IDC_CLEAR_ALL)
	{
		booleans = _options._doMarkLine ? IDF_MARKLINE_CHECK : 0;
		booleans |= _options._isInSelection ? IDF_IN_SELECTION_CHECK : 0;
	}
	_sendToParent(WM_FRSAVE_INT, IDC_FRCOMMAND_BOOLEANS, booleans);
	_sendToParent(WM_FRSAVE_INT, IDC_FRCOMMAND_EXEC, cmd);
}

void FindReplaceDlg::execSavedCommand(int cmd, uptr_t intValue, const wstring& stringValue)
{
	try {
		switch (cmd)
		{
			case IDC_FRCOMMAND_INIT:
				_env = new FindOption;
				break;
			case IDFINDWHAT:
				_env->_str2Search = stringValue;
				break;
			case IDC_FRCOMMAND_BOOLEANS:
				_env->_isWholeWord = ((intValue & IDF_WHOLEWORD) > 0);
				_env->_isMatchCase = ((intValue & IDF_MATCHCASE) > 0);
				_env->_isRecursive = ((intValue & IDF_FINDINFILES_RECURSIVE_CHECK) > 0);
				_env->_isInHiddenDir = ((intValue & IDF_FINDINFILES_INHIDDENDIR_CHECK) > 0);
				_env->_isProjectPanel_1 = ((intValue & IDF_FINDINFILES_PROJECT1_CHECK) > 0);
				_env->_isProjectPanel_2 = ((intValue & IDF_FINDINFILES_PROJECT2_CHECK) > 0);
				_env->_isProjectPanel_3 = ((intValue & IDF_FINDINFILES_PROJECT3_CHECK) > 0);
				_env->_doPurge = ((intValue & IDF_PURGE_CHECK) > 0);
				_env->_doMarkLine = ((intValue & IDF_MARKLINE_CHECK) > 0);
				_env->_isInSelection = ((intValue & IDF_IN_SELECTION_CHECK) > 0);
				_env->_isWrapAround = ((intValue & IDF_WRAP) > 0);
				_env->_whichDirection = ((intValue & IDF_WHICH_DIRECTION) > 0);
				_env->_dotMatchesNewline = ((intValue & IDF_REDOTMATCHNL) > 0);
				break;
			case IDNORMAL:
				_env->_searchType = static_cast<SearchType>(intValue);
				break;
			case IDREPLACEWITH:
				_env->_str4Replace = stringValue;
				break;
			case IDD_FINDINFILES_DIR_COMBO:
				_env->_directory = stringValue;
				break;
			case IDD_FINDINFILES_FILTERS_COMBO:
				_env->_filters = stringValue;
				break;
			case IDC_FRCOMMAND_EXEC:
			{
				NppParameters& nppParamInst = NppParameters::getInstance();
				switch (intValue)
				{
					case IDOK:
						if ((_env->_whichDirection == DIR_UP) && (_env->_searchType == FindRegex) && !nppParamInst.regexBackward4PowerUser())
						{
							// regex upward search is disabled
							// this macro step could have been recorded in an earlier version before it was not allowed, or hand-edited
							// make this a no-action macro step

							regexBackwardMsgBox();
						}
						else
						{
							nppParamInst._isFindReplacing = true;
							processFindNext(_env->_str2Search.c_str());
							nppParamInst._isFindReplacing = false;
						}
						break;

					case IDC_FINDNEXT:
						// IDC_FINDNEXT will not be recorded into new macros recorded with 7.8.5 and later
						// stay playback compatible with 7.5.5 - 7.8.4 where IDC_FINDNEXT was allowed but unneeded/undocumented
						nppParamInst._isFindReplacing = true;
						_env->_whichDirection = DIR_DOWN;
						processFindNext(_env->_str2Search.c_str());
						nppParamInst._isFindReplacing = false;
						break;

					case IDC_FINDPREV:
						// IDC_FINDPREV will not be recorded into new macros recorded with 7.8.5 and later
						// stay playback compatible with 7.5.5 - 7.8.4 where IDC_FINDPREV was allowed but unneeded/undocumented
						if (_env->_searchType == FindRegex && !nppParamInst.regexBackward4PowerUser())
						{
							// regex upward search is disabled
							// this macro step could have been recorded in an earlier version before it was not allowed, or hand-edited
							// make this a no-action macro step

							regexBackwardMsgBox();
						}
						else
						{
							_env->_whichDirection = DIR_UP;
							nppParamInst._isFindReplacing = true;
							processFindNext(_env->_str2Search.c_str());
							nppParamInst._isFindReplacing = false;
						}
						break;

					case IDREPLACE:
						if ((_env->_whichDirection == DIR_UP) && (_env->_searchType == FindRegex && !nppParamInst.regexBackward4PowerUser()))
						{
							// regex upward search is disabled
							// this macro step could have been recorded in an earlier version before it was disabled, or hand-edited
							// make this a no-action macro step

							regexBackwardMsgBox();
						}
						else
						{
							nppParamInst._isFindReplacing = true;
							processReplace(_env->_str2Search.c_str(), _env->_str4Replace.c_str(), _env);
							nppParamInst._isFindReplacing = false;
						}
						break;
					case IDC_FINDALL_OPENEDFILES:
						nppParamInst._isFindReplacing = true;
						findAllIn(ALL_OPEN_DOCS);
						nppParamInst._isFindReplacing = false;
						break;
					case IDC_FINDALL_CURRENTFILE:
						nppParamInst._isFindReplacing = true;
						findAllIn(_env->_isInSelection ? CURR_DOC_SELECTION : CURRENT_DOC);
						nppParamInst._isFindReplacing = false;
						break;
					case IDC_REPLACE_OPENEDFILES:
					{
						NppParameters& nppParam = NppParameters::getInstance();
						const NppGUI& nppGui = nppParam.getNppGUI();
						if (!nppGui._confirmReplaceInAllOpenDocs || replaceInOpenDocsConfirmCheck())
						{
							nppParamInst._isFindReplacing = true;
							replaceAllInOpenedDocs();
							nppParamInst._isFindReplacing = false;
						}
						break;
					}
					case IDD_FINDINFILES_FIND_BUTTON:
						nppParamInst._isFindReplacing = true;
						findAllIn(FILES_IN_DIR);
						nppParamInst._isFindReplacing = false;
						break;
					case IDD_FINDINFILES_FINDINPROJECTS:
						nppParamInst._isFindReplacing = true;
						findAllIn(FILES_IN_PROJECTS);
						nppParamInst._isFindReplacing = false;
						break;

					case IDD_FINDINFILES_REPLACEINFILES:
					{
						if (replaceInFilesConfirmCheck(_env->_directory, _env->_filters))
						{
							nppParamInst._isFindReplacing = true;
							_sendToParent(WM_REPLACEINFILES, 0, 0);
							nppParamInst._isFindReplacing = false;
						}
						break;
					}

					case IDD_FINDINFILES_REPLACEINPROJECTS:
					{
						if (replaceInProjectsConfirmCheck())
						{
							nppParamInst._isFindReplacing = true;
							_sendToParent(WM_REPLACEINPROJECTS, 0, 0);
							nppParamInst._isFindReplacing = false;
						}
						break;
					}

					case IDREPLACEALL:
					{
						nppParamInst._isFindReplacing = true;
						(*_ppEditView)->execute(SCI_BEGINUNDOACTION);
						int nbReplaced = processAll(ProcessReplaceAll, _env);
						(*_ppEditView)->execute(SCI_ENDUNDOACTION);
						nppParamInst._isFindReplacing = false;

						if (nbReplaced == FIND_INVALID_REGULAR_EXPRESSION)
						{	
							setStatusbarMessageWithRegExprErr(*_ppEditView);
						}
						else
						{
							wstring result;
							NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

							if (nbReplaced == 1)
							{
								result = pNativeSpeaker->getLocalizedStrFromID("find-status-replaceall-1-replaced", L"Replace All: 1 occurrence was replaced");
							}
							else //if (nbReplaced == 0 || nbReplaced > 1)
							{
								result = pNativeSpeaker->getLocalizedStrFromID("find-status-replaceall-nb-replaced", L"Replace All: $INT_REPLACE$ occurrences were replaced");
								result = stringReplace(result, L"$INT_REPLACE$", std::to_wstring(nbReplaced));
							}
							result += L" ";
							result += getScopeInfoForStatusBar(_env);

							setStatusbarMessage(result, FSMessage);
						}

						break;
					}

					case IDCCOUNTALL:
					{
						int nbCounted = processAll(ProcessCountAll, _env);

						if (nbCounted == FIND_INVALID_REGULAR_EXPRESSION)
						{
							setStatusbarMessageWithRegExprErr(*_ppEditView);
						}
						else
						{
							wstring result;
							NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

							if (nbCounted == 1)
							{
								result = pNativeSpeaker->getLocalizedStrFromID("find-status-count-1-match", L"Count: 1 match");
							}
							else
							{
								result = pNativeSpeaker->getLocalizedStrFromID("find-status-count-nb-matches", L"Count: $INT_REPLACE$ matches");
								result = stringReplace(result, L"$INT_REPLACE$", std::to_wstring(nbCounted));
							}
							result += L" ";
							result += getScopeInfoForStatusBar(_env);

							setStatusbarMessage(result, FSMessage);
						}
						break;
					}

					case IDCMARKALL:
					{
						nppParamInst._isFindReplacing = true;
						int nbMarked = processAll(ProcessMarkAll, _env);
						nppParamInst._isFindReplacing = false;

						if (nbMarked == FIND_INVALID_REGULAR_EXPRESSION)
						{
							setStatusbarMessageWithRegExprErr(*_ppEditView);
						}
						else
						{
							wstring result;
							NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
							if (nbMarked == 1)
							{
								result = pNativeSpeaker->getLocalizedStrFromID("find-status-mark-1-match", L"Mark: 1 match");
							}
							else
							{
								result = pNativeSpeaker->getLocalizedStrFromID("find-status-mark-nb-matches", L"Mark: $INT_REPLACE$ matches");
								result = stringReplace(result, L"$INT_REPLACE$", std::to_wstring(nbMarked));
							}
							result += L" ";
							result += getScopeInfoForStatusBar(_env);

							setStatusbarMessage(result, FSMessage);
						}

						break;
					}

					case IDC_CLEAR_ALL:
					{
						clearMarks(*_env);
						break;
					}

					default:
						throw std::runtime_error("Internal error: unknown saved command!");
				}

				delete _env;
				_env = &_options;
				break;
			}
			default:
				throw std::runtime_error("Internal error: unknown SnR command!");
		}
	}
	catch (const std::runtime_error& err)
	{
		nplx::messageBox(string2wstring(err.what()), L"Play Macro Exception", MB_OK); // NPLX: was MessageBoxA
	}
}

bool FindReplaceDlg::replaceInProjectsConfirmCheck()
{
	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

	wstring title = pNativeSpeaker->getLocalizedStrFromID("replace-in-projects-confirm-title", L"Are you sure?");

	wstring msg = pNativeSpeaker->getLocalizedStrFromID("replace-in-projects-confirm-message", L"Do you want to replace all occurrences in all documents in the selected Project Panel(s)?");

	const bool confirmed = nplx::messageBox(msg, title, MB_OKCANCEL | MB_DEFBUTTON2 | MB_TASKMODAL) == IDOK; // NPLX: was NppDarkMode::darkMessageBoxW
	return confirmed;
}

void FindReplaceDlg::enableProjectCheckmarks()
{
	NppParameters& nppParams = NppParameters::getInstance();
	FindHistory & findHistory = nppParams.getFindHistory();
	// NPLX: the View menu check marks through NPPM_INTERNAL_ISMENUITEMCHECKED (upstream: GetMenuState)
	int   idm [3] = {IDM_VIEW_PROJECT_PANEL_1, IDM_VIEW_PROJECT_PANEL_2, IDM_VIEW_PROJECT_PANEL_3};
	int   idd [3] = {IDD_FINDINFILES_PROJECT1_CHECK, IDD_FINDINFILES_PROJECT2_CHECK, IDD_FINDINFILES_PROJECT3_CHECK};
	bool *opt [3] = {&_options._isProjectPanel_1, &_options._isProjectPanel_2, &_options._isProjectPanel_3};
	bool *hst [3] = {&findHistory._isFifProjectPanel_1, &findHistory._isFifProjectPanel_2, &findHistory._isFifProjectPanel_3};
	bool enable = false;
	for (int i = 0; i < 3; i++)
	{
		if (_sendToParent && _sendToParent(NPPM_INTERNAL_ISMENUITEMCHECKED, idm[i], 0))
		{
			enableFindDlgItem (idd [i], true);
			if (isCheckedOrNot(idd [i]))
				enable = true;
		}
		else
		{
			*opt [i] = 0;
			*hst [i] = 0;
			setChecked(idd [i], false);
			enableFindDlgItem (idd [i], false);
		}
	}
	enableFindDlgItem (IDD_FINDINFILES_FIND_BUTTON, enable || (_currentStatus != FINDINPROJECTS_DLG));
	enableFindDlgItem (IDD_FINDINFILES_REPLACEINPROJECTS, enable);
}

void FindReplaceDlg::setProjectCheckmarks(FindHistory *findHistory, int msk)
{
	_options._isProjectPanel_1 = (msk & 1) ? true : false;
	_options._isProjectPanel_2 = (msk & 2) ? true : false;
	_options._isProjectPanel_3 = (msk & 4) ? true : false;
	FindHistory *fh = findHistory;
	if (! fh)
	{
		NppParameters& nppParams = NppParameters::getInstance();
		fh = & nppParams.getFindHistory();
	}

	if (fh)
	{
		fh->_isFifProjectPanel_1 = _options._isProjectPanel_1;
		fh->_isFifProjectPanel_2 = _options._isProjectPanel_2;
		fh->_isFifProjectPanel_3 = _options._isProjectPanel_3;
	}
	setChecked(IDD_FINDINFILES_PROJECT1_CHECK, _options._isProjectPanel_1);
	setChecked(IDD_FINDINFILES_PROJECT2_CHECK, _options._isProjectPanel_2);
	setChecked(IDD_FINDINFILES_PROJECT3_CHECK, _options._isProjectPanel_3);

	if (_currentStatus == FINDINPROJECTS_DLG)
	{
		enableFindDlgItem (IDD_FINDINFILES_FIND_BUTTON, msk != 0);
		enableFindDlgItem (IDD_FINDINFILES_REPLACEINPROJECTS, msk != 0);
	}
}

bool FindReplaceDlg::replaceInFilesConfirmCheck(const wstring& directory, const wstring& fileTypes)
{
	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

	wstring title = pNativeSpeaker->getLocalizedStrFromID("replace-in-files-confirm-title", L"Are you sure?");

	wstring msg = pNativeSpeaker->getLocalizedStrFromID("replace-in-files-confirm-directory", L"Are you sure you want to replace all occurrences in:");
	msg += L"\n\n";
	msg += directory;

	msg += L"\n\n";

	wstring msg2 = pNativeSpeaker->getLocalizedStrFromID("replace-in-files-confirm-filetype", L"For file type:");
	msg2 += L"\n\n";
	msg2 += fileTypes[0] ? fileTypes : L"*.*";

	msg += msg2;

	const bool confirmed = nplx::uiServices()->messageBox(msg.c_str(), title.c_str(), MB_OKCANCEL | MB_DEFBUTTON2 | MB_TASKMODAL) == IDOK;
	return confirmed;
}

bool FindReplaceDlg::replaceInOpenDocsConfirmCheck()
{
	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	wstring title = pNativeSpeaker->getLocalizedStrFromID("replace-in-open-docs-confirm-title", L"Are you sure?");
	wstring msg = pNativeSpeaker->getLocalizedStrFromID("replace-in-open-docs-confirm-message", L"Are you sure you want to replace all occurrences in all open documents?");

	const bool confirmed = nplx::uiServices()->messageBox(msg.c_str(), title.c_str(), MB_OKCANCEL | MB_DEFBUTTON2 | MB_TASKMODAL) == IDOK;
	return confirmed;
}

void FindReplaceDlg::updateInSelectionCheck()
{
	// upstream WM_ACTIVATE (WA_ACTIVE / WA_CLICKACTIVE)
	Sci_CharacterRangeFull cr = (*_ppEditView)->getSelection();
	intptr_t nbSelected = (*_ppEditView)->execute(SCI_COUNTCHARACTERS, cr.cpMin, cr.cpMax);

	bool inSelEnabled = nbSelected != 0;

	// Searching/replacing in multiple selections or column selection is not allowed
	if (((*_ppEditView)->execute(SCI_GETSELECTIONMODE) == SC_SEL_RECTANGLE) ||
		((*_ppEditView)->execute(SCI_GETSELECTIONS) > 1))
	{
		inSelEnabled = false;
	}

	enableFindDlgItem(IDC_IN_SELECTION_CHECK, inSelEnabled);

	bool inSelChecked = isCheckedOrNot(IDC_IN_SELECTION_CHECK);

	const NppGUI& nppGui = (NppParameters::getInstance()).getNppGUI();
	if (nppGui._inSelectionAutocheckThreshold != 0)
	{
		// code is allowed to change checkmark status of In-selection checkbox

		inSelChecked = inSelEnabled && (nbSelected >= nppGui._inSelectionAutocheckThreshold);

		setChecked(IDC_IN_SELECTION_CHECK, inSelChecked);
	}

	_options._isInSelection = inSelEnabled && inSelChecked;

	// At very first time (when find dlg is launched), search mode is Normal.
	// In that case, ". Matches newline" should be disabled as it applicable only for Regex
	enableFindDlgItem(IDREDOTMATCHNL, isCheckedOrNot(IDREGEXP));
}

void FindReplaceDlg::changeEvent(QEvent* event)
{
	if (event->type() == QEvent::ActivationChange && _isCreated)
	{
		if (isActiveWindow())
			updateInSelectionCheck();
		enableProjectCheckmarks(); // upstream: on every WM_ACTIVATE

		if (isCheckedOrNot(IDC_TRANSPARENT_LOSSFOCUS_RADIO))
		{
			if (!isActiveWindow() && isVisible())
			{
				auto* slider = qobject_cast<QSlider*>(ctrl(IDC_PERCENTAGE_SLIDER));
				int percent = slider ? slider->value() : 150;
				setWindowOpacity(1.0 - percent / 255.0); // NPLX: was NppParameters::setTransparent
			}
			else
			{
				setWindowOpacity(1.0); // NPLX: was NppParameters::removeTransparent
			}
		}
	}
	QDialog::changeEvent(event);
}

void FindReplaceDlg::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	adjustSize();
}

void FindReplaceDlg::reject()
{
	command(IDCANCEL);
}

bool FindReplaceDlg::eventFilter(QObject* watched, QEvent* event)
{
	// upstream ComboEditProc + IsDialogMessage: Enter / Shift+Enter in the combo boxes
	if (event->type() == QEvent::KeyPress && qobject_cast<QLineEdit*>(watched))
	{
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
		{
			switch (_currentStatus)
			{
				case FIND_DLG:
				case REPLACE_DLG:
				{
					if (isCheckedOrNot(IDC_2_BUTTONS_MODE))
						command((keyEvent->modifiers() & Qt::ShiftModifier) ? IDC_FINDPREV : IDC_FINDNEXT);
					else
					{
						// IDOK reverses the direction with Shift (upstream GetKeyState(VK_SHIFT))
						const bool directionBak = _options._whichDirection;
						if (keyEvent->modifiers() & Qt::ShiftModifier)
							_options._whichDirection = !_options._whichDirection;
						const bool shiftPressed = keyEvent->modifiers() & Qt::ShiftModifier;
						_shiftForOk = shiftPressed;
						command(IDOK);
						_shiftForOk = false;
						_options._whichDirection = directionBak;
					}
					break;
				}
				case FINDINFILES_DLG:
				case FINDINPROJECTS_DLG:
					command(IDD_FINDINFILES_FIND_BUTTON);
					break;
				case MARK_DLG:
					command(IDCMARKALL);
					break;
			}
			return true;
		}
	}
	return QDialog::eventFilter(watched, event);
}

void FindReplaceDlg::command(int id)
{
	// upstream run_dlgProc WM_COMMAND
	NppParameters& nppParamInst = NppParameters::getInstance();
	FindHistory& findHistory = nppParamInst.getFindHistory();
	bool isMacroRecording = (static_cast<MacroStatus>(_sendToParent(NPPM_GETCURRENTMACROSTATUS,0,0)) == MacroStatus::RecordInProgress);

	switch (id)
	{
		case IDC_2_BUTTONS_MODE:
		{
			bool is2ButtonsMode = isCheckedOrNot(IDC_2_BUTTONS_MODE);
			findHistory._isSearch2ButtonsMode = is2ButtonsMode;

			showFindDlgItem(IDC_FINDPREV, is2ButtonsMode);
			showFindDlgItem(IDC_FINDNEXT, is2ButtonsMode);
			showFindDlgItem(IDOK, !is2ButtonsMode);
			return;
		}

		case IDCANCEL:
			(*_ppEditView)->execute(SCI_CALLTIPCANCEL);
			setStatusbarMessage(wstring(), FSNoMessage);
			hide();
			(*_ppEditView)->setFocus();
			return;

		case IDC_FINDPREV:
		case IDC_FINDNEXT:
		case IDOK : // Find Next : only for FIND_DLG and REPLACE_DLG
		{
			setStatusbarMessage(wstring(), FSNoMessage);

			_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
			updateCombo(IDFINDWHAT);

			nppParamInst._isFindReplacing = true;

			bool direction_bak = _options._whichDirection;

			if (id == IDC_FINDPREV)
			{
				_options._whichDirection = DIR_UP;
			}
			else if (id == IDC_FINDNEXT)
			{
				_options._whichDirection = DIR_DOWN;
			}
			else // IDOK
			{
				// if shift-key is pressed, revert search direction
				// if shift-key is not pressed, use the normal setting
				// NPLX: the key handler (eventFilter) already reversed it for Shift+Enter
				if (!_shiftForOk && (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier))
				{
					_options._whichDirection = !_options._whichDirection;
				}
			}

			if ((_options._whichDirection == DIR_UP) && (_options._searchType == FindRegex) && !nppParamInst.regexBackward4PowerUser())
			{
				// this can only happen when shift-key was pressed
				// regex upward search is disabled
				// turn user action into a no-action step

				regexBackwardMsgBox();
			}
			else
			{
				if (isMacroRecording)
					saveInMacro(IDOK, FR_OP_FIND);

				FindStatus findStatus = FSFound;
				processFindNext(_options._str2Search.c_str(), _env, &findStatus);

				NativeLangSpeaker *pNativeSpeaker = nppParamInst.getNativeLangSpeaker();
				if (findStatus == FSEndReached)
				{
					wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-end-reached", FIND_STATUS_END_REACHED_TEXT);
					setStatusbarMessage(msg, FSEndReached);
				}
				else if (findStatus == FSTopReached)
				{
					wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-top-reached", FIND_STATUS_TOP_REACHED_TEXT);
					setStatusbarMessage(msg, FSTopReached);
				}
			}

			// restore search direction which may have been overwritten because shift-key was pressed
			_options._whichDirection = direction_bak;

			nppParamInst._isFindReplacing = false;
			return;
		}

		case IDC_SWAP_FIND_REPLACE:
		{
			if (_swapButtonStatus != swap)
			{
				_swapButtonStatus = swap;
				if (auto* button = qobject_cast<QPushButton*>(ctrl(IDD_FINDREPLACE_SWAP_BUTTON)))
					button->setText(QStringLiteral("⇅"));
			}
			command(IDD_FINDREPLACE_SWAP_BUTTON);
			return;
		}

		case IDC_COPY_FIND2REPLACE:
		{
			if (_swapButtonStatus != down)
			{
				_swapButtonStatus = down;
				if (auto* button = qobject_cast<QPushButton*>(ctrl(IDD_FINDREPLACE_SWAP_BUTTON)))
					button->setText(QStringLiteral("⤵"));
			}
			command(IDD_FINDREPLACE_SWAP_BUTTON);
			return;
		}

		case IDC_COPY_REPLACE2FIND:
		{
			if (_swapButtonStatus != up)
			{
				_swapButtonStatus = up;
				if (auto* button = qobject_cast<QPushButton*>(ctrl(IDD_FINDREPLACE_SWAP_BUTTON)))
					button->setText(QStringLiteral("⤴"));
			}
			command(IDD_FINDREPLACE_SWAP_BUTTON);
			return;
		}

		case IDD_FINDREPLACE_SWAP_BUTTON:
		{
			QComboBox* hFindWhat = comboBox(IDFINDWHAT);
			wstring findWhatText = getTextFromCombo(hFindWhat);
			QComboBox* hPlaceWith = comboBox(IDREPLACEWITH);
			wstring replaceWithText = getTextFromCombo(hPlaceWith);

			if (_swapButtonStatus == swap)
			{
				if ((!findWhatText.empty() || !replaceWithText.empty()) && (findWhatText != replaceWithText))
				{
					hFindWhat->setEditText(QString::fromStdWString(replaceWithText));
					hPlaceWith->setEditText(QString::fromStdWString(findWhatText));
				}
			}
			else if (_swapButtonStatus == down)
			{
				if (!findWhatText.empty() && (findWhatText != replaceWithText))
				{
					hPlaceWith->setEditText(QString::fromStdWString(findWhatText));
				}
			}
			else // if (_swapButtonStatus == up)
			{
				if (!replaceWithText.empty() && (findWhatText != replaceWithText))
				{
					hFindWhat->setEditText(QString::fromStdWString(replaceWithText));
				}
			}
			return;
		}

		case IDREPLACE:
		{
			std::lock_guard<std::mutex> lock(findOps_mutex);

			if (_currentStatus == REPLACE_DLG)
			{
				setStatusbarMessage(L"", FSNoMessage);
				_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
				_options._str4Replace = getTextFromCombo(comboBox(IDREPLACEWITH));
				updateCombos();

				nppParamInst._isFindReplacing = true;
				if (isMacroRecording) saveInMacro(id, FR_OP_REPLACE);
				processReplace(_options._str2Search.c_str(), _options._str4Replace.c_str());
				nppParamInst._isFindReplacing = false;
			}
			return;
		}

		case IDC_REPLACEANDSTOP:
		{
			NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
			nppGUI._replaceStopsWithoutFindingNext = isCheckedOrNot(IDC_REPLACEANDSTOP);
			return;
		}

//Process actions
		case IDC_FINDALL_OPENEDFILES :
		{
			if (_currentStatus == FIND_DLG)
			{
				setStatusbarMessage(L"", FSNoMessage);
				_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
				updateCombo(IDFINDWHAT);

				nppParamInst._isFindReplacing = true;
				if (isMacroRecording) saveInMacro(id, FR_OP_FIND + FR_OP_GLOBAL);
				findAllIn(ALL_OPEN_DOCS);
				nppParamInst._isFindReplacing = false;
			}
			return;
		}

		case IDC_FINDALL_CURRENTFILE :
		{
			setStatusbarMessage(L"", FSNoMessage);
			_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
			updateCombo(IDFINDWHAT);

			nppParamInst._isFindReplacing = true;
			if (isMacroRecording) saveInMacro(id, FR_OP_FIND + FR_OP_GLOBAL);
			findAllIn(_options._isInSelection ? CURR_DOC_SELECTION : CURRENT_DOC);
			nppParamInst._isFindReplacing = false;
			return;
		}

		case IDD_FINDINFILES_FIND_BUTTON:
		{
			setStatusbarMessage(L"", FSNoMessage);
			QComboBox* filtersCombo = comboBox(IDD_FINDINFILES_FILTERS_COMBO);
			const wstring filters = getTextFromCombo(filtersCombo);
			addText2Combo(filters.c_str(), filtersCombo);
			filtersCombo->setEditText(QString::fromStdWString(filters));
			_options._filters = filters;

			_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
			updateCombo(IDFINDWHAT);

			if (_currentStatus == FINDINFILES_DLG)
			{
				_options._directory = getTextFromCombo(comboBox(IDD_FINDINFILES_DIR_COMBO));
				trim(_options._directory);
				if (!_options._directory.empty())
				{
					addText2Combo(_options._directory.c_str(), comboBox(IDD_FINDINFILES_DIR_COMBO));
					if (_options._directory.back() != L'/') // NPLX: '/' (upstream '\\')
					{
						_options._directory += L"/";
					}
					nppParamInst._isFindReplacing = true;
					if (isMacroRecording) saveInMacro(id, FR_OP_FIND + FR_OP_FIF);
					findAllIn(FILES_IN_DIR);
					nppParamInst._isFindReplacing = false;
				}
				else
				{
					// move input focus to "Directory:" edit control
					ctrl(IDD_FINDINFILES_DIR_COMBO)->setFocus();
				}
			}
			else if (_currentStatus == FINDINPROJECTS_DLG)
			{
				if (_options._isProjectPanel_1 || _options._isProjectPanel_2 || _options._isProjectPanel_3)
				{
					nppParamInst._isFindReplacing = true;
					if (isMacroRecording) saveInMacro(IDD_FINDINFILES_FINDINPROJECTS, FR_OP_FIND + FR_OP_FIP);
					findAllIn(FILES_IN_PROJECTS);
					nppParamInst._isFindReplacing = false;
				}
			}
			return;
		}

		case IDD_FINDINFILES_REPLACEINPROJECTS:
		{
			std::lock_guard<std::mutex> lock(findOps_mutex);

			setStatusbarMessage(L"", FSNoMessage);
			QComboBox* filtersCombo = comboBox(IDD_FINDINFILES_FILTERS_COMBO);
			const wstring filters = getTextFromCombo(filtersCombo);
			addText2Combo(filters.c_str(), filtersCombo);
			filtersCombo->setEditText(QString::fromStdWString(filters));
			_options._filters = filters;
			if (replaceInProjectsConfirmCheck())
			{
				_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
				_options._str4Replace = getTextFromCombo(comboBox(IDREPLACEWITH));
				updateCombo(IDFINDWHAT);
				updateCombo(IDREPLACEWITH);

				nppParamInst._isFindReplacing = true;
				if (isMacroRecording) saveInMacro(id, FR_OP_REPLACE + FR_OP_FIP);
				_sendToParent(WM_REPLACEINPROJECTS, 0, 0);
				nppParamInst._isFindReplacing = false;
			}
			return;
		}

		case IDD_FINDINFILES_PROJECT1_CHECK:
		case IDD_FINDINFILES_PROJECT2_CHECK:
		case IDD_FINDINFILES_PROJECT3_CHECK:
		{
			if (_currentStatus == FINDINPROJECTS_DLG)
			{
				switch (id)
				{
					case IDD_FINDINFILES_PROJECT1_CHECK:
						findHistory._isFifProjectPanel_1 = _options._isProjectPanel_1 = isCheckedOrNot(IDD_FINDINFILES_PROJECT1_CHECK);
						break;
					case IDD_FINDINFILES_PROJECT2_CHECK:
						findHistory._isFifProjectPanel_2 = _options._isProjectPanel_2 = isCheckedOrNot(IDD_FINDINFILES_PROJECT2_CHECK);
						break;
					case IDD_FINDINFILES_PROJECT3_CHECK:
						findHistory._isFifProjectPanel_3 = _options._isProjectPanel_3 = isCheckedOrNot(IDD_FINDINFILES_PROJECT3_CHECK);
						break;
				}
				bool enable = _options._isProjectPanel_1 || _options._isProjectPanel_2 || _options._isProjectPanel_3;
				enableFindDlgItem(IDD_FINDINFILES_FIND_BUTTON, enable);
				enableFindDlgItem(IDD_FINDINFILES_REPLACEINPROJECTS, enable);
			}
			return;
		}

		case IDD_FINDINFILES_REPLACEINFILES:
		{
			std::lock_guard<std::mutex> lock(findOps_mutex);

			setStatusbarMessage(L"", FSNoMessage);
			QComboBox* filtersCombo = comboBox(IDD_FINDINFILES_FILTERS_COMBO);
			const wstring filters = getTextFromCombo(filtersCombo);
			addText2Combo(filters.c_str(), filtersCombo);
			filtersCombo->setEditText(QString::fromStdWString(filters));
			_options._filters = filters;

			_options._directory = getTextFromCombo(comboBox(IDD_FINDINFILES_DIR_COMBO));
			trim(_options._directory);
			if (!_options._directory.empty())
			{
				if (replaceInFilesConfirmCheck(_options._directory, _options._filters))
				{
					addText2Combo(_options._directory.c_str(), comboBox(IDD_FINDINFILES_DIR_COMBO));
					if (_options._directory.back() != L'/') // NPLX: '/' (upstream '\\')
					{
						_options._directory += L"/";
					}

					_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
					_options._str4Replace = getTextFromCombo(comboBox(IDREPLACEWITH));
					updateCombo(IDFINDWHAT);
					updateCombo(IDREPLACEWITH);

					nppParamInst._isFindReplacing = true;
					if (isMacroRecording) saveInMacro(id, FR_OP_REPLACE + FR_OP_FIF);
					_sendToParent(WM_REPLACEINFILES, 0, 0);
					nppParamInst._isFindReplacing = false;
				}
			}
			else
			{
				// move input focus to "Directory:" edit control
				ctrl(IDD_FINDINFILES_DIR_COMBO)->setFocus();
			}
			return;
		}

		case IDC_CONFIRMREPLACEOPENDOCS:
		{
			NppGUI& nppGui = nppParamInst.getNppGUI();
			nppGui._confirmReplaceInAllOpenDocs = isCheckedOrNot(IDC_CONFIRMREPLACEOPENDOCS);
			return;
		}

		case IDC_REPLACE_OPENEDFILES :
		{
			std::lock_guard<std::mutex> lock(findOps_mutex);

			if (_currentStatus == REPLACE_DLG)
			{
				const NppGUI& nppGui = nppParamInst.getNppGUI();
				if (!nppGui._confirmReplaceInAllOpenDocs || replaceInOpenDocsConfirmCheck())
				{
					setStatusbarMessage(L"", FSNoMessage);
					_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
					_options._str4Replace = getTextFromCombo(comboBox(IDREPLACEWITH));
					updateCombos();

					nppParamInst._isFindReplacing = true;
					if (isMacroRecording) saveInMacro(id, FR_OP_REPLACE + FR_OP_GLOBAL);
					replaceAllInOpenedDocs();
					nppParamInst._isFindReplacing = false;
				}
			}
			return;
		}

		case IDREPLACEALL :
		{
			std::lock_guard<std::mutex> lock(findOps_mutex);

			if (_currentStatus == REPLACE_DLG)
			{
				setStatusbarMessage(L"", FSNoMessage);
				if ((*_ppEditView)->getCurrentBuffer()->isReadOnly())
				{
					NativeLangSpeaker *pNativeSpeaker = nppParamInst.getNativeLangSpeaker();
					wstring msg = pNativeSpeaker->getLocalizedStrFromID("find-status-replace-readonly", L"Replace: Cannot replace text. The current document is read only.");
					setStatusbarMessage(msg, FSNotFound);
					return;
				}

				_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
				_options._str4Replace = getTextFromCombo(comboBox(IDREPLACEWITH));
				updateCombos();

				nppParamInst._isFindReplacing = true;
				if (isMacroRecording) saveInMacro(id, FR_OP_REPLACE);
				(*_ppEditView)->execute(SCI_BEGINUNDOACTION);
				int nbReplaced = processAll(ProcessReplaceAll, &_options);
				(*_ppEditView)->execute(SCI_ENDUNDOACTION);
				nppParamInst._isFindReplacing = false;


				NativeLangSpeaker *pNativeSpeaker = nppParamInst.getNativeLangSpeaker();
				if (nbReplaced == FIND_INVALID_REGULAR_EXPRESSION)
				{
					setStatusbarMessageWithRegExprErr(*_ppEditView);
				}
				else
				{
					wstring result;
					if (nbReplaced == 1)
					{
						result = pNativeSpeaker->getLocalizedStrFromID("find-status-replaceall-1-replaced", L"Replace All: 1 occurrence was replaced");
					}
					else
					{
						result = pNativeSpeaker->getLocalizedStrFromID("find-status-replaceall-nb-replaced", L"Replace All: $INT_REPLACE$ occurrences were replaced");
						result = stringReplace(result, L"$INT_REPLACE$", std::to_wstring(nbReplaced));
					}
					result += L" ";
					result += getScopeInfoForStatusBar(&_options);

					wstring reasonMsg;
					bool isTheMostLaxMode = _options._isWrapAround && !_options._isMatchCase && !_options._isWholeWord;
					if (nbReplaced == 0 && !isTheMostLaxMode)
					{
						reasonMsg = pNativeSpeaker->getLocalizedStrFromID("find-status-cannot-find-pebkac-maybe", noFoundPotentialReason);
					}

					setStatusbarMessage(result, FSMessage, reasonMsg);
				}
				activateWindow();
			}
			return;
		}

		case IDCCOUNTALL :
		{
			if (_currentStatus == FIND_DLG)
			{
				setStatusbarMessage(L"", FSNoMessage);
				updateCombo(IDFINDWHAT);
				_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));

				int nbCounted = processAll(ProcessCountAll, &_options);

				NativeLangSpeaker *pNativeSpeaker = nppParamInst.getNativeLangSpeaker();
				if (nbCounted == FIND_INVALID_REGULAR_EXPRESSION)
				{
					setStatusbarMessageWithRegExprErr(*_ppEditView);
				}
				else
				{
					wstring result;
					if (nbCounted == 1)
					{
						result = pNativeSpeaker->getLocalizedStrFromID("find-status-count-1-match", L"Count: 1 match");
					}
					else //if (nbCounted == 0 || nbCounted > 1)
					{
						result = pNativeSpeaker->getLocalizedStrFromID("find-status-count-nb-matches", L"Count: $INT_REPLACE$ matches");
						result = stringReplace(result, L"$INT_REPLACE$", commafyInt(nbCounted));
					}
					result += L" ";
					result += getScopeInfoForStatusBar(&_options);

					wstring reasonMsg;
					bool isTheMostLaxMode = _options._isWrapAround && !_options._isMatchCase && !_options._isWholeWord;
					if (nbCounted == 0 && !isTheMostLaxMode)
					{
						reasonMsg = pNativeSpeaker->getLocalizedStrFromID("find-status-cannot-find-pebkac-maybe", noFoundPotentialReason);
					}
					else if (_options._regexEmptyStringFound)
					{
						reasonMsg = pNativeSpeaker->getLocalizedStrFromID("find-status-regex-find-empty-string", regexEmptyFoundReason);
					}


					setStatusbarMessage(result, FSMessage, reasonMsg);
				}

				if (isMacroRecording)
					saveInMacro(id, FR_OP_FIND);

				activateWindow();
			}
			return;
		}

		case IDCMARKALL :
		{
			if (_currentStatus == MARK_DLG)
			{
				setStatusbarMessage(L"", FSNoMessage);
				_options._str2Search = getTextFromCombo(comboBox(IDFINDWHAT));
				updateCombo(IDFINDWHAT);

				if (isMacroRecording) saveInMacro(id, FR_OP_FIND);
				nppParamInst._isFindReplacing = true;
				int nbMarked = processAll(ProcessMarkAll, &_options);
				nppParamInst._isFindReplacing = false;


				NativeLangSpeaker *pNativeSpeaker = nppParamInst.getNativeLangSpeaker();
				if (nbMarked == FIND_INVALID_REGULAR_EXPRESSION)
				{
					setStatusbarMessageWithRegExprErr(*_ppEditView);
				}
				else
				{
					std::wstring result;
					if (nbMarked == 1)
					{
						result = pNativeSpeaker->getLocalizedStrFromID("find-status-mark-1-match", L"Mark: 1 match");
					}
					else //if (nbMarked == 0 || nbMarked > 1)
					{
						result = pNativeSpeaker->getLocalizedStrFromID("find-status-mark-nb-matches", L"Mark: $INT_REPLACE$ matches");
						result = stringReplace(result, L"$INT_REPLACE$", std::to_wstring(nbMarked));
					}
					result += L" ";
					result += getScopeInfoForStatusBar(&_options);

					wstring reasonMsg;
					bool isTheMostLaxMode = _options._isWrapAround && !_options._isMatchCase && !_options._isWholeWord;
					if (nbMarked == 0 && !isTheMostLaxMode)
					{
						reasonMsg = pNativeSpeaker->getLocalizedStrFromID("find-status-cannot-find-pebkac-maybe", noFoundPotentialReason);
					}
					else if (_options._regexEmptyStringFound)
					{
						reasonMsg = pNativeSpeaker->getLocalizedStrFromID("find-status-regex-find-empty-string", regexEmptyFoundReason);
					}

					setStatusbarMessage(result, FSMessage, reasonMsg);
				}

				activateWindow();
			}
			return;
		}

		case IDC_CLEAR_ALL :
		{
			if (_currentStatus == MARK_DLG)
			{
				if (isMacroRecording) saveInMacro(id, FR_OP_FIND);
				clearMarks(_options);
			}
			return;
		}

		case IDC_COPY_MARKED_TEXT:
		{
			_sendToParent(WM_COMMAND, IDM_SEARCH_MARKEDTOCLIP, 0);
			return;
		}

//Option actions
		case IDREDOTMATCHNL:
			findHistory._dotMatchesNewline = _options._dotMatchesNewline = isCheckedOrNot(IDREDOTMATCHNL);
			return;

		case IDWHOLEWORD :
			findHistory._isMatchWord = _options._isWholeWord = isCheckedOrNot(IDWHOLEWORD);
			return;

		case IDMATCHCASE :
			findHistory._isMatchCase = _options._isMatchCase = isCheckedOrNot(IDMATCHCASE);
			return;

		case IDNORMAL:
		case IDEXTENDED:
		case IDREGEXP :
		{
			if (isCheckedOrNot(IDREGEXP))
			{
				_options._searchType = FindRegex;
				findHistory._searchMode = FindHistory::regExpr;
				enableFindDlgItem(IDREDOTMATCHNL);
			}
			else if (isCheckedOrNot(IDEXTENDED))
			{
				_options._searchType = FindExtended;
				findHistory._searchMode = FindHistory::extended;
				enableFindDlgItem(IDREDOTMATCHNL, false);
			}
			else
			{
				_options._searchType = FindNormal;
				findHistory._searchMode = FindHistory::normal;
				enableFindDlgItem(IDREDOTMATCHNL, false);
			}

			bool isRegex = (_options._searchType == FindRegex);
			if (isRegex)
			{
				//regex doesn't allow whole word
				_options._isWholeWord = false;
				setChecked(IDWHOLEWORD, _options._isWholeWord);

				//regex upward search is disabled
				if (!nppParamInst.regexBackward4PowerUser())
				{
					setChecked(IDC_BACKWARDDIRECTION, false);
					_options._whichDirection = DIR_DOWN;
				}
			}

			enableFindDlgItem(IDWHOLEWORD, !isRegex);

			// regex upward search is disabled
			bool doEnable = true;
			if (isRegex && !nppParamInst.regexBackward4PowerUser())
			{
				doEnable = false;
			}
			enableFindDlgItem(IDC_BACKWARDDIRECTION, doEnable);
			enableFindDlgItem(IDC_FINDPREV, doEnable);

			return;
		}

		case IDWRAP :
			findHistory._isWrap = _options._isWrapAround = isCheckedOrNot(IDWRAP);
			return;

		case IDC_BACKWARDDIRECTION:
			_options._whichDirection = isCheckedOrNot(IDC_BACKWARDDIRECTION) ? DIR_UP : DIR_DOWN;
			findHistory._isDirectionDown = _options._whichDirection == DIR_DOWN;
			return;

		case IDC_PURGE_CHECK :
		{
			if (_currentStatus == MARK_DLG)
				findHistory._isPurge = _options._doPurge = isCheckedOrNot(IDC_PURGE_CHECK);
			return;
		}

		case IDC_MARKLINE_CHECK :
		{
			if (_currentStatus == MARK_DLG)
				findHistory._isBookmarkLine = _options._doMarkLine = isCheckedOrNot(IDC_MARKLINE_CHECK);
			return;
		}

		case IDC_IN_SELECTION_CHECK :
		{
			if ((_currentStatus == FIND_DLG) || (_currentStatus == REPLACE_DLG) || (_currentStatus == MARK_DLG))
			{
				_options._isInSelection = ctrl(IDC_IN_SELECTION_CHECK)->isEnabled() &&
					isCheckedOrNot(IDC_IN_SELECTION_CHECK);
			}
			return;
		}

		case IDC_TRANSPARENT_CHECK :
		{
			bool isChecked = isCheckedOrNot(IDC_TRANSPARENT_CHECK);

			enableFindDlgItem(IDC_TRANSPARENT_LOSSFOCUS_RADIO, isChecked);
			enableFindDlgItem(IDC_TRANSPARENT_ALWAYS_RADIO, isChecked);
			enableFindDlgItem(IDC_PERCENTAGE_SLIDER, isChecked);

			if (isChecked)
			{
				setChecked(IDC_TRANSPARENT_LOSSFOCUS_RADIO, true);
				findHistory._transparencyMode = FindHistory::onLosingFocus;
			}
			else
			{
				setChecked(IDC_TRANSPARENT_LOSSFOCUS_RADIO, false);
				setChecked(IDC_TRANSPARENT_ALWAYS_RADIO, false);
				setWindowOpacity(1.0); // NPLX: was NppParameters::removeTransparent
				findHistory._transparencyMode = FindHistory::none;
			}
			return;
		}

		case IDC_TRANSPARENT_ALWAYS_RADIO :
		{
			auto* slider = qobject_cast<QSlider*>(ctrl(IDC_PERCENTAGE_SLIDER));
			int percent = slider ? slider->value() : 150;
			setWindowOpacity(1.0 - percent / 255.0); // NPLX: was NppParameters::setTransparent
			findHistory._transparencyMode = FindHistory::persistent;
			return;
		}

		case IDC_TRANSPARENT_LOSSFOCUS_RADIO :
		{
			setWindowOpacity(1.0); // NPLX: was NppParameters::removeTransparent
			findHistory._transparencyMode = FindHistory::onLosingFocus;
			return;
		}

		case IDD_FINDINFILES_RECURSIVE_CHECK :
		{
			if (_currentStatus == FINDINFILES_DLG)
				findHistory._isFifRecursive = _options._isRecursive = isCheckedOrNot(IDD_FINDINFILES_RECURSIVE_CHECK);
			return;
		}

		case IDD_FINDINFILES_INHIDDENDIR_CHECK :
		{
			if (_currentStatus == FINDINFILES_DLG)
				findHistory._isFifInHiddenFolder = _options._isInHiddenDir = isCheckedOrNot(IDD_FINDINFILES_INHIDDENDIR_CHECK);
			return;
		}

		case IDD_FINDINFILES_SETDIRFROMDOC_BUTTON :
		{
			wstring currPath;
			const Buffer* buf = (*_ppEditView)->getCurrentBuffer();
			if (!(buf->getStatus() & (DOC_UNNAMED | DOC_DELETED)))
			{
				currPath = buf->getFullPathName();
				pathRemoveFileSpec(currPath);
			}

			if (!currPath.empty() && doesDirectoryExist(currPath.c_str()))
			{
				setFindInFilesDirFilter(currPath.c_str(), NULL);
			}
			return;
		}

		case IDD_FINDINFILES_BROWSE_BUTTON :
		{
			if (_currentStatus == FINDINFILES_DLG)
			{
				NativeLangSpeaker* pNativeSpeaker = nppParamInst.getNativeLangSpeaker();
				const wstring title = pNativeSpeaker->getLocalizedStrFromID("find-in-files-select-folder", L"Select a folder to search from");
				// NPLX: upstream folderBrowser(_hSelf, title, IDD_FINDINFILES_DIR_COMBO, dir)
				const QString dir = QFileDialog::getExistingDirectory(this, QString::fromStdWString(title), QString::fromStdWString(getTextFromCombo(comboBox(IDD_FINDINFILES_DIR_COMBO))));
				if (!dir.isEmpty())
					comboBox(IDD_FINDINFILES_DIR_COMBO)->setEditText(dir);
			}
			return;
		}

		default:
			break;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FindIncrementDlg

FindIncrementDlg::FindIncrementDlg(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("FindIncrementDlg"));
}

void FindIncrementDlg::init(FindReplaceDlg* pFRDlg, bool isRTL)
{
	if (!pFRDlg)
		throw std::runtime_error("FindIncrementDlg::init : Parameter pFRDlg is null");
	_pFRDlg = pFRDlg;
	_isRTL = isRTL;
	if (isRTL)
		setLayoutDirection(Qt::RightToLeft);

	// upstream IDD_INCREMENT_FIND: ✕, "Find:", the text, <, >, Match case, Highlight all, Count, the status, the
	// position toggle
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(6, 3, 6, 3);
	layout->setSpacing(6);

	auto makeButton = [this, layout](int id, const QString& text, const QString& tip) {
		auto* button = new QPushButton(text, this);
		button->setAutoDefault(false);
		button->setProperty("compact", true);
		button->setFixedWidth(28);
		button->setToolTip(tip);
		_ctrls[id] = button;
		connect(button, &QPushButton::clicked, this, [this, id]() { command(id); });
		layout->addWidget(button);
		return button;
	};
	auto makeCheck = [this, layout](int id, const QString& text) {
		auto* check = new QCheckBox(text, this);
		_ctrls[id] = check;
		connect(check, &QCheckBox::clicked, this, [this, id]() { command(id); });
		layout->addWidget(check);
		return check;
	};

	makeButton(IDCANCEL, QStringLiteral("✕"), QStringLiteral("Close"));
	auto* label = new QLabel(QStringLiteral("Find:"), this);
	_ctrls[IDC_INCSTATIC] = label;
	layout->addWidget(label);
	auto* edit = new QLineEdit(this);
	edit->setMaxLength(FINDREPLACE_MAXLENGTH - 1);
	edit->setMinimumWidth(260);
	edit->installEventFilter(this);
	label->setBuddy(edit);
	_ctrls[IDC_INCFINDTEXT] = edit;
	connect(edit, &QLineEdit::textEdited, this, [this]() { command(IDC_INCFINDTEXT); }); // upstream EN_CHANGE
	layout->addWidget(edit);
	makeButton(IDC_INCFINDPREVOK, QStringLiteral("<"), QStringLiteral("Find Previous"));
	makeButton(IDC_INCFINDNXTOK, QStringLiteral(">"), QStringLiteral("Find Next"));
	layout->addSpacing(6);
	makeCheck(IDC_INCFINDMATCHCASE, QStringLiteral("Match &case"));
	makeCheck(IDC_INCFINDHILITEALL, QStringLiteral("&Highlight all"));
	makeCheck(IDC_INCFINDCOUNT, QStringLiteral("Count"));
	auto* status = new QLabel(this);
	_ctrls[IDC_INCFINDSTATUS] = status;
	layout->addSpacing(6);
	layout->addWidget(status, 1);
	makeButton(IDC_GOUPDOWN_TOGGLE_BUTTON, QString(), QStringLiteral("Move the bar to the top or the bottom"));

	nplx::changeDlgLang(this, [this](int id) { return dlgItem(id); }, "IncrementalFind");

	// upstream WM_INITDIALOG
	const FindHistory& findHistory = NppParameters::getInstance().getFindHistory();
	static_cast<QCheckBox*>(_ctrls[IDC_INCFINDMATCHCASE])->setChecked(findHistory._isIncrementalMatchCase);
	static_cast<QCheckBox*>(_ctrls[IDC_INCFINDHILITEALL])->setChecked(findHistory._isIncrementalHighlightAll);
	static_cast<QCheckBox*>(_ctrls[IDC_INCFINDCOUNT])->setChecked(findHistory._isIncrementalCount);

	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	static_cast<QPushButton*>(_ctrls[IDC_GOUPDOWN_TOGGLE_BUTTON])->setText(nppGUI._incrementalSearchBarAtTop ? QStringLiteral("˅") : QStringLiteral("˄"));
}

QWidget* FindIncrementDlg::dlgItem(int id) const
{
	auto it = _ctrls.find(id);
	return it == _ctrls.end() ? nullptr : it->second;
}

bool FindIncrementDlg::isCheckedOrNot(int id) const
{
	auto* button = qobject_cast<QAbstractButton*>(dlgItem(id));
	return button && button->isChecked();
}

bool FindIncrementDlg::isVisibleBar() const
{
	return _host ? _host->isVisible() : isVisible();
}

bool FindIncrementDlg::hasFocusWithin() const
{
	QWidget* focus = QApplication::focusWidget();
	return focus && isAncestorOf(focus);
}

void FindIncrementDlg::display(bool toShow)
{
	if (toShow)
	{
		if (_host)
			_host->show();
		show();
		auto* edit = static_cast<QLineEdit*>(dlgItem(IDC_INCFINDTEXT));
		edit->setFocus();
		// select the whole find editor text
		edit->selectAll();
	}
	else if (_host)
	{
		_host->hide();
	}
	else
	{
		hide();
	}
}

void FindIncrementDlg::setSearchText(const wchar_t* txt2find, bool)
{
	static_cast<QLineEdit*>(dlgItem(IDC_INCFINDTEXT))->setText(QString::fromWCharArray(txt2find));
}

bool FindIncrementDlg::eventFilter(QObject* watched, QEvent* event)
{
	// upstream: Enter in the edit field is IDOK (Shift+Enter searches backward), Esc is IDCANCEL
	if (watched == dlgItem(IDC_INCFINDTEXT) && event->type() == QEvent::KeyPress)
	{
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
		{
			command(IDOK, keyEvent->modifiers() & Qt::ShiftModifier);
			return true;
		}
		if (keyEvent->key() == Qt::Key_Escape)
		{
			command(IDCANCEL);
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

void FindIncrementDlg::command(int id, bool isShiftDown)
{
	bool updateSearch = false;
	bool forward = true;
	bool advance = false;
	bool updateHiLight = false;
	bool updateCase = false;

	switch (id)
	{
		case IDC_GOUPDOWN_TOGGLE_BUTTON:
		{
			NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
			nppGUI._incrementalSearchBarAtTop = !nppGUI._incrementalSearchBarAtTop;
			static_cast<QPushButton*>(dlgItem(IDC_GOUPDOWN_TOGGLE_BUTTON))->setText(nppGUI._incrementalSearchBarAtTop ? QStringLiteral("˅") : QStringLiteral("˄"));
			emit positionToggled(); // upstream ::SendMessage(_hParent, NPPM_INTERNAL_INCSEARCHBARPOSITION, 0, 0)
			return;
		}

		case IDCANCEL:
			(*(_pFRDlg->_ppEditView))->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE_INC);
			(*(_pFRDlg->_ppEditView))->setFocus(); // upstream grabFocus
			display(false);
			_matches.clear();
			return;

		case IDM_SEARCH_FINDINCREMENT:	// Accel table: Start incremental search
			// if focus is on a some other control, return it to the edit field
			if (QApplication::focusWidget() != dlgItem(IDC_INCFINDTEXT))
			{
				dlgItem(IDC_INCFINDTEXT)->setFocus();
				return;
			}
			// otherwise, repeat the search
			[[fallthrough]];
		case IDM_SEARCH_FINDPREV:		// Accel table: find prev
		case IDM_SEARCH_FINDNEXT:		// Accel table: find next
		case IDC_INCFINDPREVOK:
		case IDC_INCFINDNXTOK:
		case IDOK:						// Enter key in the edit field
			updateSearch = true;
			advance = true;
			forward = (id == IDC_INCFINDNXTOK) ||
				(id == IDM_SEARCH_FINDNEXT) ||
				(id == IDM_SEARCH_FINDINCREMENT) ||
				((id == IDOK) && !isShiftDown);
			break;

		case IDC_INCFINDMATCHCASE:
			NppParameters::getInstance().getFindHistory()._isIncrementalMatchCase = isCheckedOrNot(IDC_INCFINDMATCHCASE);
			updateSearch = true;
			updateCase = true;
			updateHiLight = true;
			_matches.clear();
			break;

		case IDC_INCFINDHILITEALL:
			NppParameters::getInstance().getFindHistory()._isIncrementalHighlightAll = isCheckedOrNot(IDC_INCFINDHILITEALL);
			updateHiLight = true;
			break;

		case IDC_INCFINDCOUNT:
			NppParameters::getInstance().getFindHistory()._isIncrementalCount = isCheckedOrNot(IDC_INCFINDCOUNT);
			_matches.clear();
			break;

		case IDC_INCFINDTEXT: // EN_CHANGE
			updateSearch = true;
			updateHiLight = isCheckedOrNot(IDC_INCFINDHILITEALL);
			updateCase = isCheckedOrNot(IDC_INCFINDMATCHCASE);
			_matches.clear();
			break;

		default:
			return;
	}

	FindOption fo;
	fo._isWholeWord = false;
	fo._incrementalType = advance ? NextIncremental : FirstIncremental;
	fo._whichDirection = forward ? DIR_DOWN : DIR_UP;
	fo._isMatchCase = isCheckedOrNot(IDC_INCFINDMATCHCASE);

	const std::wstring str2Search = static_cast<QLineEdit*>(dlgItem(IDC_INCFINDTEXT))->text().toStdWString();
	if (updateSearch)
	{
		FindStatus findStatus = FSFound;
		MatchPosition match;
		bool isFound = _pFRDlg->processFindNext(str2Search.c_str(), &fo, &findStatus, FINDNEXTTYPE_FINDNEXT, &match);

		fo._str2Search = str2Search;
		intptr_t nth = 0;
		if (isCheckedOrNot(IDC_INCFINDCOUNT))
		{
			if (!_matches.size())
				_pFRDlg->processAll(ProcessCountAll, &fo, false, nullptr, -1, &_matches);
			if (_matches.size() && (match.start > -1) && (match.end > -1))
			{
				size_t totalMatches = _matches.size();
				for (size_t i = 0; i < totalMatches; ++i)
				{
					if ((match.start == _matches[i].start) && (match.end == _matches[i].end))
					{
						nth = i + 1;
						break;
					}
				}
			}
		}
		setFindStatus(findStatus, _matches.size(), static_cast<int>(nth));

		// If case-sensitivity changed (to Match=yes), there may have been a matched selection that
		// now does not match; so if Not Found, clear selection and put caret at beginning of what was
		// selected (no change, if there was no selection)
		if (updateCase && !isFound)
		{
			Sci_CharacterRangeFull range = (*(_pFRDlg->_ppEditView))->getSelection();
			(*(_pFRDlg->_ppEditView))->execute(SCI_SETSEL, static_cast<uptr_t>(-1), range.cpMin);
		}
	}

	if (updateHiLight)
	{
		bool highlight = !str2Search.empty() && isCheckedOrNot(IDC_INCFINDHILITEALL);
		markSelectedTextInc(highlight, &fo);
	}
}

void FindIncrementDlg::markSelectedTextInc(bool enable, FindOption* opt)
{
	(*(_pFRDlg->_ppEditView))->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE_INC);

	if (!enable)
		return;

	//Get selection
	Sci_CharacterRangeFull range = (*(_pFRDlg->_ppEditView))->getSelection();

	//If nothing selected, don't mark anything
	if (range.cpMin == range.cpMax)
		return;

	auto text2Find = (*(_pFRDlg->_ppEditView))->getSelectedTextToWChar(false);	//do not expand selection (false)
	if (text2Find.empty())
		return;
	opt->_str2Search = text2Find;
	_pFRDlg->markAllInc(opt);
}

void FindIncrementDlg::setFindStatus(FindStatus iStatus, size_t nbCounted, int nth)
{
	std::wstring statusStr2Display;

	static const wchar_t* const strFSNotFound = L"Phrase not found";
	static const wchar_t* const strFSTopReached = L"Reached top of page, continued from bottom";
	static const wchar_t* const strFSEndReached = L"Reached end of page, continued from top";

	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

	if (nbCounted > 0)
		statusStr2Display = commafyInt(nth) + L"/" + commafyInt(nbCounted);

	switch (iStatus)
	{
		case FindStatus::FSNotFound:
			statusStr2Display = pNativeSpeaker->getLocalizedStrFromID("IncrementalFind-FSNotFound", strFSNotFound);
			break;

		case FindStatus::FSTopReached:
		{
			auto topReached = pNativeSpeaker->getLocalizedStrFromID("IncrementalFind-FSTopReached", strFSTopReached);
			if (nbCounted > 0)
				statusStr2Display += (L" - " + topReached);
			else
				statusStr2Display = topReached;
			break;
		}

		case FindStatus::FSEndReached:
		{
			auto endReached = pNativeSpeaker->getLocalizedStrFromID("IncrementalFind-FSEndReached", strFSEndReached);
			if (nbCounted > 0)
				statusStr2Display += (L" - " + endReached);
			else
				statusStr2Display = endReached;
			break;
		}

		case FindStatus::FSFound:
			break;

		case FindStatus::FSResetCount:
			statusStr2Display = L"";
			break;

		default:
			return; // out of range
	}

	_findStatus = iStatus;

	// upstream WM_CTLCOLOREDIT: the edit field is red when the text is not found
	auto* edit = static_cast<QLineEdit*>(dlgItem(IDC_INCFINDTEXT));
	if (FSNotFound == iStatus)
		edit->setStyleSheet(QStringLiteral("QLineEdit { background: #%1; color: #%2; }")
			.arg(QColor(GetRValue(BCKGRD_COLOR), GetGValue(BCKGRD_COLOR), GetBValue(BCKGRD_COLOR)).name().mid(1))
			.arg(QColor(GetRValue(TXT_COLOR), GetGValue(TXT_COLOR), GetBValue(TXT_COLOR)).name().mid(1)));
	else
		edit->setStyleSheet(QString());

	static_cast<QLabel*>(dlgItem(IDC_INCFINDSTATUS))->setText(QString::fromStdWString(statusStr2Display));
}
