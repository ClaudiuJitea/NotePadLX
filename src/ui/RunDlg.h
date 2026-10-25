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

// NPLX: port of upstream WinControls/StaticDialog/RunDlg (Run > Run..., the Run menu user commands).
// Command::run expands the environment variables and the $(NPP_VARIABLES), then starts the program.
// Linux: ::ShellExecute(L"open") is a detached QProcess for a program (absolute path or found in PATH),
// else the default application of the file or URL (QDesktopServices).

#pragma once

#include <string>

#include "ParentMessenger.h"
#include "RcDialog.h"
#include "shortcut.h"

#define CURRENTWORD_MAXLENGTH 2048

const wchar_t fullCurrentPath[] = L"FULL_CURRENT_PATH";
const wchar_t currentDirectory[] = L"CURRENT_DIRECTORY";
const wchar_t onlyFileName[] = L"FILE_NAME";
const wchar_t fileNamePart[] = L"NAME_PART";
const wchar_t fileExtPart[] = L"EXT_PART";
const wchar_t currentWord[] = L"CURRENT_WORD";
const wchar_t nppDir[] = L"NPP_DIRECTORY";
const wchar_t nppFullFilePath[] = L"NPP_FULL_FILE_PATH";
const wchar_t currentLine[] = L"CURRENT_LINE";
const wchar_t currentColumn[] = L"CURRENT_COLUMN";
const wchar_t currentLineStr[] = L"CURRENT_LINESTR";

int whichVar(const wchar_t *str);
// NPLX: hWnd was the window answering RUNCOMMAND_USER + var; returns the expanded string (no length limit)
std::wstring expandNppEnvironmentStrs(const std::wstring& strSrc, const ParentMessenger& sendToNpp);
// NPLX: upstream ::ExpandEnvironmentStrings (%VAR%); also the POSIX $VAR and ${VAR} forms
std::wstring expandEnvironmentStrings(const std::wstring& str);

class Command {
public :
	Command() = default;
	explicit Command(const wchar_t* cmd) : _cmdLine(cmd) {}
	explicit Command(const std::wstring& cmd) : _cmdLine(cmd) {}
	// NPLX: true on success (upstream: an HINSTANCE > 32)
	bool run(const ParentMessenger& sendToNpp);
	bool run(const ParentMessenger& sendToNpp, const wchar_t* cwd);

	static void extractArgs(std::wstring& cmd2Exec, std::wstring& args, const std::wstring& cmdEntier);

protected :
	std::wstring _cmdLine;
};

class RunDlg : public RcDialog, public Command
{
	Q_OBJECT

public :
	explicit RunDlg(QWidget* parent = nullptr) : RcDialog(parent) {}

	// sendToNpp: RUNCOMMAND_USER + var; userCommandAdded: what upstream does to the Run menu after "Save..."
	void init(ParentMessenger sendToNpp, std::function<void(const UserCommand&)> userCommandAdded,
		std::function<bool(const KeyCombo&)> isConflict) {
		_sendToNpp = std::move(sendToNpp);
		_userCommandAdded = std::move(userCommandAdded);
		_isConflict = std::move(isConflict);
	}

	void doDialog();

protected :
	void insertVariable(unsigned char id);
	void onCommand(int id, int notification) override;
	void reject() override { hide(); } // upstream IDCANCEL: display(false)

private :
	void addTextToCombo(const wchar_t *txt2Add) const;
	void removeTextFromCombo(const wchar_t *txt2Remove) const;

	ParentMessenger _sendToNpp;
	std::function<void(const UserCommand&)> _userCommandAdded;
	std::function<bool(const KeyCombo&)> _isConflict;
};
