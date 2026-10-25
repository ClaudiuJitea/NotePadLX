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

// NPLX: the editing helpers of upstream Notepad_plus.cpp (trim, tab/space conversion, empty and duplicate
// lines, bookmarked lines, change history, comments, menu state checks), extracted from upstream with
// mechanical substitutions only (members that are pointers here, Qt clipboard, beep).

#include <algorithm>
#include <cstring>
#include <mutex>

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QPalette>

#include "Notepad_plus.h"

#include "AutoCompletion.h"
#include "DocTabView.h"
#include "Encoding.h"
#include "UrlDetection.h"
#include "FindReplaceDlg.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "menuCmdID.h"

using namespace std;

namespace
{
	std::mutex mark_mutex;
}

void Notepad_plus::checkClipboard()
{
	bool hasSelection = _pEditView->hasSelection();
	bool canPaste = (_pEditView->execute(SCI_CANPASTE) != 0);

	if (!NppParameters::getInstance().getSVP()._lineCopyCutWithoutSelection)
	{
		enableCommand(IDM_EDIT_CUT, hasSelection, MENU | TOOLBAR);

		if (hasSelection)
		{
			enableCommand(IDM_EDIT_COPY, true, MENU | TOOLBAR);
		}
		else if (_findReplaceDlg->allowCopyAction())
		{
			enableCommand(IDM_EDIT_COPY, false, TOOLBAR);
			enableCommand(IDM_EDIT_COPY, true, MENU);
		}
		else
		{
			enableCommand(IDM_EDIT_COPY, false, MENU | TOOLBAR);
		}
	}
	enableCommand(IDM_EDIT_PASTE, canPaste, MENU | TOOLBAR);
	enableCommand(IDM_EDIT_DELETE, hasSelection, MENU | TOOLBAR);
	enableCommand(IDM_EDIT_UPPERCASE, hasSelection, MENU);
	enableCommand(IDM_EDIT_LOWERCASE, hasSelection, MENU);
	enableCommand(IDM_EDIT_PROPERCASE_FORCE, hasSelection, MENU);
	enableCommand(IDM_EDIT_PROPERCASE_BLEND, hasSelection, MENU);
	enableCommand(IDM_EDIT_SENTENCECASE_FORCE, hasSelection, MENU);
	enableCommand(IDM_EDIT_SENTENCECASE_BLEND, hasSelection, MENU);
	enableCommand(IDM_EDIT_INVERTCASE, hasSelection, MENU);
	enableCommand(IDM_EDIT_RANDOMCASE, hasSelection, MENU);
	Buffer* curBuf = _pEditView->getCurrentBuffer();
	bool canRedact = !curBuf->getFileReadOnly() && !curBuf->getUserReadOnly() && hasSelection;
	enableCommand(IDM_EDIT_REDACT_SELECTION, canRedact, MENU);
}

void Notepad_plus::checkUndoState()
{
	enableCommand(IDM_EDIT_UNDO, _pEditView->execute(SCI_CANUNDO) != 0, MENU | TOOLBAR);
	enableCommand(IDM_EDIT_REDO, _pEditView->execute(SCI_CANREDO) != 0, MENU | TOOLBAR);
}

void Notepad_plus::wsTabConvert(spaceTab whichWay)
{
	// block selection is not supported
	if ((_pEditView->execute(SCI_GETSELECTIONMODE) == SC_SEL_RECTANGLE) || (_pEditView->execute(SCI_GETSELECTIONMODE) == SC_SEL_THIN))
		return;

	intptr_t tabWidth = _pEditView->execute(SCI_GETTABWIDTH);
	intptr_t currentPos = _pEditView->execute(SCI_GETCURRENTPOS);
	intptr_t currentLine = _pEditView->execute(SCI_LINEFROMPOSITION, currentPos);
	intptr_t currentPosInLine = currentPos - _pEditView->execute(SCI_POSITIONFROMLINE, currentLine);

	intptr_t startLine = 0;
	intptr_t endLine = _pEditView->lastZeroBasedLineNumber();
	intptr_t endLineCorrect = endLine;
	intptr_t dataLength = _pEditView->execute(SCI_GETLENGTH) + 1;
	intptr_t mainSelAnchor = _pEditView->execute(SCI_GETANCHOR);
	bool isEntireDoc = (mainSelAnchor == currentPos);

	// restore original selection if nothing has changed
	auto restoreSelection = [this, mainSelAnchor, currentPos, isEntireDoc]()
	{
		if (!isEntireDoc)
		{
			_pEditView->execute(SCI_SETANCHOR, mainSelAnchor);
			_pEditView->execute(SCI_SETCURRENTPOS, currentPos);
		}
	};

	// auto-expand of partially selected lines
	if (!isEntireDoc)
	{
		intptr_t startPos = _pEditView->execute(SCI_GETSELECTIONSTART);
		startLine = _pEditView->execute(SCI_LINEFROMPOSITION, startPos);
		intptr_t endPos = _pEditView->execute(SCI_GETSELECTIONEND);
		endLine = endLineCorrect = _pEditView->execute(SCI_LINEFROMPOSITION, endPos);

		if (startPos != _pEditView->execute(SCI_POSITIONFROMLINE, startLine))
			startPos = _pEditView->execute(SCI_POSITIONFROMLINE, startLine);

		if (endPos == _pEditView->execute(SCI_POSITIONFROMLINE, endLine))
			endLineCorrect = endLine - 1;
		else if (endPos < _pEditView->execute(SCI_GETLINEENDPOSITION, endLine))
			endPos = _pEditView->execute(SCI_GETLINEENDPOSITION, endLine);

		dataLength = endPos - startPos + 1;
		_pEditView->execute(SCI_SETSEL, startPos, endPos);
	}

	if (dataLength < 2)
	{
		restoreSelection();
		return;
	}

	intptr_t changeDataCount = 0;
	intptr_t newCurrentPos = 0;
	vector<intptr_t> folding;

	_pEditView->execute(SCI_BEGINUNDOACTION);

	for (intptr_t idx = startLine; idx < endLineCorrect + 1; ++idx)
	{
		intptr_t startPos = _pEditView->execute(SCI_POSITIONFROMLINE, idx);
		intptr_t endPos = _pEditView->execute(SCI_GETLINEENDPOSITION, idx);
		dataLength = endPos - startPos + 1;

		char * source = new char[dataLength];
		if (source == NULL)
			continue;

		source[dataLength - 1] = '\0'; // make sure to have correct data termination
		_pEditView->execute(SCI_SETTARGETRANGE, startPos, endPos);
		_pEditView->execute(SCI_GETTARGETTEXT, 0, reinterpret_cast<LPARAM>(source));

		intptr_t count = 0;
		intptr_t column = 0;
		intptr_t tabStop = tabWidth - 1;   // remember, counting from zero !
		bool onlyLeading = false;

		if (whichWay == tab2Space)
		{
			// count how many tabs are there
			for (const char * ch = source; *ch; ++ch)
			{
				if (*ch == '\t')
					++count;
			}
			if (count == 0)
			{
				delete [] source;
				continue;
			}
		}
		// allocate tabwidth-1 chars extra per tab, just to be safe
		size_t newLen = dataLength + count * (tabWidth - 1) + 1;
		char * destination = new char[newLen];
		if (destination == NULL)
		{
			delete [] source;
			continue;
		}
		char * dest = destination;
		intptr_t changeDataLineCount = 0;

		switch (whichWay)
		{
			case tab2Space:
			{
				// rip through each line of the file
				for (int i = 0; source[i] != '\0'; ++i)
				{
					if (source[i] == '\t')
					{
						intptr_t insertTabs = tabWidth - (column % tabWidth);
						for (int j = 0; j < insertTabs; ++j)
						{
							*dest++ = ' ';
							changeDataCount++;
							changeDataLineCount++;
							if (idx == currentLine && i < currentPosInLine)
								++newCurrentPos;
						}
						column += insertTabs;
					}
					else
					{
						*dest++ = source[i];
						if (idx == currentLine && i < currentPosInLine)
							++newCurrentPos;
						if ((source[i] == '\n') || (source[i] == '\r'))
							column = 0;
						else if ((source[i] & 0xC0) != 0x80)  // UTF_8 support: count only bytes that don't start with 10......
							++column;
					}
				}
				*dest = '\0';
				break;
			}
			case space2TabLeading:
			{
				onlyLeading = true;
			}
			[[fallthrough]];
			case space2TabAll:
			{
				bool nextChar = false;
				int counter = 0;
				bool nonSpaceFound = false;
				for (int i = 0; source[i] != '\0'; ++i)
				{
					if (nonSpaceFound == false)
					{
						while (source[i + counter] == ' ')
						{
							if ((column + counter) == tabStop)
							{
								tabStop += tabWidth;
								if (counter >= 1)        // counter is counted from 0, so counter >= max-1
								{
									*dest++ = '\t';
									changeDataCount++;
									changeDataLineCount++;
									i += counter;
									column += counter + 1;
									counter = 0;
									nextChar = true;
									if (idx == currentLine && i <= currentPosInLine)
										++newCurrentPos;
									break;
								}
								else if (source[i + 1] == ' ' || source[i + 1] == '\t')  // if followed by space or TAB, convert even a single space to TAB
								{
									*dest++ = '\t';
									changeDataCount++;
									changeDataLineCount++;
									i++;
									column += 1;
									counter = 0;
									if (idx == currentLine && i <= currentPosInLine)
										++newCurrentPos;
								}
								else       // single space, don't convert it to TAB
								{
									*dest++ = source[i];
									column += 1;
									counter = 0;
									nextChar = true;
									if (idx == currentLine && i <= currentPosInLine)
										++newCurrentPos;
									break;
								}
							}
							else
								++counter;
						}

						if (nextChar == true)
						{
							nextChar = false;
							continue;
						}

						if (source[i] == ' ' && source[i + counter] == '\t') // spaces "absorbed" by a TAB on the right
						{
							*dest++ = '\t';
							changeDataCount++;
							changeDataLineCount++;
							i += counter;
							column = tabStop + 1;
							tabStop += tabWidth;
							counter = 0;
							if (idx == currentLine && i <= currentPosInLine)
								++newCurrentPos;
							continue;
						}
					}

					if (onlyLeading == true && nonSpaceFound == false)
						nonSpaceFound = true;

					if (source[i] == '\n' || source[i] == '\r')
					{
						*dest++ = source[i];
						column = 0;
						tabStop = tabWidth - 1;
						nonSpaceFound = false;
					}
					else if (source[i] == '\t')
					{
						*dest++ = source[i];
						column = tabStop + 1;
						tabStop += tabWidth;
						counter = 0;
					}
					else
					{
						*dest++ = source[i];
						counter = 0;
						if ((source[i] & 0xC0) != 0x80)   // UTF_8 support: count only bytes that don't start with 10......
						{
							++column;

							if (column > 0 && column % tabWidth == 0)
								tabStop += tabWidth;
						}
					}

					if (idx == currentLine && i < currentPosInLine)
						++newCurrentPos;
				}
				*dest = '\0';
				break;
			}
		}

		if ((_pEditView->execute(SCI_GETFOLDLEVEL, idx) & SC_FOLDLEVELHEADERFLAG))
			if (_pEditView->execute(SCI_GETFOLDEXPANDED, idx) == 0)
				folding.push_back(idx);

		if (changeDataLineCount)
			_pEditView->execute(SCI_REPLACETARGET, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(destination));

		// clean up
		delete [] source;
		delete [] destination;
	
	}

	_pEditView->execute(SCI_ENDUNDOACTION);

	if (changeDataCount)
	{
		if (!isEntireDoc)
			_pEditView->execute(SCI_SETSEL, _pEditView->execute(SCI_POSITIONFROMLINE, startLine), endLineCorrect != endLine ? _pEditView->execute(SCI_POSITIONFROMLINE, endLine) : _pEditView->execute(SCI_GETLINEENDPOSITION, endLine));
		else
			_pEditView->execute(SCI_GOTOPOS, _pEditView->execute(SCI_POSITIONFROMLINE, currentLine) + newCurrentPos);

		for (size_t i = 0; i < folding.size(); ++i)
			_pEditView->fold(folding[i], false);
	}
	else
		restoreSelection();

}

void Notepad_plus::doTrim(trimOp whichPart)
{
	// whichPart : line head or line tail or line both
	FindOption env;
	if (whichPart == lineHeader)
	{
		env._str2Search = L"^[\\t ]+";
	}
	else if (whichPart == lineTail)
	{
		env._str2Search = L"[\\t ]+$";
	}
	else if (whichPart == lineBoth)
	{
		env._str2Search = L"^[\\t ]+|[\\t ]+$";
	}
	else
		return;
	env._str4Replace = L"";
	env._searchType = FindRegex;
	auto mainSelAnchor = _pEditView->execute(SCI_GETANCHOR);
	auto mainSelCaretPos = _pEditView->execute(SCI_GETCURRENTPOS);
	bool isEntireDoc = (mainSelAnchor == mainSelCaretPos);
	auto docLength = _pEditView->execute(SCI_GETLENGTH);

	// block selection is not supported
	if ((_pEditView->execute(SCI_GETSELECTIONMODE) == SC_SEL_RECTANGLE) || (_pEditView->execute(SCI_GETSELECTIONMODE) == SC_SEL_THIN))
		return;

	// auto-expand of partially selected lines
	if (!isEntireDoc)
	{
		env._isInSelection = !isEntireDoc;
		auto startPos = _pEditView->execute(SCI_GETSELECTIONSTART);
		auto startLine = _pEditView->execute(SCI_LINEFROMPOSITION, startPos);
		auto endPos = _pEditView->execute(SCI_GETSELECTIONEND);
		auto endLine = _pEditView->execute(SCI_LINEFROMPOSITION, endPos);

		if (startPos != _pEditView->execute(SCI_POSITIONFROMLINE, startLine))
			startPos = _pEditView->execute(SCI_POSITIONFROMLINE, startLine);

		if (endPos != _pEditView->execute(SCI_POSITIONFROMLINE, endLine) && endPos < _pEditView->execute(SCI_GETLINEENDPOSITION, endLine))
			endPos = _pEditView->execute(SCI_GETLINEENDPOSITION, endLine);

		_pEditView->execute(SCI_SETSEL, startPos, endPos);
	}
	_findReplaceDlg->processAll(ProcessReplaceAll, &env, isEntireDoc);

	// restore original selection if nothing has changed
	if (!isEntireDoc && (docLength == _pEditView->execute(SCI_GETLENGTH)))
	{
		_pEditView->execute(SCI_SETANCHOR, mainSelAnchor);
		_pEditView->execute(SCI_SETCURRENTPOS, mainSelCaretPos);
	}
}

void Notepad_plus::eol2ws()
{
	bool isEntireDoc = (_pEditView->execute(SCI_GETANCHOR) == _pEditView->execute(SCI_GETCURRENTPOS));

	// block selection is not supported
	if ((_pEditView->execute(SCI_GETSELECTIONMODE) == SC_SEL_RECTANGLE) || (_pEditView->execute(SCI_GETSELECTIONMODE) == SC_SEL_THIN))
		return;

	_pEditView->execute(isEntireDoc ? SCI_TARGETWHOLEDOCUMENT: SCI_TARGETFROMSELECTION);
	_pEditView->execute(SCI_LINESJOIN);
}

void Notepad_plus::removeEmptyLine(bool isBlankContained)
{
	// whichPart : line head or line tail
	FindOption env;
	if (isBlankContained)
	{
		env._str2Search = L"^(?>[\\t ]*[\\r\\n]+)+";
	}
	else
	{
		env._str2Search = L"^[\\r\\n]+";
	}
	env._str4Replace = L"";
	env._searchType = FindRegex;
	auto mainSelStart = _pEditView->execute(SCI_GETSELECTIONSTART);
	auto mainSelEnd = _pEditView->execute(SCI_GETSELECTIONEND);
	auto mainSelLength = mainSelEnd - mainSelStart;
	bool isEntireDoc = mainSelLength == 0;
	env._isInSelection = !isEntireDoc;
	_findReplaceDlg->processAll(ProcessReplaceAll, &env, isEntireDoc);

	// remove the last line if it's an empty line.
	auto lastLineDoc = _pEditView->execute(SCI_GETLINECOUNT) - 1;
	auto str2Search = isBlankContained ? L"[\\r\\n]+^[\\t ]*$|^[\\t ]+$" : L"[\\r\\n]+^$";
	auto startPos = _pEditView->execute(SCI_POSITIONFROMLINE, lastLineDoc - 1);
	auto endPos = _pEditView->execute(SCI_GETLENGTH);
	if (!isEntireDoc)
	{
		startPos = _pEditView->execute(SCI_GETSELECTIONSTART);
		endPos = _pEditView->execute(SCI_GETSELECTIONEND);
		auto endLine = _pEditView->execute(SCI_LINEFROMPOSITION, endPos);
		if (endPos != (_pEditView->execute(SCI_POSITIONFROMLINE, endLine) + _pEditView->execute(SCI_LINELENGTH, endLine)))
			return;
	}
	_pEditView->execute(SCI_SETSEARCHFLAGS, SCFIND_REGEXP|SCFIND_POSIX);
	auto posFound = _pEditView->searchInTarget(str2Search, wcslen(str2Search), startPos, endPos);
	if (posFound >= 0)
		_pEditView->replaceTarget(L"", posFound, endPos);
}

void Notepad_plus::removeDuplicateLines()
{
	intptr_t mainSelAnchor = _pEditView->execute(SCI_GETANCHOR);
	intptr_t mainSelCaretPos = _pEditView->execute(SCI_GETCURRENTPOS);
	bool isEntireDoc = (mainSelAnchor == mainSelCaretPos);

	intptr_t startLine = 0;
	intptr_t endLine = _pEditView->lastZeroBasedLineNumber();

	if (!isEntireDoc)
	{
		intptr_t startPos = _pEditView->execute(SCI_GETSELECTIONSTART);
		startLine = _pEditView->execute(SCI_LINEFROMPOSITION, startPos);
		intptr_t endPos = _pEditView->execute(SCI_GETSELECTIONEND);
		endLine = _pEditView->execute(SCI_LINEFROMPOSITION, endPos);
		if (endPos == _pEditView->execute(SCI_POSITIONFROMLINE, endLine))
			endLine -= 1;
	}

	if (startLine == endLine)
		return;

	intptr_t firstMatchLineNr = 0;
	intptr_t lastMatchLineNr = 0;
	wstring firstMatchLineStr;
	wstring lastMatchLineStr;

	for (intptr_t i = startLine; i <= endLine; i++)
	{
		if (firstMatchLineStr.empty())
		{
			firstMatchLineNr = lastMatchLineNr = i;
			firstMatchLineStr = _pEditView->getLine(i);
			continue;
		}
		else
			lastMatchLineStr = _pEditView->getLine(i);

		if (firstMatchLineStr == lastMatchLineStr)
		{
			lastMatchLineNr = i;
			if (i != endLine)
				continue;
		}

		if (firstMatchLineNr != lastMatchLineNr)
		{
			intptr_t startPos = _pEditView->execute(SCI_POSITIONFROMLINE, firstMatchLineNr + 1);
			intptr_t endPos = _pEditView->execute(SCI_POSITIONFROMLINE, lastMatchLineNr) + _pEditView->execute(SCI_LINELENGTH, lastMatchLineNr);
			_pEditView->execute(SCI_DELETERANGE, startPos, endPos - startPos);
			intptr_t removedLines = lastMatchLineNr - firstMatchLineNr;
			i -= removedLines;
			endLine -= removedLines;
		}

		firstMatchLineStr = lastMatchLineStr;
		firstMatchLineNr = lastMatchLineNr = i;

	}

	// correct the last line (without EOL) if it's a duplicate line
	intptr_t endLineStartPos = _pEditView->execute(SCI_POSITIONFROMLINE, endLine);
	intptr_t endLineEndPos = _pEditView->execute(SCI_GETLINEENDPOSITION, endLine);
	intptr_t endLineLength = _pEditView->execute(SCI_LINELENGTH, endLine);

	if (endLine == _pEditView->lastZeroBasedLineNumber() && endLineLength && ((endLineEndPos - endLineStartPos) == endLineLength))
	{
		intptr_t prevLine = endLine - 1;
		intptr_t prevLineStartPos = _pEditView->execute(SCI_POSITIONFROMLINE, prevLine);
		intptr_t prevLineEndPos = _pEditView->execute(SCI_GETLINEENDPOSITION, prevLine);
		intptr_t prevLineLength = _pEditView->execute(SCI_LINELENGTH, prevLine);
		const wstring endLineStr = _pEditView->getLine(endLine);
		const wstring prevLineStr = _pEditView->getGenericTextAsString(prevLineStartPos, prevLineEndPos);
		if (endLineStr == prevLineStr)
			_pEditView->execute(SCI_DELETERANGE, prevLineStartPos, prevLineLength);
	}

}

void Notepad_plus::copyMarkedLines()
{
	intptr_t lastLine = _pEditView->lastZeroBasedLineNumber();
	wstring globalStr = L"";
	for (intptr_t i = lastLine ; i >= 0 ; i--)
	{
		if (bookmarkPresent(i))
		{
			wstring currentStr = getMarkedLine(i) + globalStr;
			globalStr = currentStr;
		}
	}
	QGuiApplication::clipboard()->setText(QString::fromStdWString(globalStr)); // NPLX: was str2Clipboard
}

void Notepad_plus::cutMarkedLines()
{
	std::lock_guard<std::mutex> lock(mark_mutex);

	intptr_t lastLine = _pEditView->lastZeroBasedLineNumber();
	wstring globalStr = L"";

	_pEditView->execute(SCI_BEGINUNDOACTION);
	for (intptr_t i = lastLine ; i >= 0 ; i--)
	{
		if (bookmarkPresent(i))
		{
			wstring currentStr = getMarkedLine(i) + globalStr;
			globalStr = currentStr;

			deleteMarkedline(i);
		}
	}
	_pEditView->execute(SCI_ENDUNDOACTION);
	QGuiApplication::clipboard()->setText(QString::fromStdWString(globalStr)); // NPLX: was str2Clipboard
}

void Notepad_plus::deleteMarkedLines(bool isMarked)
{
	std::lock_guard<std::mutex> lock(mark_mutex);

	intptr_t lastLine = _pEditView->lastZeroBasedLineNumber();

	_pEditView->execute(SCI_BEGINUNDOACTION);
	for (intptr_t i = lastLine ; i >= 0 ; i--)
	{
		if (bookmarkPresent(i) == isMarked)
			deleteMarkedline(i);
	}
	_pEditView->execute(SCI_ENDUNDOACTION);
}

void Notepad_plus::pasteToMarkedLines()
{
	std::lock_guard<std::mutex> lock(mark_mutex);

	// NPLX: QClipboard in place of the CF_UNICODETEXT clipboard API
	const QString clipboardText = QGuiApplication::clipboard()->text();
	if (clipboardText.isEmpty())
		return;

	intptr_t lastLine = _pEditView->lastZeroBasedLineNumber();

	wstring clipboardStr = clipboardText.toStdWString();

	_pEditView->execute(SCI_BEGINUNDOACTION);
	for (intptr_t i = lastLine ; i >= 0 ; i--)
	{
		if (bookmarkPresent(i))
		{
			replaceMarkedline(i, clipboardStr.c_str());
		}
	}
	_pEditView->execute(SCI_ENDUNDOACTION);
}

void Notepad_plus::deleteMarkedline(size_t ln)
{
	intptr_t lineLen = _pEditView->execute(SCI_LINELENGTH, ln);
	intptr_t lineBegin = _pEditView->execute(SCI_POSITIONFROMLINE, ln);

	bookmarkDelete(ln);
	wchar_t emptyString[2] = L"";
	_pEditView->replaceTarget(emptyString, lineBegin, lineBegin + lineLen);
}

void Notepad_plus::inverseMarks()
{
	intptr_t lastLine = _pEditView->lastZeroBasedLineNumber();
	for (int i = 0 ; i <= lastLine  ; ++i)
	{
		if (bookmarkPresent(i))
		{
			bookmarkDelete(i);
		}
		else
		{
			bookmarkAdd(i);
		}
	}
}

void Notepad_plus::replaceMarkedline(size_t ln, const wchar_t *str)
{
	intptr_t lineBegin = _pEditView->execute(SCI_POSITIONFROMLINE, ln);
	intptr_t lineEnd = _pEditView->execute(SCI_GETLINEENDPOSITION, ln);

	_pEditView->replaceTarget(str, lineBegin, lineEnd);
}

wstring Notepad_plus::getMarkedLine(size_t ln)
{
	auto lineLen = _pEditView->execute(SCI_LINELENGTH, ln);
	auto lineBegin = _pEditView->execute(SCI_POSITIONFROMLINE, ln);

	wchar_t * buf = new wchar_t[lineLen+1];
	_pEditView->getGenericText(buf, lineLen + 1, lineBegin, lineBegin + lineLen);
	wstring line = buf;
	delete [] buf;

	return line;
}

void Notepad_plus::clearChangesHistory(int iView)
{
	// use current view by default
	ScintillaEditView* pViewToChange = _pEditView;
	ScintillaEditView* pAnotherView = _pNonEditView;

	if (iView == MAIN_VIEW)
	{
		pViewToChange = _mainEditView;
		pAnotherView = _subEditView;
	}
	else if (iView == SUB_VIEW)
	{
		pViewToChange = _subEditView;
		pAnotherView = _mainEditView;
	}

	Sci_Position pos = static_cast<Sci_Position>(pViewToChange->execute(SCI_GETCURRENTPOS));
	int chFlags = static_cast<int>(pViewToChange->execute(SCI_GETCHANGEHISTORY));

	pViewToChange->execute(SCI_EMPTYUNDOBUFFER);
	pViewToChange->execute(SCI_SETCHANGEHISTORY, SC_CHANGE_HISTORY_DISABLED);
	pViewToChange->execute(SCI_SETCHANGEHISTORY, chFlags);
	pViewToChange->execute(SCI_GOTOPOS, pos);

	checkUndoState();
	pAnotherView->redraw(); // Prevent cloned document visual glitch on another view
}

void Notepad_plus::changedHistoryGoTo(int idGoTo)
{
	int mask =	(1 << SC_MARKNUM_HISTORY_REVERTED_TO_ORIGIN) |
				(1 << SC_MARKNUM_HISTORY_SAVED) |
				(1 << SC_MARKNUM_HISTORY_MODIFIED) |
				(1 << SC_MARKNUM_HISTORY_REVERTED_TO_MODIFIED);

	intptr_t line = -1;
	intptr_t blockIndicator = _pEditView->getCurrentLineNumber();
	intptr_t lastLine = _pEditView->execute(SCI_GETLINECOUNT);

	if (idGoTo == IDM_SEARCH_CHANGED_NEXT)		// Next.
	{
		intptr_t currentLine = blockIndicator;

		// Start from currentLine (not currentLine + 1) in case currentLine is not-changed and the next line IS changed. lastLine is at least *1*.
		for (intptr_t i = currentLine; i < lastLine; i++)
		{
			if (_pEditView->execute(SCI_MARKERGET, i) & mask)
			{
				if (i != blockIndicator)		// Changed-line found in a different block.
				{
					line = i;
					break;
				}
				else
				{
					blockIndicator++;
				}
			}
		}

		if (line == -1)		// Wrap around.
		{
			intptr_t endRange = currentLine + 1;		//	"+ 1": currentLine might be *0*.
			for (intptr_t i = 0; i < endRange; i++)
			{
				if (_pEditView->execute(SCI_MARKERGET, i) & mask)
				{
					line = i;
					break;
				}
			}
		}
	}
	else	// Prev.
	{
		while (true)
		{
			line = _pEditView->execute(SCI_MARKERPREVIOUS, blockIndicator, mask);
			// "line == -1": no changed-line found. "line != blockIndicator": changed-line found in a different block.
			if (line == -1 || line != blockIndicator)
				break;
			else
				blockIndicator--;
		}

		if (line == -1)	// Wrap around.
		{
			line = _pEditView->execute(SCI_MARKERPREVIOUS, lastLine - 1, mask);
		}
	}

	if (line != -1)
	{
		_pEditView->execute(SCI_ENSUREVISIBLEENFORCEPOLICY, line);
		_pEditView->execute(SCI_GOTOLINE, line);
	}
	else
	{
		bool isSilent = NppParameters::getInstance().getNppGUI()._muteSounds;

		if (!isSilent)
			QApplication::beep(); // NPLX: was ::MessageBeep
	}
}

void Notepad_plus::changeReadOnlyUserModeForAllOpenedTabs(const bool ro)
{
	if (ro != true && NppParameters::getInstance().getNppGUI()._isFullReadOnlySavingForbidden)
		return; // safety for FullReadOnlySavingForbidden mode, refuse to cease the R/O state

	// make R/O changes in both views
	std::vector<DocTabView*> tabViews = { _mainDocTab, _subDocTab };
	for (auto& pTabView : tabViews)
	{
		for (size_t i = 0; i < pTabView->nbItem(); ++i)
		{
			BufferID id = pTabView->getBufferByIndex(i);
			if (id != BUFFER_INVALID)
			{
				Buffer* buf = MainFileManager.getBufferByID(id);
				if (buf != nullptr)
					buf->setUserReadOnly(ro);
			}
		}
	}
}

// ============================================================================
// Comments (upstream Notepad_plus.cpp)
// ============================================================================

static std::string extractSymbol(char firstChar, char secondChar, const char* str2extract)
{
	bool found = false;
	static constexpr size_t extractedLen = 128;
	char extracted[extractedLen] = { '\0' };

	for (size_t i = 0, j = 0, len = std::strlen(str2extract); i < len && j < extractedLen - 1; ++i)
	{
		if (found)
		{
			if (!str2extract[i] || str2extract[i] == ' ')
			{
				extracted[j] = '\0';
				return extracted;
			}
			extracted[j++] = str2extract[i];
		}
		else
		{
			if (!str2extract[i])
				return "";

			if (str2extract[i] == firstChar && str2extract[i + 1] == secondChar)
			{
				found = true;
				++i;
			}
		}
	}
	return extracted;
}

bool Notepad_plus::doBlockComment(comment_mode currCommentMode)
{
	Buffer * buf = _pEditView->getCurrentBuffer();
	// Avoid side-effects (e.g. cursor moves number of comment-characters) when file is read-only.
	if (buf->isReadOnly())
		return false;

	//-- BlockToStreamComment:
	const char* commentStart = nullptr;
	const char* commentEnd = nullptr;
	std::string symbolStart;
	std::string symbolEnd;

	const char* commentLineSymbol = nullptr;
	std::string symbol;

	//Single Line Comment/Uncomment/Toggle can have two modes:
	// * a NORMAL MODE which uses a commentLineSymbol to comment/uncomment code per line, and
	// * an ADVANCED MODE which uses commentStart and commentEnd symbols to comment/uncomment code per line.
	//The NORMAL MODE is used for all Lexers which have a commentLineSymbol defined.
	//The ADVANCED MODE is only available for Lexers which do not have a commentLineSymbol but commentStreamSymbols (start/end) defined.
	//The ADVANCED MODE behaves the same way as the NORMAL MODE (comment/uncomment every single line in the selection range separately)
	//but uses two symbols to accomplish this.
	bool isSingleLineAdvancedMode = false;

	if (buf->getLangType() == L_USER)
	{
		const UserLangContainer* userLangContainer = NppParameters::getInstance().getULCFromName(buf->getUserDefineLangName());
		if (!userLangContainer)
			return false;

		const std::string& kws = userLangContainer->_keywordLists[SCE_USER_KWLIST_COMMENTS];

		symbol = extractSymbol('0', '0', kws.c_str());
		commentLineSymbol = symbol.c_str();
		// BlockToStreamComment: Needed to decide, if stream-comment can be called below!
		symbolStart = extractSymbol('0', '3', kws.c_str());
		commentStart = symbolStart.c_str();
		symbolEnd = extractSymbol('0', '4', kws.c_str());
		commentEnd = symbolEnd.c_str();
	}
	else
	{
		commentLineSymbol = buf->getCommentLineSymbol();
		// BlockToStreamComment: Needed to decide, if stream-comment can be called below!
		commentStart = buf->getCommentStart();
		commentEnd = buf->getCommentEnd();
	}

	if ((!commentLineSymbol) || (!commentLineSymbol[0]) || (commentLineSymbol == NULL))
	{
	// BlockToStreamComment: If there is no block-comment symbol, try the stream comment:
		if (!(!commentStart || !commentStart[0] || commentStart == NULL || !commentEnd || !commentEnd[0] || commentEnd == NULL))
		{
			if (currCommentMode == cm_comment)
			{
				//Do an advanced "Single Line Comment" by using stream-comment symbols (start/end) per line in this case.
				isSingleLineAdvancedMode = true;
				//return doStreamComment(); //Use "Block Comment" for this.
			}
			else if (currCommentMode == cm_uncomment)
			{
				//"undoStreamComment()" can be more flexible than "isSingleLineAdvancedMode = true", 
				//since it can uncomment more embedded levels at once and the commentEnd symbol can be located everywhere. 
				//But, depending on the selection start/end position, the first/last selected line may not be uncommented properly!
				return undoStreamComment(false);
				//isSingleLineAdvancedMode = true;
			}
			else if (currCommentMode == cm_toggle)
			{
				//Do an advanced "Toggle Single Line Comment" by using stream-comment symbols (start/end) per line in this case.
				isSingleLineAdvancedMode = true;
			}
			else
				return false;
		}
		else
			return false;
	}

	//For Single Line NORMAL MODE
	std::string comment;
	size_t comment_length = 0;

	//For Single Line ADVANCED MODE
	std::string advCommentStart;
	std::string advCommentEnd;
	size_t advCommentStart_length = 0;
	size_t advCommentEnd_length = 0;

	const char aSpace[]{ " " };

	//Only values that have passed through will be assigned, to be sure they are valid!
	if (!isSingleLineAdvancedMode)
	{
		comment = commentLineSymbol;

		if (!(buf->getLangType() == L_BAANC)) // BaanC standardization - no space.
			comment += aSpace;

		comment_length = comment.length();
	}
	else // isSingleLineAdvancedMode
	{
		advCommentStart = commentStart;
		advCommentStart += aSpace;
		advCommentEnd = aSpace;
		advCommentEnd += commentEnd;

		advCommentStart_length = advCommentStart.length();
		advCommentEnd_length = advCommentEnd.length();
	}

    size_t selectionStart = _pEditView->execute(SCI_GETSELECTIONSTART);
    size_t selectionEnd = _pEditView->execute(SCI_GETSELECTIONEND);
    size_t caretPosition = _pEditView->execute(SCI_GETCURRENTPOS);
    // checking if caret is located in _beginning_ of selected block
    bool move_caret = caretPosition < selectionEnd;
	intptr_t selStartLine = _pEditView->execute(SCI_LINEFROMPOSITION, selectionStart);
	intptr_t selEndLine = _pEditView->execute(SCI_LINEFROMPOSITION, selectionEnd);
	intptr_t lines = selEndLine - selStartLine;
    // "caret return" is part of the last selected line
    if ((lines > 0) && (selectionEnd == static_cast<size_t>(_pEditView->execute(SCI_POSITIONFROMLINE, selEndLine))))
		selEndLine--;
	// count lines which were un-commented to decide if undoStreamComment() shall be called.
	int nUncomments = 0;
	//Some Lexers need line-comments at the beginning of a line.
	const bool avoidIndent = (buf->getLangType() == L_FORTRAN_77 || buf->getLangType() == L_BAANC);
	//Some Lexers comment blank lines, per their standards.
	const bool commentEmptyLines = (buf->getLangType() == L_BAANC);

    _pEditView->execute(SCI_BEGINUNDOACTION);

    for (intptr_t i = selStartLine; i <= selEndLine; ++i)
	{
		size_t lineStart = _pEditView->execute(SCI_POSITIONFROMLINE, i);
		size_t lineIndent = _pEditView->execute(SCI_GETLINEINDENTPOSITION, i);
		size_t lineEnd = _pEditView->execute(SCI_GETLINEENDPOSITION, i);

		// empty lines are not commented, unless required by the language.
		if (lineIndent == lineEnd && !commentEmptyLines)
			continue;

		if (avoidIndent)
			lineIndent = lineStart;

		size_t linebufferSize = lineEnd - lineIndent + 1;
		auto linebuf = std::make_unique<char[]>(linebufferSize);

		_pEditView->getGenericText(linebuf.get(), linebufferSize, lineIndent, lineEnd);

		std::string linebufStr = linebuf.get();

   		if (currCommentMode != cm_comment) // uncomment/toggle
		{
			if (!isSingleLineAdvancedMode)
			{
				// In order to do get case insensitive comparison use strnicmp() instead case-sensitive comparison.
				//      Case insensitive comparison is needed e.g. for "REM" and "rem" in Batchfiles.
				if (::_strnicmp(linebufStr.c_str(), comment.c_str(), (buf->getLangType() != L_BAANC) ? comment_length - 1 : comment_length) == 0)
				{
					size_t len = linebufStr[comment_length - 1] == aSpace[0] ? comment_length : !(buf->getLangType() == L_BAANC) ? comment_length - 1 : comment_length;

					_pEditView->execute(SCI_SETSEL, lineIndent, lineIndent + len);
					_pEditView->replaceSelWith("");

					// SELECTION RANGE ADJUSTMENT .......................
					if (i == selStartLine) // first selected line
					{
						if (selectionStart > lineIndent + len)
							selectionStart -= len;
						else if (selectionStart > lineIndent)
							selectionStart = lineIndent;
					} // ................................................
					if (i == selEndLine) // last selected line
					{
						if (selectionEnd > lineIndent + len)
							selectionEnd -= len;
						else if (selectionEnd > lineIndent)
						{
							selectionEnd = lineIndent;
							if (lineIndent == lineStart && i != selStartLine)
								++selectionEnd; // avoid caret return in this case
						}
					} // ................................................
					else // every iteration except the last selected line
						selectionEnd -= len;
					// ..................................................

					++nUncomments;
					continue;
				}
			}
			else // isSingleLineAdvancedMode
			{
				if ((::_strnicmp(linebufStr.c_str(), advCommentStart.c_str(), advCommentStart_length - 1) == 0)
					&& (::_strnicmp(linebufStr.substr(linebufStr.length() - advCommentEnd_length + 1, advCommentEnd_length - 1).c_str(), advCommentEnd.substr(1, advCommentEnd_length - 1).c_str(), advCommentEnd_length - 1) == 0))
				{
					size_t startLen = linebufStr[advCommentStart_length - 1] == aSpace[0] ? advCommentStart_length : advCommentStart_length - 1;
					size_t endLen = linebufStr[linebufStr.length() - advCommentEnd_length] == aSpace[0] ? advCommentEnd_length : advCommentEnd_length - 1;

					_pEditView->execute(SCI_SETSEL, lineIndent, lineIndent + startLen);
					_pEditView->replaceSelWith("");
					_pEditView->execute(SCI_SETSEL, lineEnd - startLen - endLen, lineEnd - startLen);
					_pEditView->replaceSelWith("");

					// SELECTION RANGE ADJUSTMENT .......................
					if (i == selStartLine) // first selected line
					{
						if (selectionStart > lineEnd - endLen)
							selectionStart = lineEnd - startLen - endLen;
						else if (selectionStart > lineIndent + startLen)
							selectionStart -= startLen;
						else if (selectionStart > lineIndent)
							selectionStart = lineIndent;
					} // ................................................
					if (i == selEndLine) // last selected line
					{
						if (selectionEnd > lineEnd)
							selectionEnd -= (startLen + endLen);
						else if (selectionEnd > lineEnd - endLen)
							selectionEnd = lineEnd - startLen - endLen;
						else if (selectionEnd > lineIndent + startLen)
							selectionEnd -= startLen;
						else if (selectionEnd > lineIndent)
						{
							selectionEnd = lineIndent;
							if (lineIndent == lineStart && i != selStartLine)
								++selectionEnd; // avoid caret return in this case
						}
					} // ................................................
					else // every iteration except the last selected line
						selectionEnd -= (startLen + endLen);
					// ..................................................

					++nUncomments;
					continue;
				}
			}
		} // uncomment/toggle

		if (currCommentMode != cm_uncomment) // comment/toggle
		{
			if (!isSingleLineAdvancedMode)
			{
				_pEditView->insertGenericTextFrom(lineIndent, comment.c_str());

				// SELECTION RANGE ADJUSTMENT .......................
				if (i == selStartLine) // first selected line
				{
					if (selectionStart >= lineIndent)
						selectionStart += comment_length;
				} // ................................................
				if (i == selEndLine) // last selected line
				{
					if (selectionEnd >= lineIndent)
						selectionEnd += comment_length;
				} // ................................................
				else // every iteration except the last selected line
					selectionEnd += comment_length;
				// ..................................................
			}
			else // isSingleLineAdvancedMode
			{
				_pEditView->insertGenericTextFrom(lineIndent, advCommentStart.c_str());
				_pEditView->insertGenericTextFrom(lineEnd + advCommentStart_length, advCommentEnd.c_str());

				// SELECTION RANGE ADJUSTMENT .......................
				if (i == selStartLine) // first selected line
				{
					if (selectionStart >= lineIndent)
						selectionStart += advCommentStart_length;
				} // ................................................
				if (i == selEndLine) // last selected line
				{
					if (selectionEnd > lineEnd)
						selectionEnd += (advCommentStart_length + advCommentEnd_length);
					else if (selectionEnd >= lineIndent)
						selectionEnd += advCommentStart_length;
				} // ................................................
				else // every iteration except the last selected line
					selectionEnd += (advCommentStart_length + advCommentEnd_length);
				// ..................................................
			}
		} // comment/toggle
	} // for (...)

    if (move_caret)
	{
        // moving caret to the beginning of selected block
        _pEditView->execute(SCI_GOTOPOS, selectionEnd);
        _pEditView->execute(SCI_SETCURRENTPOS, selectionStart);
    }
	else
	{
        _pEditView->execute(SCI_SETSEL, selectionStart, selectionEnd);
    }
    _pEditView->execute(SCI_ENDUNDOACTION);

	// undoStreamComment: If there were no block-comments to un-comment try uncommenting of stream-comment.
	if ((currCommentMode == cm_uncomment) && (nUncomments == 0))
	{
		return undoStreamComment(false);
	}
    return true;
}

bool Notepad_plus::doStreamComment()
{
	const char* commentStart = nullptr;
	const char* commentEnd = nullptr;

	std::string symbolStart;
	std::string symbolEnd;

	// BlockToStreamComment:
	const char* commentLineSymbol = nullptr;
	std::string symbol;

	Buffer * buf = _pEditView->getCurrentBuffer();
	// Avoid side-effects (e.g. cursor moves number of comment-characters) when file is read-only.
	if (buf->isReadOnly())
		return false;

	if (buf->getLangType() == L_USER)
	{
		const UserLangContainer* userLangContainer = NppParameters::getInstance().getULCFromName(buf->getUserDefineLangName());

		if (!userLangContainer)
			return false;

		const std::string& kws = userLangContainer->_keywordLists[SCE_USER_KWLIST_COMMENTS];

		// BlockToStreamComment: Next two lines needed to decide, if block-comment can be called below!
		symbol = extractSymbol('0', '0', kws.c_str());
		commentLineSymbol = symbol.c_str();

		symbolStart = extractSymbol('0', '3', kws.c_str());
		commentStart = symbolStart.c_str();
		symbolEnd = extractSymbol('0', '4', kws.c_str());
		commentEnd = symbolEnd.c_str();
	}
	else
	{
		// BlockToStreamComment: Next line needed to decide, if block-comment can be called below!
		commentLineSymbol = buf->getCommentLineSymbol();
		commentStart = buf->getCommentStart();
		commentEnd = buf->getCommentEnd();
	}

	// BlockToStreamComment: If there is no stream-comment symbol, try the block comment:
	if ((!commentStart) || (!commentStart[0]) || (commentStart == NULL) || (!commentEnd) || (!commentEnd[0]) || (commentEnd == NULL))
	{
		if (!(!commentLineSymbol || !commentLineSymbol[0] || commentLineSymbol == NULL))
			return doBlockComment(cm_comment);
		else
		return false;
	}

	std::string start_comment(commentStart);
	std::string end_comment(commentEnd);
	std::string white_space(" ");

	start_comment += white_space;
	white_space += end_comment;
	end_comment = white_space;
	size_t start_comment_length = start_comment.length();
	size_t selectionStart = _pEditView->execute(SCI_GETSELECTIONSTART);
	size_t selectionEnd = _pEditView->execute(SCI_GETSELECTIONEND);
	size_t caretPosition = _pEditView->execute(SCI_GETCURRENTPOS);
	// checking if caret is located in _beginning_ of selected block
	bool move_caret = caretPosition < selectionEnd;

	// if there is no selection?
	if (selectionEnd <= selectionStart)
	{
		auto selLine = _pEditView->execute(SCI_LINEFROMPOSITION, selectionStart);
		selectionStart = _pEditView->execute(SCI_GETLINEINDENTPOSITION, selLine);
		selectionEnd = _pEditView->execute(SCI_GETLINEENDPOSITION, selLine);
	}

	_pEditView->execute(SCI_BEGINUNDOACTION);
	_pEditView->insertGenericTextFrom(selectionStart, start_comment.c_str());
	selectionEnd += start_comment_length;
	selectionStart += start_comment_length;
	_pEditView->insertGenericTextFrom(selectionEnd, end_comment.c_str());

	if (move_caret)
	{
		// moving caret to the beginning of selected block
		_pEditView->execute(SCI_GOTOPOS, selectionEnd);
		_pEditView->execute(SCI_SETCURRENTPOS, selectionStart);
	}
	else
	{
		_pEditView->execute(SCI_SETSEL, selectionStart, selectionEnd);
	}
	_pEditView->execute(SCI_ENDUNDOACTION);
	return true;
}

bool Notepad_plus::undoStreamComment(bool tryBlockComment)
{
	const char* commentStart = nullptr;
	const char* commentEnd = nullptr;
	const char* commentLineSymbol = nullptr;

	std::string symbolStart;
	std::string symbolEnd;
	std::string symbol;

	static constexpr int charbufLen = 10;
	char charbuf[charbufLen]{};

	bool retVal = false;

	Buffer* buf = _pEditView->getCurrentBuffer();
	//-- Avoid side-effects (e.g. cursor moves number of comment-characters) when file is read-only.
	if (buf->isReadOnly())
		return false;
	if (buf->getLangType() == L_USER)
	{
		const UserLangContainer* userLangContainer = NppParameters::getInstance().getULCFromName(buf->getUserDefineLangName());
		if (!userLangContainer)
			return false;

		const std::string& kws = userLangContainer->_keywordLists[SCE_USER_KWLIST_COMMENTS];

		symbol = extractSymbol('0', '0', kws.c_str());
		commentLineSymbol = symbol.c_str();
		symbolStart = extractSymbol('0', '3', kws.c_str());
		commentStart = symbolStart.c_str();
		symbolEnd = extractSymbol('0', '4', kws.c_str());
		commentEnd = symbolEnd.c_str();
	}
	else
	{
		commentLineSymbol = buf->getCommentLineSymbol();
		commentStart = buf->getCommentStart();
		commentEnd = buf->getCommentEnd();
	}


	// BlockToStreamComment: If there is no stream-comment symbol and we came not from doBlockComment, try the block comment:
	if ((!commentStart) || (!commentStart[0]) || (commentStart == NULL) || (!commentEnd) || (!commentEnd[0]) || (commentEnd == NULL))
	{
		if (!(!commentLineSymbol || !commentLineSymbol[0] || commentLineSymbol == NULL) && tryBlockComment)
			return doBlockComment(cm_uncomment);
		else
			return false;
	}

	std::string start_comment(commentStart);
	std::string end_comment(commentEnd);
	std::string white_space(" ");
	size_t start_comment_length = start_comment.length();
	size_t end_comment_length = end_comment.length();

	_pEditView->execute(SCI_BEGINUNDOACTION);

	// do as long as stream-comments are within selection
	do
	{
		auto selectionStart = _pEditView->execute(SCI_GETSELECTIONSTART);
		auto selectionEnd = _pEditView->execute(SCI_GETSELECTIONEND);
		auto caretPosition = _pEditView->execute(SCI_GETCURRENTPOS);
		auto docLength = _pEditView->execute(SCI_GETLENGTH);

		// checking if caret is located in _beginning_ of selected block
		bool move_caret = caretPosition < selectionEnd;

		//-- Note: The caretPosition is either at selectionEnd or at selectionStart!! selectionStart is always before (smaller) than selectionEnd!!

		//-- First, search all start_comment and end_comment before and after the selectionStart and selectionEnd position.
		const int iSelStart = 0, iSelEnd = 1;
		const size_t N_CMNT = 2;
		intptr_t posStartCommentBefore[N_CMNT], posEndCommentBefore[N_CMNT], posStartCommentAfter[N_CMNT], posEndCommentAfter[N_CMNT];
		bool blnStartCommentBefore[N_CMNT], blnEndCommentBefore[N_CMNT], blnStartCommentAfter[N_CMNT], blnEndCommentAfter[N_CMNT];
		intptr_t posStartComment, posEndComment;
		intptr_t selectionStartMove, selectionEndMove;
		int flags;

		//-- Directly use Scintilla-Functions
		//   rather than _findReplaceDlg->processFindNext()which does not return the find-position and is not quiet!
		flags = SCFIND_WORDSTART;
		_pEditView->execute(SCI_SETSEARCHFLAGS, flags);
		//-- Find all start- and end-comments before and after the selectionStart position.
		//-- When searching upwards the start-position for searching must be moved one after the current position
		//   to find a search-string just starting before the current position!
		//-- Direction DIR_UP ---
		posStartCommentBefore[iSelStart] = _pEditView->searchInTarget(start_comment, selectionStart, 0);
		(posStartCommentBefore[iSelStart] == -1 ? blnStartCommentBefore[iSelStart] = false : blnStartCommentBefore[iSelStart] = true);
		posEndCommentBefore[iSelStart] = _pEditView->searchInTarget(end_comment, selectionStart, 0);
		(posEndCommentBefore[iSelStart] == -1 ? blnEndCommentBefore[iSelStart] = false : blnEndCommentBefore[iSelStart] = true);
		//-- Direction DIR_DOWN ---
		posStartCommentAfter[iSelStart] = _pEditView->searchInTarget(start_comment, selectionStart, docLength);
		(posStartCommentAfter[iSelStart] == -1 ? blnStartCommentAfter[iSelStart] = false : blnStartCommentAfter[iSelStart] = true);
		posEndCommentAfter[iSelStart] = _pEditView->searchInTarget(end_comment, selectionStart, docLength);
		(posEndCommentAfter[iSelStart] == -1 ? blnEndCommentAfter[iSelStart] = false : blnEndCommentAfter[iSelStart] = true);

		//-- Check, if selectionStart or selectionEnd is within a stream comment -----
		//   or if the selection includes a complete stream-comment!! ----------------

		//-- First, check if there is a stream-comment around the selectionStart position:
		if ((blnStartCommentBefore[iSelStart] && blnEndCommentAfter[iSelStart])
			&& (!blnEndCommentBefore[iSelStart] || (posStartCommentBefore[iSelStart] >= posEndCommentBefore[iSelStart]))
			&& (!blnStartCommentAfter[iSelStart] || (posEndCommentAfter[iSelStart] <= posStartCommentAfter[iSelStart])))
		{
			posStartComment = posStartCommentBefore[iSelStart];
			posEndComment = posEndCommentAfter[iSelStart];
		}
		else //-- Second, check if there is a stream-comment around the selectionEnd position:
		{
			//-- Find all start- and end-comments before and after the selectionEnd position.
			//-- Direction DIR_UP ---
			posStartCommentBefore[iSelEnd] = _pEditView->searchInTarget(start_comment, selectionEnd, 0);
			(posStartCommentBefore[iSelEnd] == -1 ? blnStartCommentBefore[iSelEnd] = false : blnStartCommentBefore[iSelEnd] = true);
			posEndCommentBefore[iSelEnd] = _pEditView->searchInTarget(end_comment, selectionEnd, 0);
			(posEndCommentBefore[iSelEnd] == -1 ? blnEndCommentBefore[iSelEnd] = false : blnEndCommentBefore[iSelEnd] = true);
			//-- Direction DIR_DOWN ---
			posStartCommentAfter[iSelEnd] = _pEditView->searchInTarget(start_comment, selectionEnd, docLength);
			(posStartCommentAfter[iSelEnd] == -1 ? blnStartCommentAfter[iSelEnd] = false : blnStartCommentAfter[iSelEnd] = true);
			posEndCommentAfter[iSelEnd] = _pEditView->searchInTarget(end_comment, selectionEnd, docLength);
			(posEndCommentAfter[iSelEnd] == -1 ? blnEndCommentAfter[iSelEnd] = false : blnEndCommentAfter[iSelEnd] = true);

			if ((blnStartCommentBefore[iSelEnd] && blnEndCommentAfter[iSelEnd])
				&& (!blnEndCommentBefore[iSelEnd] || (posStartCommentBefore[iSelEnd] >= posEndCommentBefore[iSelEnd]))
				&& (!blnStartCommentAfter[iSelEnd] || (posEndCommentAfter[iSelEnd] <= posStartCommentAfter[iSelEnd])))
			{
				posStartComment = posStartCommentBefore[iSelEnd];
				posEndComment = posEndCommentAfter[iSelEnd];
			}
			//-- Third, check if there is a stream-comment within the selected area:
			else if ((blnStartCommentAfter[iSelStart] && (posStartCommentAfter[iSelStart] < selectionEnd))
				&& (blnEndCommentBefore[iSelEnd] && (posEndCommentBefore[iSelEnd] > selectionStart)))
			{
				//-- If there are more than one stream-comment within the selection, take the first one after selectionStart!!
				posStartComment = posStartCommentAfter[iSelStart];
				posEndComment = posEndCommentAfter[iSelStart];
			}
			//-- Finally, if there is no stream-comment, return
			else
			{
				_pEditView->execute(SCI_ENDUNDOACTION);
				return retVal;
			}
		}

		//-- Ok, there are valid start-comment and valid end-comment around the caret-position.
		//   Now, un-comment stream-comment:
		retVal = true;
		intptr_t startCommentLength = static_cast<intptr_t>(start_comment_length);
		intptr_t endCommentLength = static_cast<intptr_t>(end_comment_length);

		//-- First delete end-comment, so that posStartCommentBefore does not change!
		//-- Get character before end-comment to decide, if there is a white character before the end-comment, which will be removed too!
		_pEditView->getGenericText(charbuf, charbufLen, posEndComment - 1, posEndComment);
		if (::_strnicmp(charbuf, white_space.c_str(), white_space.length()) == 0)
		{
			endCommentLength += 1;
			posEndComment -= 1;
		}
		//-- Delete end stream-comment string ---------
		_pEditView->execute(SCI_BEGINUNDOACTION);
		_pEditView->execute(SCI_SETSEL, posEndComment, posEndComment + endCommentLength);
		_pEditView->execute(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(""));

		//-- Get character after start-comment to decide, if there is a white character after the start-comment, which will be removed too!
		_pEditView->getGenericText(charbuf, charbufLen, posStartComment + startCommentLength, posStartComment + startCommentLength + 1);
		if (::_strnicmp(charbuf, white_space.c_str(), white_space.length()) == 0)
			startCommentLength += 1;

		//-- Delete starting stream-comment string ---------
		_pEditView->execute(SCI_SETSEL, posStartComment, posStartComment + startCommentLength);
		_pEditView->execute(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(""));
		_pEditView->execute(SCI_ENDUNDOACTION);

		//-- Reset selection before calling the routine
		//-- Determine selection movement
		//   selectionStart
		if (selectionStart > posStartComment)
		{
			if (selectionStart >= posStartComment + startCommentLength)
				selectionStartMove = -static_cast<intptr_t>(startCommentLength);
			else
				selectionStartMove = -static_cast<intptr_t>(selectionStart - posStartComment);
		}
		else
			selectionStartMove = 0;

		//   selectionEnd
		if (selectionEnd >= posEndComment + endCommentLength)
			selectionEndMove = -static_cast<intptr_t>(startCommentLength + endCommentLength);
		else if (selectionEnd <= posEndComment)
			selectionEndMove = -static_cast<intptr_t>(startCommentLength);
		else
			selectionEndMove = -static_cast<intptr_t>(startCommentLength + (selectionEnd - posEndComment));

		//-- Reset selection of text without deleted stream-comment-string
		if (move_caret)
		{
			// moving caret to the beginning of selected block
			_pEditView->execute(SCI_GOTOPOS, selectionEnd + selectionEndMove);
			_pEditView->execute(SCI_SETCURRENTPOS, selectionStart + selectionStartMove);
		}
		else
		{
			_pEditView->execute(SCI_SETSEL, selectionStart + selectionStartMove, selectionEnd + selectionEndMove);
		}
	} while (1); //do as long as stream-comments are within selection
}

bool Notepad_plus::isConditionExprLine(intptr_t lineNumber)
{
	if (lineNumber < 0 || lineNumber > _pEditView->execute(SCI_GETLINECOUNT))
		return false;

	auto startPos = _pEditView->execute(SCI_POSITIONFROMLINE, lineNumber);
	auto endPos = _pEditView->execute(SCI_GETLINEENDPOSITION, lineNumber);
	_pEditView->execute(SCI_SETSEARCHFLAGS, SCFIND_REGEXP | SCFIND_POSIX);
	_pEditView->execute(SCI_SETTARGETRANGE, startPos, endPos);

	const char ifElseForWhileExpr[] = "((else[ \t]+)?if|for|while)[ \t]*[(].*[)][ \t]*|else[ \t]*";

	auto posFound = _pEditView->execute(SCI_SEARCHINTARGET, strlen(ifElseForWhileExpr), reinterpret_cast<LPARAM>(ifElseForWhileExpr));
	if (posFound >= 0)
	{
		auto end = _pEditView->execute(SCI_GETTARGETEND);
		if (end == endPos)
			return true;
	}

	return false;
}

// ============================================================================
// Counts for the status bar and the Summary (upstream Notepad_plus.cpp)
// ============================================================================

static const char utflen[] = {1,1,2,3};

static inline size_t countUtf8Characters(const unsigned char *buf, size_t pos, size_t endpos)
{
	size_t result = 0;
	while (pos < endpos)
	{
		unsigned char c = buf[pos++];
		if ((c&0xc0) == 0x80 // do not count unexpected continuation bytes (this handles the case where an UTF-8 character is split in the middle)
			|| c == '\n' || c == '\r') continue; // do not count end of lines
		if (c >= 0xc0) 
			pos += utflen[(c & 0x30) >>  4];
		++result;
	}
	return result;
}

size_t Notepad_plus::getSelectedCharNumber(UniMode u)
{
	size_t result = 0;
	size_t numSel = _pEditView->execute(SCI_GETSELECTIONS);
	if (u == uniUTF8 || u == uniUTF8_NoBOM)
	{
		for (size_t i = 0; i < numSel; ++i)
		{
			size_t line1 = _pEditView->execute(SCI_LINEFROMPOSITION, _pEditView->execute(SCI_GETSELECTIONNSTART, i));
			size_t line2 = _pEditView->execute(SCI_LINEFROMPOSITION, _pEditView->execute(SCI_GETSELECTIONNEND, i));
			for (size_t j = line1; j <= line2; ++j)
			{
				size_t stpos = _pEditView->execute(SCI_GETLINESELSTARTPOSITION, j);
				if (static_cast<intptr_t>(stpos) != INVALID_POSITION)
				{
					size_t endpos = _pEditView->execute(SCI_GETLINESELENDPOSITION, j);
					for (size_t pos = stpos; pos < endpos; ++pos)
					{
						unsigned char c = 0xf0 & static_cast<unsigned char>(_pEditView->execute(SCI_GETCHARAT, pos));
						if (c >= 0xc0)
							pos += utflen[(c & 0x30) >>  4];
						++result;
					}
				}
			}
		}
	}
	else
	{
		for (size_t i = 0; i < numSel; ++i)
		{
			size_t stpos = _pEditView->execute(SCI_GETSELECTIONNSTART, i);
			size_t endpos = _pEditView->execute(SCI_GETSELECTIONNEND, i);
			result += (endpos - stpos);
			size_t line1 = _pEditView->execute(SCI_LINEFROMPOSITION, stpos);
			size_t line2 = _pEditView->execute(SCI_LINEFROMPOSITION, endpos);
			line2 -= line1;
			if (_pEditView->execute(SCI_GETEOLMODE) == SC_EOL_CRLF) line2 *= 2;
			result -= line2;
		}
		if (u != uni8Bit && u != uni7Bit) result *= 2;
	}
	return result;
}

size_t Notepad_plus::getCurrentDocCharCount(UniMode u)
{
	if (u != uniUTF8 && u != uniUTF8_NoBOM)
	{
		size_t numLines = _pEditView->execute(SCI_GETLINECOUNT);
		auto result = _pEditView->execute(SCI_GETLENGTH);
		size_t lines = numLines==0?0:numLines-1;
		if (_pEditView->execute(SCI_GETEOLMODE) == SC_EOL_CRLF) lines *= 2;
		result -= lines;
		return (result < 0) ? 0 : result;
	}
 	else
 	{
		// Note that counting is not well defined for invalid UTF-8 characters.
		// This method is O(filelength) regardless of the number of characters we count (due to SCI_GETCHARACTERPOINTER);
		// it would not be appropriate for counting characters in a small selection.
		size_t result = 0;

		size_t endpos = _pEditView->execute(SCI_GETLENGTH);
		const unsigned char* buf = (unsigned char*)_pEditView->execute(SCI_GETCHARACTERPOINTER); // Scintilla doc said the pointer can be invalidated by any other "execute"

#ifdef _OPENMP // parallel counting of characters with OpenMP
		if (endpos > 50000) // starting threads takes time; for small files it is better to simply count in one thread
		{
			#pragma omp parallel reduction(+: result)
			{
				// split in chunks of same size (except last chunk if it's not evenly divisible)
				unsigned int num_threads = omp_get_num_threads();
				unsigned int thread_num = omp_get_thread_num();
				size_t chunk_size = endpos/num_threads;
				size_t pos = chunk_size*thread_num;
				size_t endpos_local = (thread_num == num_threads-1) ? endpos : pos+chunk_size;
				result = countUtf8Characters(buf, pos, endpos_local);
			}
		}
		else
#endif
		{
			result = countUtf8Characters(buf, 0, endpos);
		}
 		return result;
 	}
}

size_t Notepad_plus::getSelectedAreas()
{
	size_t numSel = _pEditView->execute(SCI_GETSELECTIONS);
	if (numSel == 1) // either 0 or 1 selection
		return (_pEditView->execute(SCI_GETSELECTIONNSTART, 0) == _pEditView->execute(SCI_GETSELECTIONNEND, 0)) ? 0 : 1;
	return (_pEditView->execute(SCI_SELECTIONISRECTANGLE)) ? 1 : numSel;
}

size_t Notepad_plus::getSelectedBytes()
{
	size_t numSel = _pEditView->execute(SCI_GETSELECTIONS);
	size_t result = 0;
	for (size_t i = 0; i < numSel; ++i)
		result += (_pEditView->execute(SCI_GETSELECTIONNEND, i) - _pEditView->execute(SCI_GETSELECTIONNSTART, i));
	return result;
}

int Notepad_plus::wordCount()
{
    FindOption env;
    env._str2Search = L"[^ 	\\\\.,;:!?()+\\r\\n\\-\\*/=\\]\\[{}&~\"'`|@$%<>\\^]+";
    env._searchType = FindRegex;
    return _findReplaceDlg->processAll(ProcessCountAll, &env, true);
}

// ============================================================================
// Synchronized scrolling (upstream Notepad_plus.cpp)
// ============================================================================

void Notepad_plus::checkSyncState()
{
	bool canDoSync = viewVisible(MAIN_VIEW) && viewVisible(SUB_VIEW);
	if (!canDoSync)
	{
		_syncInfo._isSynScrollV = false;
		_syncInfo._isSynScrollH = false;
		checkMenuItem(IDM_VIEW_SYNSCROLLV, false);
		checkMenuItem(IDM_VIEW_SYNSCROLLH, false);
	}
	else
	{
		syncZoom();
	}
	enableCommand(IDM_VIEW_SYNSCROLLV, canDoSync, MENU | TOOLBAR);
	enableCommand(IDM_VIEW_SYNSCROLLH, canDoSync, MENU | TOOLBAR);
	enableCommand(IDM_VIEW_ZOOM_SYNC, canDoSync, MENU);
}

void Notepad_plus::doSynScroll(ScintillaEditView* whichView) // NPLX: was HWND
{
	intptr_t column = 0;
	intptr_t line = 0;
	ScintillaEditView *pView;

	// var for Line
	intptr_t mainCurrentLine, subCurrentLine;

	// var for Column
	intptr_t mxoffset, sxoffset;
	intptr_t pixel;
	intptr_t mainColumn, subColumn;

	if (whichView == _mainEditView)
	{
		if (_syncInfo._isSynScrollV)
		{
			// Compute for Line
			mainCurrentLine = _mainEditView->execute(SCI_GETFIRSTVISIBLELINE);
			subCurrentLine = _subEditView->execute(SCI_GETFIRSTVISIBLELINE);
			line = mainCurrentLine - _syncInfo._line - subCurrentLine;
		}
		if (_syncInfo._isSynScrollH)
		{
			// Compute for Column
			mxoffset = _mainEditView->execute(SCI_GETXOFFSET);
			pixel = _mainEditView->execute(SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>("P"));
			mainColumn = mxoffset/pixel;

			sxoffset = _subEditView->execute(SCI_GETXOFFSET);
			pixel = _subEditView->execute(SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>("P"));
			subColumn = sxoffset/pixel;
			column = mainColumn - _syncInfo._column - subColumn;
		}
		pView = _subEditView;
	}
	else if (whichView == _subEditView)
	{
		if (_syncInfo._isSynScrollV)
		{
			// Compute for Line
			mainCurrentLine = _mainEditView->execute(SCI_GETFIRSTVISIBLELINE);
			subCurrentLine = _subEditView->execute(SCI_GETFIRSTVISIBLELINE);
			line = subCurrentLine + _syncInfo._line - mainCurrentLine;
		}
		if (_syncInfo._isSynScrollH)
		{
			// Compute for Column
			mxoffset = _mainEditView->execute(SCI_GETXOFFSET);
			pixel = _mainEditView->execute(SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>("P"));
			mainColumn = mxoffset/pixel;

			sxoffset = _subEditView->execute(SCI_GETXOFFSET);
			pixel = _subEditView->execute(SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>("P"));
			subColumn = sxoffset/pixel;
			column = subColumn + _syncInfo._column - mainColumn;
		}
		pView = _mainEditView;
	}
	else
		return;

	pView->scroll(column, line);
}

// ============================================================================
// Auto-completion, call tips, auto-indent (upstream Notepad_plus.cpp)
// ============================================================================

void Notepad_plus::showAutoComp()
{
	bool isFromPrimary = _pEditView == _mainEditView;
	AutoCompletion * autoC = isFromPrimary?_autoCompleteMain.get():_autoCompleteSub.get();
	autoC->showApiComplete();
}

void Notepad_plus::showPathCompletion()
{
	bool isFromPrimary = _pEditView == _mainEditView;
	AutoCompletion * autoC = isFromPrimary?_autoCompleteMain.get():_autoCompleteSub.get();
	autoC->showPathCompletion();
}

void Notepad_plus::autoCompFromCurrentFile(bool autoInsert)
{
	bool isFromPrimary = _pEditView == _mainEditView;
	AutoCompletion * autoC = isFromPrimary?_autoCompleteMain.get():_autoCompleteSub.get();
	autoC->showWordComplete(autoInsert);
}

void Notepad_plus::showFunctionComp()
{
	bool isFromPrimary = _pEditView == _mainEditView;
	AutoCompletion * autoC = isFromPrimary?_autoCompleteMain.get():_autoCompleteSub.get();
	autoC->showFunctionComplete();
}

void Notepad_plus::showFunctionNextHint(bool isNext)
{
	bool isFromPrimary = _pEditView == _mainEditView;
	AutoCompletion* autoC = isFromPrimary ? _autoCompleteMain.get() : _autoCompleteSub.get();
	if (isNext)
		autoC->callTipClick(2);
	else
		autoC->callTipClick(1);
}

intptr_t Notepad_plus::findMachedBracePos(size_t startPos, size_t endPos, char targetSymbol, char matchedSymbol)
{
	if (startPos == endPos)
		return -1;

	if (startPos > endPos) // backward
	{
		int balance = 0;
		for (intptr_t i = startPos; i >= static_cast<intptr_t>(endPos); --i)
		{
			char aChar = static_cast<char>(_pEditView->execute(SCI_GETCHARAT, i));
			if (aChar == targetSymbol)
			{
				if (balance == 0)
					return i;
				--balance;
			}
			else if (aChar == matchedSymbol)
			{
				++balance;
			}
		}
	}
	else // forward
	{
	}
	return -1;
}

void Notepad_plus::maintainIndentation(wchar_t ch)
{
	const NppGUI& nppGui = NppParameters::getInstance().getNppGUI();
	if (nppGui._maintainIndent == autoIndent_none)
		return;

	intptr_t eolMode = _pEditView->execute(SCI_GETEOLMODE);
	intptr_t curLine = _pEditView->getCurrentLineNumber();
	intptr_t prevLine = curLine - 1;
	intptr_t indentAmountPrevLine = 0;
	intptr_t tabWidth = _pEditView->execute(SCI_GETTABWIDTH);

	// Do not alter indentation if we were at the beginning of the line and we pressed Enter
	if ((((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
		(eolMode == SC_EOL_CR && ch == '\r')) && prevLine >= 0 && _pEditView->getLineLength(prevLine) == 0)
		return;

	LangType type = _pEditView->getCurrentBuffer()->getLangType();
	ExternalLexerAutoIndentMode autoIndentMode = ExternalLexerAutoIndentMode::Standard;

	// For external languages, query for custom auto-indentation funcionality
	if (type >= L_EXTERNAL)
	{
		NppParameters& nppParam = NppParameters::getInstance();
		autoIndentMode = nppParam.getELCFromIndex(type - L_EXTERNAL)->_autoIndentMode;
		if (autoIndentMode == ExternalLexerAutoIndentMode::Custom)
			return;
	}

	if (nppGui._maintainIndent == autoIndent_basic) // Basic indentation mode only
	{
		if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
			(eolMode == SC_EOL_CR && ch == '\r'))
		{
			// Search the non-empty previous line
			while (prevLine >= 0 && _pEditView->getLineLength(prevLine) == 0)
				prevLine--;

			if (prevLine >= 0)
			{
				indentAmountPrevLine = _pEditView->getLineIndent(prevLine);
			}

			if (indentAmountPrevLine > 0)
			{
				_pEditView->setLineIndent(curLine, indentAmountPrevLine);
			}
		}

		return;
	}

	// else nppGui._maintainIndent == autoIndent_advance

	if (type == L_C || type == L_CPP || type == L_JAVA || type == L_CS || type == L_OBJC ||
		type == L_PHP || type == L_JS_EMBEDDED || type == L_JAVASCRIPT || type == L_JSP || type == L_CSS || type == L_PERL || 
		type == L_RUST || type == L_POWERSHELL || type == L_JSON || type == L_JSON5 || type == L_TYPESCRIPT || type == L_GOLANG || type == L_SWIFT || 
		autoIndentMode == ExternalLexerAutoIndentMode::C_Like)
	{
		if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
			(eolMode == SC_EOL_CR && ch == '\r'))
		{
			// Search the non-empty previous line
			while (prevLine >= 0 && _pEditView->getLineLength(prevLine) == 0)
				prevLine--;

			// Get previous line's Indent
			if (prevLine >= 0)
			{
				indentAmountPrevLine = _pEditView->getLineIndent(prevLine);
			}

			// get previous char from current line
			intptr_t prevPos = _pEditView->execute(SCI_GETCURRENTPOS) - (eolMode == SC_EOL_CRLF ? 3 : 2);
			UCHAR prevChar = (UCHAR)_pEditView->execute(SCI_GETCHARAT, prevPos);
			auto curPos = _pEditView->execute(SCI_GETCURRENTPOS);
			UCHAR nextChar = (UCHAR)_pEditView->execute(SCI_GETCHARAT, curPos);

			if (prevChar == '{')
			{
				if (nextChar == '}')
				{
					const char *eolChars;
					if (eolMode == SC_EOL_CRLF)
						eolChars = "\r\n";
					else if (eolMode == SC_EOL_LF)
						eolChars = "\n";
					else
						eolChars = "\r";

					_pEditView->execute(SCI_INSERTTEXT, _pEditView->execute(SCI_GETCURRENTPOS), reinterpret_cast<LPARAM>(eolChars));
					_pEditView->setLineIndent(curLine + 1, indentAmountPrevLine);
				}
				_pEditView->setLineIndent(curLine, indentAmountPrevLine + tabWidth);
			}
			else if (nextChar == '{')
			{
				_pEditView->setLineIndent(curLine, indentAmountPrevLine);
			}
			// These languages do no support single line control structures without braces.
			else if (type == L_PERL || type == L_RUST || type == L_POWERSHELL || type == L_JSON || type == L_JSON5)
			{
				_pEditView->setLineIndent(curLine, indentAmountPrevLine);
			}
			else if (isConditionExprLine(prevLine))
			{
				_pEditView->setLineIndent(curLine, indentAmountPrevLine + tabWidth);
			}
			else
			{
				if (indentAmountPrevLine > 0)
				{
					if (prevLine > 0 && isConditionExprLine(prevLine - 1))
						_pEditView->setLineIndent(curLine, indentAmountPrevLine - tabWidth);
					else
						_pEditView->setLineIndent(curLine, indentAmountPrevLine);
				}
			}
		}
		else if (ch == '{')
		{
			// if no character in front of {, aligned with prev line's indentation
			auto startPos = _pEditView->execute(SCI_POSITIONFROMLINE, curLine);
			LRESULT endPos = _pEditView->execute(SCI_GETCURRENTPOS);

			for (LRESULT i = endPos - 2; i > 0 && i >= startPos; --i)
			{
				UCHAR aChar = (UCHAR)_pEditView->execute(SCI_GETCHARAT, i);
				if (aChar != ' ' && aChar != '\t')
					return;
			}

			// Search the non-empty previous line
			while (prevLine >= 0 && _pEditView->getLineLength(prevLine) == 0)
				prevLine--;

			// Get previous line's Indent
			if (prevLine >= 0)
			{
				indentAmountPrevLine = _pEditView->getLineIndent(prevLine);

				auto startPos2 = _pEditView->execute(SCI_POSITIONFROMLINE, prevLine);
				auto endPos2 = _pEditView->execute(SCI_GETLINEENDPOSITION, prevLine);
				_pEditView->execute(SCI_SETSEARCHFLAGS, SCFIND_REGEXP | SCFIND_POSIX);
				_pEditView->execute(SCI_SETTARGETRANGE, startPos2, endPos2);

				const char braceExpr[] = "[ \t]*\\{.*";

				intptr_t posFound = _pEditView->execute(SCI_SEARCHINTARGET, strlen(braceExpr), reinterpret_cast<LPARAM>(braceExpr));
				if (posFound >= 0)
				{
					auto end = _pEditView->execute(SCI_GETTARGETEND);
					if (end == endPos2)
						indentAmountPrevLine += tabWidth;
				}
			}

			_pEditView->setLineIndent(curLine, indentAmountPrevLine);

		}
		else if (ch == '}')
		{
			// Look backward for the pair {
			intptr_t startPos = _pEditView->execute(SCI_GETCURRENTPOS);
			if (startPos != 0)
				startPos -= 1;
			intptr_t posFound = findMachedBracePos(startPos - 1, 0, '{', '}');

			// if no { found, do nothing
			if (posFound == -1)
				return;

			// if { is in the same line, do nothing
			intptr_t matchedPairLine = _pEditView->execute(SCI_LINEFROMPOSITION, posFound);
			if (matchedPairLine == curLine)
				return;

			// { is in another line, get its indentation
			indentAmountPrevLine = _pEditView->getLineIndent(matchedPairLine);

			// aligned } indent with {
			_pEditView->setLineIndent(curLine, indentAmountPrevLine);

			/*
			// indent lines from { to }
			for (int i = matchedPairLine + 1; i < curLine; ++i)
				_pEditView->setLineIndent(i, indentAmountPrevLine + tabWidth);
			*/
		}
	}
	else if (type == L_PYTHON)
	{
		if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
			(eolMode == SC_EOL_CR && ch == '\r'))
		{
			// Search the non-empty previous line
			while (prevLine >= 0 && _pEditView->getLineLength(prevLine) == 0)
				prevLine--;

			// Get previous line's Indent
			if (prevLine >= 0)
			{
				indentAmountPrevLine = _pEditView->getLineIndent(prevLine);
			}

			_pEditView->execute(SCI_SETSEARCHFLAGS, SCFIND_REGEXP | SCFIND_POSIX);

			auto startPos = _pEditView->execute(SCI_POSITIONFROMLINE, prevLine);
			auto endPos = _pEditView->execute(SCI_GETLINEENDPOSITION, prevLine);
			_pEditView->execute(SCI_SETTARGETRANGE, startPos, endPos);

			// colon optionally followed by only whitespace and/or start-of-comment, but NOT on a line that is already a comment
			const char colonExpr[] = ":[ \t]*(#|$)";

			auto posColon = _pEditView->execute(SCI_SEARCHINTARGET, strlen(colonExpr), reinterpret_cast<LPARAM>(colonExpr));

			// when colon found, additionally check that it is not in a comment, inside a string, etc.
			if ((posColon >= 0) && (_pEditView->execute(SCI_GETSTYLEINDEXAT, posColon) == SCE_P_OPERATOR))
			{
				_pEditView->setLineIndent(curLine, indentAmountPrevLine + tabWidth);
			}
			else if (indentAmountPrevLine > 0)
			{
				_pEditView->setLineIndent(curLine, indentAmountPrevLine);
			}
		}
	}
	else // Basic indentation mode
	{
		if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
			(eolMode == SC_EOL_CR && ch == '\r'))
		{
			// Search the non-empty previous line
			while (prevLine >= 0 && _pEditView->getLineLength(prevLine) == 0)
				prevLine--;

			if (prevLine >= 0)
			{
				indentAmountPrevLine = _pEditView->getLineIndent(prevLine);
			}

			if (indentAmountPrevLine > 0)
			{
				_pEditView->setLineIndent(curLine, indentAmountPrevLine);
			}
		}
	}
}

// ============================================================================
// Clickable links (upstream Notepad_plus.cpp)
// ============================================================================

void Notepad_plus::addHotSpot(ScintillaEditView* view)
{
	if (_isAttemptingCloseOnQuit)
		return; // don't recalculate URLs when shutting down

	ScintillaEditView* pView = view ? view : _pEditView;
	Buffer* currentBuf = pView->getCurrentBuffer();

	int urlAction = (NppParameters::getInstance()).getNppGUI()._styleURL;
	LPARAM indicStyle = (urlAction == urlNoUnderLineFg) || (urlAction == urlNoUnderLineBg) ? INDIC_HIDDEN : INDIC_PLAIN;
	LPARAM indicHoverStyle = (urlAction == urlNoUnderLineBg) || (urlAction == urlUnderLineBg) ? INDIC_FULLBOX : INDIC_EXPLORERLINK;
	LPARAM indicStyleCur = pView->execute(SCI_INDICGETSTYLE, URL_INDIC);
	LPARAM indicHoverStyleCur = pView->execute(SCI_INDICGETHOVERSTYLE, URL_INDIC);

	if ((indicStyleCur != indicStyle) || (indicHoverStyleCur != indicHoverStyle))
	{
		pView->execute(SCI_INDICSETSTYLE, URL_INDIC, indicStyle);
		pView->execute(SCI_INDICSETHOVERSTYLE, URL_INDIC, indicHoverStyle);
		pView->execute(SCI_INDICSETALPHA, URL_INDIC, 70);
		pView->execute(SCI_INDICSETFLAGS, URL_INDIC, SC_INDICFLAG_VALUEFORE);
	}

	intptr_t startPos = 0;
	intptr_t endPos = -1;
	pView->getVisibleStartAndEndPosition(&startPos, &endPos);
	if (startPos >= endPos) return;
	pView->execute(SCI_SETINDICATORCURRENT, URL_INDIC);

	if (urlAction == urlDisable || !currentBuf->allowClickableLink())
	{
		pView->execute(SCI_INDICATORCLEARRANGE, startPos, endPos - startPos);
		return;
	}

	LRESULT indicFore = pView->execute(SCI_STYLEGETFORE, STYLE_DEFAULT);
	pView->execute(SCI_SETINDICATORVALUE, indicFore);

	UINT cp = static_cast<UINT>(pView->execute(SCI_GETCODEPAGE));
	char* encodedText = nullptr;
	try {
		encodedText = new char[endPos - startPos + 1];
	}
	catch (const std::bad_alloc&)
	{
		return;
	}

	pView->getText(encodedText, startPos, endPos);
	wchar_t* wideText = nullptr;
	try {
		wideText = new wchar_t[endPos - startPos + 1];
	}
	catch (const std::bad_alloc&)
	{
		delete[] encodedText;
		return;
	}

	// NPLX: nplx::encoding in place of MultiByteToWideChar (the text, without its null terminator)
	const std::wstring wideStr = nplx::encoding::toWide(encodedText, std::strlen(encodedText), static_cast<int>(cp));
	int wideTextLen = static_cast<int>(std::min<size_t>(wideStr.size(), static_cast<size_t>(endPos - startPos)));
	std::copy_n(wideStr.data(), wideTextLen, wideText);
	wideText[wideTextLen] = L'\0';
	delete[] encodedText;
	if (wideTextLen > 0)
	{
		int startWide = 0;
		int lenWide = 0;
		int startEncoded = 0;
		int lenEncoded = 0;
		while (true)
		{
			bool r = isUrl(wideText, wideTextLen, startWide, & lenWide);
			if (lenWide <= 0)
				break;
			assert ((startWide + lenWide) <= wideTextLen);
			lenEncoded = static_cast<int>(nplx::encoding::fromWide(& wideText [startWide], lenWide, static_cast<int>(cp)).size()); // NPLX: was WideCharToMultiByte
			if (r)
				pView->execute(SCI_INDICATORFILLRANGE, startEncoded + startPos, lenEncoded);
			else
				pView->execute(SCI_INDICATORCLEARRANGE, startEncoded + startPos, lenEncoded);
			startWide += lenWide;
			startEncoded += lenEncoded;

			if ((startWide >= wideTextLen) || ((startEncoded + startPos) >= endPos))
				break;
		}
	}

	delete[] wideText;
}

void Notepad_plus::drawAutocompleteColoursFromTheme(COLORREF fgColor, COLORREF bgColor)
{
	if (bgColor == 0xFFFFFF)
	{
		// default colors from PlatWin.cxx void ListBoxX::Draw(DRAWITEMSTRUCT *pDrawItem)
		// NPLX: the application palette in place of ::GetSysColor
		auto sysColor = [](QPalette::ColorRole role) {
			const QColor c = QApplication::palette().color(role);
			return static_cast<COLORREF>(RGB(c.red(), c.green(), c.blue()));
		};
		COLORREF autocompleteBg = sysColor(QPalette::Base);
		COLORREF selectedBg = sysColor(QPalette::Highlight);
		COLORREF autocompleteText = sysColor(QPalette::Text);
		COLORREF selectedText = sysColor(QPalette::HighlightedText);

		AutoCompletion::setColour(autocompleteBg, AutoCompletion::AutocompleteColorIndex::autocompleteBg);
		AutoCompletion::setColour(selectedBg, AutoCompletion::AutocompleteColorIndex::selectedBg);
		AutoCompletion::setColour(autocompleteText, AutoCompletion::AutocompleteColorIndex::autocompleteText);
		AutoCompletion::setColour(selectedText, AutoCompletion::AutocompleteColorIndex::selectedText);

		// default colors from CallTip.cxx CallTip::CallTip() noexcept
		COLORREF colourBG = bgColor;
		COLORREF colourUnSel = RGB(0x80, 0x80, 0x80);
		COLORREF colourSel = RGB(0, 0, 0x80);

		AutoCompletion::setColour(colourBG, AutoCompletion::AutocompleteColorIndex::calltipBg);
		AutoCompletion::setColour(colourUnSel, AutoCompletion::AutocompleteColorIndex::calltipText);
		AutoCompletion::setColour(colourSel, AutoCompletion::AutocompleteColorIndex::calltipHighlight);

		return;
	}

	int rbv = GetRValue(bgColor);
	int gbv = GetGValue(bgColor);
	int bbv = GetBValue(bgColor);

	int rfv = GetRValue(fgColor);
	int gfv = GetGValue(fgColor);
	int bfv = GetBValue(fgColor);

	COLORREF bgDarker = RGB(rbv - 20 <= 0 ? 0 : rbv - 20, gbv - 20 <= 0 ? 0 : gbv - 20, bbv - 20 <= 0 ? 0 : bbv - 20);

	if (bgColor == RGB(0, 0, 0)) // if the bg is pure black
		bgDarker = RGB(20, 20, 20); // make bgDarker lighter for distinguishing between both

	COLORREF fgDarker = RGB(rfv - 20 <= 0 ? 0 : rfv - 20, gfv - 20 <= 0 ? 0 : gfv - 20, bfv - 20 <= 0 ? 0 : bfv - 20);
	COLORREF fgLigher = RGB(rfv + 20 >= 255 ? 255 : rfv + 20, gfv + 20 >= 255 ? 255 : gfv + 20, bfv + 20 >= 255 ? 255 : bfv + 20);

	AutoCompletion::setColour(bgDarker, AutoCompletion::AutocompleteColorIndex::autocompleteBg);
	AutoCompletion::setColour(bgColor, AutoCompletion::AutocompleteColorIndex::selectedBg);
	AutoCompletion::setColour(fgDarker, AutoCompletion::AutocompleteColorIndex::autocompleteText);
	AutoCompletion::setColour(fgColor, AutoCompletion::AutocompleteColorIndex::selectedText);

	AutoCompletion::setColour(bgDarker, AutoCompletion::AutocompleteColorIndex::calltipBg);
	AutoCompletion::setColour(fgDarker, AutoCompletion::AutocompleteColorIndex::calltipText);
	AutoCompletion::setColour(fgLigher, AutoCompletion::AutocompleteColorIndex::calltipHighlight);
	
}
