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

// NPLX: command line handling of upstream winmain.cpp. The same options are accepted
// (-multiInst, -nosession, -ro, -n<line>, -c<col>, -p<pos>, -l<lang>, -L<loc>, -udl=, -titleAdd=,
// -settingsDir=, -openSession, -r, -fullReadOnly, -fullReadOnlySavingForbidden, -alwaysOnTop, -notabbar ...).
// On Linux the arguments arrive already split (argv), so upstream parseCommandLine is not needed.

#pragma once

#include <string>
#include <vector>

#include "Parameters.h"

using ParamVector = std::vector<std::wstring>;

struct NppCommandLine
{
	CmdLineParams _params;
	bool _isMultiInst = false;
	bool _showHelp = false;
	bool _showVersion = false;
	bool _doFunctionListExport = false; // -export=functionList (upstream FLAG_FUNCLSTEXPORT)
	std::wstring _settingsDir;
	std::wstring _titleBarAdd;
	std::vector<std::wstring> _files; // absolute paths
};

// Parses argv[1..] like upstream wWinMain; relative file names are resolved against the current directory
NppCommandLine parseNppCommandLine(int argc, char* argv[]);

// Applies the parts that must be set before NppParameters::load() (settings dir, title, localization)
void applyCommandLineBeforeLoad(const NppCommandLine& cmdLine);

// Applies the parts that need the loaded parameters (upstream Notepad_plus::init / Notepad_plus_Window::init)
void applyCommandLineAfterLoad(const NppCommandLine& cmdLine);

const char* commandLineHelpText();
