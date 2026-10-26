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

#include "RunDlg.h"

#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>

#include "LocalizationQt.h"
#include "Common.h"
#include "Notepad_plus_msgs.h"
#include "Parameters.h"
#include "RunDlg_rc.h"
#include "RunMacroDlg_rc.h"
#include "ShortcutDlg.h"
#include "UiServices.h"
#include "menuCmdID.h"
#include "resource.h"
#include "shortcutRc.h"

using namespace std;

namespace
{
#include "RcShortcutRunDialogs.inc"
}

void Command::extractArgs(wstring& cmd2Exec, wstring& args, const wstring& cmdEntier)
{
	// NPLX: upstream works on fixed-size buffers (MAX_PATH); same splitting on std::wstring
	size_t i = 0;
	bool quoted = false;

	const size_t cmdEntierLen = cmdEntier.length();

	cmd2Exec.clear();
	args.clear();

	for (; i < cmdEntierLen; ++i)
	{
		if (cmdEntier[i] == ' ' && !quoted)
			break;

		if (cmdEntier[i]=='"')
			quoted = !quoted;

		cmd2Exec += cmdEntier[i];
	}

	if (i < cmdEntierLen)
	{
		for (; (i < cmdEntierLen) && (cmdEntier[i] == ' ') ; ++i);

		if (i < cmdEntierLen)
			args = cmdEntier.substr(i);

		// trailing blanks
		const size_t l = args.find_last_not_of(L' ');
		args.erase(l == wstring::npos ? 0 : l + 1);
	}
}


int whichVar(const wchar_t *str)
{
	if (!lstrcmp(fullCurrentPath, str))
		return FULL_CURRENT_PATH;
	else if (!lstrcmp(currentDirectory, str))
		return CURRENT_DIRECTORY;
	else if (!lstrcmp(onlyFileName, str))
		return FILE_NAME;
	else if (!lstrcmp(fileNamePart, str))
		return NAME_PART;
	else if (!lstrcmp(fileExtPart, str))
		return EXT_PART;
	else if (!lstrcmp(currentWord, str))
		return CURRENT_WORD;
	else if (!lstrcmp(nppDir, str))
		return NPP_DIRECTORY;
	else if (!lstrcmp(nppFullFilePath, str))
		return NPP_FULL_FILE_PATH;
	else if (!lstrcmp(currentLine, str))
		return CURRENT_LINE;
	else if (!lstrcmp(currentColumn, str))
		return CURRENT_COLUMN;
	else if (!lstrcmp(currentLineStr, str))
		return CURRENT_LINESTR;

	return VAR_NOT_RECOGNIZED;
}

wstring expandNppEnvironmentStrs(const wstring& strSrc, const ParentMessenger& sendToNpp)
{
	wstring stringDest;
	for (int i = 0, len = static_cast<int>(strSrc.length()); i < len; ++i)
	{
		int iBegin = -1;
		int iEnd = -1;
		if ((strSrc[i] == '$') && (i + 1 < len) && (strSrc[i+1] == '('))
		{
			iBegin = i += 2;
			for (; i < len ; ++i)
			{
				if (strSrc[i] == ')')
				{
					iEnd = i - 1;
					break;
				}
			}
		}

		if (iBegin != -1)
		{
			if (iEnd != -1)
			{
				const wstring str = strSrc.substr(iBegin, iEnd - iBegin + 1);

				int internalVar = whichVar(str.c_str());
				if (internalVar == VAR_NOT_RECOGNIZED || !sendToNpp)
				{
					i = iBegin - 2;
					stringDest += strSrc[i];
				}
				else
				{
					wstring expandedStr;
					if (internalVar == CURRENT_LINE || internalVar == CURRENT_COLUMN)
					{
						size_t lineNumber = sendToNpp(RUNCOMMAND_USER + internalVar, 0, 0);
						expandedStr = std::to_wstring(lineNumber);
					}
					else
					{
						wchar_t buf[CURRENTWORD_MAXLENGTH] = { '\0' };
						sendToNpp(RUNCOMMAND_USER + internalVar, CURRENTWORD_MAXLENGTH, reinterpret_cast<sptr_t>(buf));
						expandedStr = buf;
					}

					stringDest += expandedStr;
				}
			}
			else
			{
				i = iBegin - 2;
				stringDest += strSrc[i];
			}
		}
		else
			stringDest += strSrc[i];
	}
	return stringDest;
}

wstring expandEnvironmentStrings(const wstring& str)
{
	// upstream ::ExpandEnvironmentStrings: %NAME% (an undefined variable is left as is)
	auto envValue = [](const wstring& name, bool& found) -> wstring {
		const QByteArray value = qgetenv(QString::fromStdWString(name).toLocal8Bit().constData());
		found = !value.isNull();
		return QString::fromLocal8Bit(value).toStdWString();
	};

	wstring result;
	for (size_t i = 0, len = str.length(); i < len; ++i)
	{
		const wchar_t c = str[i];
		if (c == L'%')
		{
			const size_t end = str.find(L'%', i + 1);
			if (end != wstring::npos && end > i + 1)
			{
				bool found = false;
				const wstring value = envValue(str.substr(i + 1, end - i - 1), found);
				if (found)
				{
					result += value;
					i = end;
					continue;
				}
			}
		}
		else if (c == L'$' && i + 1 < len && str[i + 1] != L'(') // $(VAR) is a Notepad++ variable
		{
			// NPLX: POSIX $NAME and ${NAME}
			size_t nameBegin = i + 1;
			size_t nameEnd = nameBegin;
			bool braced = false;
			if (str[nameBegin] == L'{')
			{
				braced = true;
				nameEnd = str.find(L'}', nameBegin);
				++nameBegin;
			}
			else
			{
				while (nameEnd < len && (iswalnum(str[nameEnd]) || str[nameEnd] == L'_'))
					++nameEnd;
			}
			if (nameEnd != wstring::npos && nameEnd > nameBegin && !iswdigit(str[nameBegin]))
			{
				bool found = false;
				const wstring value = envValue(str.substr(nameBegin, nameEnd - nameBegin), found);
				if (found)
				{
					result += value;
					i = braced ? nameEnd : nameEnd - 1;
					continue;
				}
			}
		}
		result += c;
	}
	return result;
}

bool Command::run(const ParentMessenger& sendToNpp)
{
	return run(sendToNpp, L".");
}

bool Command::run(const ParentMessenger& sendToNpp, const wchar_t* cwd)
{
	wstring cmdPure;
	wstring args;

	extractArgs(cmdPure, args, _cmdLine);
	const wstring cmdIntermediate = expandEnvironmentStrings(cmdPure);
	const wstring argsIntermediate = expandEnvironmentStrings(args);

	wstring cmd2Exec = expandNppEnvironmentStrs(cmdIntermediate, sendToNpp);
	const wstring args2Exec = expandNppEnvironmentStrs(argsIntermediate, sendToNpp);

	const wstring cwd2Exec = expandNppEnvironmentStrs(cwd, sendToNpp);

	// NPLX: ::ShellExecute(hWnd, L"open", cmd2Exec, args2Exec, cwd2Exec, SW_SHOW) -- ShellExecute strips the quotes
	if (cmd2Exec.length() >= 2 && cmd2Exec.front() == L'"' && cmd2Exec.back() == L'"')
		cmd2Exec = cmd2Exec.substr(1, cmd2Exec.length() - 2);

	const QString program = QString::fromStdWString(cmd2Exec);
	const QString workingDir = QString::fromStdWString(cwd2Exec);
	QString error;
	bool ok = false;

	if (program.isEmpty())
	{
		error = QStringLiteral("No program to run.");
	}
	else if (program.contains(QStringLiteral("://")) && args2Exec.empty())
	{
		// a URL: the default browser (ShellExecute "open" on a URL)
		ok = QDesktopServices::openUrl(QUrl(program));
	}
	else
	{
		QString executable;
		const QFileInfo fi(QDir(workingDir).filePath(program));
		if (program.contains(QLatin1Char('/')))
		{
			if (fi.isFile() && fi.isExecutable())
				executable = fi.absoluteFilePath();
		}
		else
		{
			executable = QStandardPaths::findExecutable(program);
		}

		if (!executable.isEmpty())
		{
			const QStringList argList = QProcess::splitCommand(QString::fromStdWString(args2Exec));
			ok = QProcess::startDetached(executable, argList, workingDir);
			if (!ok)
				error = QStringLiteral("The program could not be started.");
		}
		else if (fi.exists() && args2Exec.empty())
		{
			// a document: its default application (ShellExecute "open" on a file)
			ok = QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
		}
		else
		{
			error = QStringLiteral("The system cannot find the file specified.");
		}
	}

	if (!ok)
	{
		wstring errorMsg;
		errorMsg += error.toStdWString();
		errorMsg += L"\nAn attempt was made to execute the below command.";
		errorMsg += L"\n----------------------------------------------------------";
		errorMsg += L"\nCommand: ";
		errorMsg += cmd2Exec;
		errorMsg += L"\nArguments: ";
		errorMsg += args2Exec;
		errorMsg += L"\n----------------------------------------------------------";

		nplx::messageBox(errorMsg, L"ShellExecute - ERROR", MB_ICONINFORMATION | MB_APPLMODAL);
	}

	return ok;
}

void RunDlg::insertVariable(unsigned char id)
{
	wstring cmd = getItemText(IDC_COMBO_RUN_PATH);

	wstring variable;
	switch (id)
	{
	case 0:
		variable = fullCurrentPath;
		break;
	case 1:
		variable = currentDirectory;
		break;
	case 2:
		variable = onlyFileName;
		break;
	case 3:
		variable = fileNamePart;
		break;
	case 4:
		variable = fileExtPart;
		break;
	case 5:
		variable = currentWord;
		break;
	case 6:
		variable = nppDir;
		break;
	case 7:
		variable = nppFullFilePath;
		break;
	case 8:
		variable = currentLine;
		break;
	case 9:
		variable = currentColumn;
		break;
	case 10:
		variable = currentLineStr;
		break;
	default:
		return; // Invalid id number
	}

	wstring cmdNew = cmd;
	cmdNew += L"$(" + variable + L")";
	setItemText(IDC_COMBO_RUN_PATH, cmdNew);
}

void RunDlg::onCommand(int id, int notification)
{
	if (notification != RcNotifyClicked)
		return;

	if (id >= IDM_RUN_DLG_VARMENU_START && id <= IDM_RUN_DLG_VARMENU_END)
	{
		insertVariable(static_cast<unsigned char>(id - IDM_RUN_DLG_VARMENU_START));
		return;
	}

	switch (id)
	{
		case IDCANCEL :
			hide();
			return;

		case IDOK :
		{
			_cmdLine = getItemText(IDC_COMBO_RUN_PATH);

			if (run(_sendToNpp))
			{
				addTextToCombo(_cmdLine.c_str());
				hide();
			}
			else
			{
				removeTextFromCombo(_cmdLine.c_str());
			}
			return;
		}

		case IDC_BUTTON_SAVE :
		{
			NppParameters& nppParams = NppParameters::getInstance();
			std::vector<UserCommand> & theUserCmds = nppParams.getUserCommandList();

			const auto nbCmd = static_cast<int>(theUserCmds.size());
			int cmdID = ID_USER_CMD + nbCmd;

			const wstring cmd = getItemText(IDC_COMBO_RUN_PATH);
			UserCommand uc(Shortcut(), wstring2string(cmd, CP_UTF8).c_str(), cmdID);

			ShortcutDlg shortcutDlg(uc, _isConflict, this);
			if (shortcutDlg.doDialog() != -1)
			{
				// NPLX: the Run menu insertion (upstream here) is done by Notepad_plus
				if (_userCommandAdded)
					_userCommandAdded(uc);
			}
			return;
		}

		case IDC_BUTTON_FILE_BROWSER:
		{
			// NPLX: upstream filter "Executable File" (*.exe;*.com;*.cmd;*.bat); Linux executables have no extension
			wstring fn = nplx::uiServices() ? nplx::uiServices()->getOpenFileName(L"", L"All Files (*)", L"") : wstring();
			if (!fn.empty())
			{
				if (fn.find(' ') != wstring::npos)
				{
					wstring fn_quotes(fn);
					fn_quotes = L"\"" + fn_quotes + L"\"";
					addTextToCombo(fn_quotes.c_str());
				}
				else
				{
					addTextToCombo(fn.c_str());
				}
			}
			return;
		}

		case IDC_BUTTON_VARIABLES:
		{
			// upstream IDR_RUN_DLG_MENU_VARIABLES (RunDlg.rc)
			static const wchar_t* const variablesMenu[] = {
				L"&FULL_CURRENT_PATH\tFull path to active file",
				L"CURRENT_&DIRECTORY\tActive file's directory",
				L"FILE_&NAME\tActive file's name",
				L"NAME_&PART\tFile name without extension",
				L"E&XT_PART\tFile extension (with .)",
				L"CURRENT_&WORD\tSelected word or word under caret",
				L"NPP_&DIRECTORY\tnotepadlx directory", // NPLX: was notepad++.exe
				L"NPP_F&ULL_FILE_PATH\tFull path of notepadlx",
				L"CURRENT_&LINE\tLine number of caret",
				L"CURRENT_&COLUMN\tColumn number of caret",
				L"CURRENT_LINE&STR\tCurrent line text",
			};

			QMenu hVariablePopup(this);
			for (int i = 0; i < static_cast<int>(std::size(variablesMenu)); ++i)
			{
				const QStringList parts = QString::fromWCharArray(variablesMenu[i]).split(QLatin1Char('\t'));
				QAction* action = hVariablePopup.addAction(parts.at(0) + QStringLiteral("    ") + parts.value(1));
				action->setData(IDM_RUN_DLG_VARMENU_START + i);
			}
			QWidget* button = dlgItem(IDC_BUTTON_VARIABLES);
			if (QAction* chosen = hVariablePopup.exec(button->mapToGlobal(QPoint(button->width(), 0))))
				onCommand(chosen->data().toInt(), RcNotifyClicked);

			return;
		}

		default:
			return;
	}
}

void RunDlg::addTextToCombo(const wchar_t *txt2Add) const
{
	QComboBox* handle = comboBox(IDC_COMBO_RUN_PATH);
	const QString text = QString::fromWCharArray(txt2Add);
	int i = handle->findText(text, Qt::MatchExactly);
	if (i == -1)
	{
		// CBS_SORT
		handle->addItem(text);
		handle->model()->sort(0);
		i = handle->findText(text, Qt::MatchExactly);
	}
	handle->setCurrentIndex(i);
}
void RunDlg::removeTextFromCombo(const wchar_t *txt2Remove) const
{
	QComboBox* handle = comboBox(IDC_COMBO_RUN_PATH);
	const int i = handle->findText(QString::fromWCharArray(txt2Remove), Qt::MatchExactly);
	if (i == -1)
		return;
	handle->removeItem(i);
}

void RunDlg::doDialog()
{
	if (!isCreated())
	{
		buildFrom(IDD_RUN_DLG_def);
		nplx::changeDlgLang(*this, "Run");
	}

	show();
	raise();
	activateWindow();
	dlgItem(IDC_COMBO_RUN_PATH)->setFocus();
}
