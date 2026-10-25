// This file is part of NotePadLX, a Linux port of Notepad++.
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

// NPLX: ScintillaEditView members that are not in upstream (kept out of the ported file so that
// it stays diffable against upstream ScintillaEditView.cpp).

#include "ScintillaEditView.h"

#include <algorithm>

#include "ILexer.h"
#include "Lexilla.h"
#include "BoostRegexSearch.h"

bool ScintillaEditView::setLexerByName(const char* lexerName)
{
	Scintilla::ILexer5* lexer = CreateLexer(lexerName);
	if (!lexer)
		return false;

	execute(SCI_SETILEXER, 0, reinterpret_cast<LPARAM>(lexer));
	execute(SCI_COLOURISE, 0, -1);
	return true;
}

std::string ScintillaEditView::lexerName() const
{
	const LRESULT len = execute(SCI_GETLEXERLANGUAGE, 0, 0);
	std::string name(static_cast<size_t>(std::max<LRESULT>(len, 0)), '\0');
	if (len > 0)
		execute(SCI_GETLEXERLANGUAGE, 0, reinterpret_cast<LPARAM>(name.data()));
	return name;
}

intptr_t ScintillaEditView::findRegex(const std::string& pattern, intptr_t from, intptr_t* matchEnd, bool matchCase)
{
	const intptr_t docLength = execute(SCI_GETLENGTH);
	int flags = SCFIND_REGEXP;
	if (matchCase)
		flags |= SCFIND_MATCHCASE;

	execute(SCI_SETSEARCHFLAGS, flags);
	execute(SCI_SETTARGETRANGE, from, docLength);
	const intptr_t pos = execute(SCI_SEARCHINTARGET, pattern.length(), reinterpret_cast<LPARAM>(pattern.c_str()));
	if (pos >= 0 && matchEnd)
		*matchEnd = execute(SCI_GETTARGETEND);
	return pos;
}

std::string ScintillaEditView::regexErrorMessage() const
{
	const LRESULT len = execute(SCI_GETBOOSTREGEXERRMSG, 0, 0); // includes terminating NUL
	if (len <= 1)
		return {};
	std::string msg(static_cast<size_t>(len), '\0');
	execute(SCI_GETBOOSTREGEXERRMSG, static_cast<WPARAM>(len), reinterpret_cast<LPARAM>(msg.data()));
	msg.resize(static_cast<size_t>(len - 1));
	return msg;
}

std::string ScintillaEditView::getText() const
{
	const intptr_t len = execute(SCI_GETLENGTH);
	std::string text(static_cast<size_t>(len) + 1, '\0');
	execute(SCI_GETTEXT, len + 1, reinterpret_cast<LPARAM>(text.data()));
	text.resize(static_cast<size_t>(len));
	return text;
}

void ScintillaEditView::setText(const std::string& text)
{
	execute(SCI_CLEARALL);
	execute(SCI_APPENDTEXT, text.length(), reinterpret_cast<LPARAM>(text.data()));
}
