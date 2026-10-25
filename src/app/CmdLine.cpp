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

#include "CmdLine.h"

#include <stdexcept>

namespace
{
	const wchar_t FLAG_MULTI_INSTANCE[] = L"-multiInst";
	const wchar_t FLAG_NO_PLUGIN[] = L"-noPlugin";
	const wchar_t FLAG_READONLY[] = L"-ro"; // for current cmdline file(s) only
	const wchar_t FLAG_FULL_READONLY[] = L"-fullReadOnly";
	const wchar_t FLAG_FULL_READONLY_SAVING_FORBIDDEN[] = L"-fullReadOnlySavingForbidden";
	const wchar_t FLAG_NOSESSION[] = L"-nosession";
	const wchar_t FLAG_NOTABBAR[] = L"-notabbar";
	const wchar_t FLAG_SYSTRAY[] = L"-systemtray";
	const wchar_t FLAG_LOADINGTIME[] = L"-loadingTime";
	const wchar_t FLAG_HELP[] = L"--help";
	const wchar_t FLAG_ALWAYS_ON_TOP[] = L"-alwaysOnTop";
	const wchar_t FLAG_OPENSESSIONFILE[] = L"-openSession";
	const wchar_t FLAG_RECURSIVE[] = L"-r";
	const wchar_t FLAG_OPEN_FOLDERS_AS_WORKSPACE[] = L"-openFoldersAsWorkspace";
	const wchar_t FLAG_SETTINGS_DIR[] = L"-settingsDir=";
	const wchar_t FLAG_TITLEBAR_ADD[] = L"-titleAdd=";
	const wchar_t FLAG_APPLY_UDL[] = L"-udl=";
	const wchar_t FLAG_PLUGIN_MESSAGE[] = L"-pluginMessage=";
	const wchar_t FLAG_MONITOR_FILES[] = L"-monitor";
	const wchar_t FLAG_MONITORING_MODE[] = L"-monitoringMode";
	const wchar_t FLAG_FUNCLSTEXPORT[] = L"-export=functionList";

	bool isInList(const wchar_t* token2Find, ParamVector& params, bool eraseArg = true)
	{
		for (auto it = params.begin(); it != params.end(); ++it)
		{
			if (lstrcmp(token2Find, it->c_str()) == 0)
			{
				if (eraseArg) params.erase(it);
				return true;
			}
		}
		return false;
	}

	bool getParamVal(wchar_t c, ParamVector& params, std::wstring& value)
	{
		value = L"";
		size_t nbItems = params.size();

		for (size_t i = 0; i < nbItems; ++i)
		{
			const wchar_t* token = params.at(i).c_str();
			if (token[0] == '-' && lstrlen(token) >= 2 && token[1] == c) //dash, and enough chars
			{
				value = (token + 2);
				params.erase(params.begin() + i);
				return true;
			}
		}
		return false;
	}

	bool getParamValFromString(const wchar_t* str, ParamVector& params, std::wstring& value)
	{
		value = L"";
		size_t nbItems = params.size();

		for (size_t i = 0; i < nbItems; ++i)
		{
			const wchar_t* token = params.at(i).c_str();
			std::wstring tokenStr = token;
			size_t pos = tokenStr.find(str);
			if (pos != std::wstring::npos && pos == 0)
			{
				value = (token + lstrlen(str));
				params.erase(params.begin() + i);
				return true;
			}
		}
		return false;
	}

	LangType getLangTypeFromParam(ParamVector& params)
	{
		std::wstring langStr;
		if (!getParamVal('l', params, langStr))
			return L_EXTERNAL;
		return NppParameters::getLangIDFromStr(langStr.c_str());
	}

	std::wstring getLocalizationPathFromParam(ParamVector& params)
	{
		std::wstring locStr;
		if (!getParamVal('L', params, locStr))
			return L"";
		locStr = stringToLower(stringReplace(locStr, L"_", L"-")); // convert to lowercase format with "-" as separator
		return NppParameters::getLocPathFromStr(locStr);
	}

	intptr_t getNumberFromParam(char paramName, ParamVector& params, bool& isParamePresent)
	{
		std::wstring numStr;
		if (!getParamVal(paramName, params, numStr))
		{
			isParamePresent = false;
			return -1;
		}
		isParamePresent = true;
		try
		{
			return static_cast<intptr_t>(std::stoll(numStr));
		}
		catch (...) // NPLX: upstream lets std::stoll throw
		{
			isParamePresent = false;
			return -1;
		}
	}

	std::wstring unquote(std::wstring s)
	{
		if (s.length() >= 2 && s.front() == '"' && s.back() == '"')
			s = s.substr(1, s.length() - 2);
		return s;
	}
}

NppCommandLine parseNppCommandLine(int argc, char* argv[])
{
	NppCommandLine result;
	ParamVector params;
	for (int i = 1; i < argc; ++i)
		params.push_back(string2wstring(argv[i], CP_UTF8));

	result._showVersion = isInList(L"--version", params) || isInList(L"-v", params);
	result._isMultiInst = isInList(FLAG_MULTI_INSTANCE, params);
	result._doFunctionListExport = isInList(FLAG_FUNCLSTEXPORT, params);

	CmdLineParams& cmdLineParams = result._params;
	cmdLineParams._displayCmdLineArgs = isInList(FLAG_HELP, params) || isInList(L"-h", params);
	result._showHelp = cmdLineParams._displayCmdLineArgs;
	cmdLineParams._isNoTab = isInList(FLAG_NOTABBAR, params);
	cmdLineParams._isNoPlugin = isInList(FLAG_NO_PLUGIN, params);
	cmdLineParams._isReadOnly = isInList(FLAG_READONLY, params);
	cmdLineParams._isFullReadOnly = isInList(FLAG_FULL_READONLY, params);
	cmdLineParams._isFullReadOnlySavingForbidden = isInList(FLAG_FULL_READONLY_SAVING_FORBIDDEN, params);
	cmdLineParams._isNoSession = isInList(FLAG_NOSESSION, params);
	cmdLineParams._isPreLaunch = isInList(FLAG_SYSTRAY, params);
	cmdLineParams._alwaysOnTop = isInList(FLAG_ALWAYS_ON_TOP, params);
	cmdLineParams._showLoadingTime = isInList(FLAG_LOADINGTIME, params);
	cmdLineParams._isSessionFile = isInList(FLAG_OPENSESSIONFILE, params);
	cmdLineParams._isRecursive = isInList(FLAG_RECURSIVE, params);
	cmdLineParams._openFoldersAsWorkspace = isInList(FLAG_OPEN_FOLDERS_AS_WORKSPACE, params);
	cmdLineParams._monitorFiles = isInList(FLAG_MONITOR_FILES, params);
	cmdLineParams._monitoringMode = isInList(FLAG_MONITORING_MODE, params);

	cmdLineParams._langType = getLangTypeFromParam(params);
	cmdLineParams._localizationPath = getLocalizationPathFromParam(params);

	std::wstring value;
	if (getParamValFromString(FLAG_PLUGIN_MESSAGE, params, value))
		cmdLineParams._pluginMessage = unquote(value);
	if (getParamValFromString(FLAG_SETTINGS_DIR, params, value))
		result._settingsDir = unquote(value);
	if (getParamValFromString(FLAG_TITLEBAR_ADD, params, value))
		result._titleBarAdd = unquote(value);
	if (getParamValFromString(FLAG_APPLY_UDL, params, value))
		cmdLineParams._udlName = unquote(value);

	// getNumberFromParam should be run at the end, to not consuming the other params
	bool isParamePresent = false;
	cmdLineParams._line2go = getNumberFromParam('n', params, isParamePresent);
	cmdLineParams._column2go = getNumberFromParam('c', params, isParamePresent);
	cmdLineParams._pos2go = getNumberFromParam('p', params, isParamePresent);
	cmdLineParams._point.x = static_cast<LONG>(getNumberFromParam('x', params, cmdLineParams._isPointXValid));
	cmdLineParams._point.y = static_cast<LONG>(getNumberFromParam('y', params, cmdLineParams._isPointYValid));

	// Everything left is a file name (upstream: relativeFilePathToFullFilePath)
	for (const std::wstring& p : params)
	{
		if (!p.empty())
			result._files.push_back(relativeFilePathToFullFilePath(p.c_str()));
	}
	return result;
}

void applyCommandLineBeforeLoad(const NppCommandLine& cmdLine)
{
	NppParameters& nppParameters = NppParameters::getInstance();
	if (!cmdLine._settingsDir.empty())
		nppParameters.setCmdSettingsDir(cmdLine._settingsDir);
	if (!cmdLine._titleBarAdd.empty())
		nppParameters.setTitleBarAdd(cmdLine._titleBarAdd);
	if (!cmdLine._params._localizationPath.empty())
		nppParameters.setStartWithLocFileName(cmdLine._params._localizationPath); // should be called before parameters are loaded
}

void applyCommandLineAfterLoad(const NppCommandLine& cmdLine)
{
	NppParameters& nppParameters = NppParameters::getInstance();
	NppGUI& nppGUI = nppParameters.getNppGUI();

	// upstream Notepad_plus_Window::init
	if (cmdLine._params._isNoSession)
		nppGUI._isCmdlineNosessionActivated = true;
	if (cmdLine._params._isFullReadOnly)
		nppGUI._isFullReadOnly = true;
	if (cmdLine._params._isFullReadOnlySavingForbidden)
		nppGUI._isFullReadOnlySavingForbidden = true;
	nppParameters.setCmdlineParam(CmdLineParamsDTO::FromCmdLineParams(cmdLine._params));
}

const char* commandLineHelpText()
{
	return
		"Usage: notepadlx [--help] [--version] [-multiInst] [-noPlugin] [-l<Language>] [-L<langCode>] [-udl=\"My UDL Name\"]\n"
		"                 [-n<line>] [-c<column>] [-p<position>] [-x<x>] [-y<y>] [-nosession] [-notabbar] [-ro]\n"
		"                 [-fullReadOnly] [-fullReadOnlySavingForbidden] [-systemtray] [-loadingTime] [-alwaysOnTop]\n"
		"                 [-openSession] [-r] [-titleAdd=\"text\"] [-settingsDir=\"dir\"] [-monitor] [filePath...]\n"
		"\n"
		"  --help        This help message\n"
		"  -multiInst    Launch another NotePadLX instance\n"
		"  -l<Language>  Open files by applying <Language> to the text, e.g. -lpython (names from langs.xml)\n"
		"  -L<langCode>  Apply indicated localization, e.g. -Lfr_FR\n"
		"  -udl=\"Name\"   Apply the User Defined Language \"Name\" to the files\n"
		"  -n<line>      Scroll to indicated line on the opened file\n"
		"  -c<column>    Scroll to indicated column on the opened file\n"
		"  -p<position>  Scroll to indicated position on the opened file\n"
		"  -nosession    Launch NotePadLX without the previous session\n"
		"  -ro           Open files in read only mode\n"
		"  -fullReadOnly Open all files in read only mode\n"
		"  -openSession  Open a session file (the only file argument)\n"
		"  -r            Open files recursively (with wildcards)\n"
		"  -titleAdd=    Add a dash and a space before the text, in the title bar\n"
		"  -settingsDir= Override the settings directory (default ~/.config/notepadlx)\n"
		"  filePath      File or folder to open (wildcards allowed)\n";
}
