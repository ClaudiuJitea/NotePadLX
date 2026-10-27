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

// NPLX: port of upstream WinControls/AboutDlg: the About box (IDD_ABOUTBOX), Debug Info (IDD_DEBUGINFOBOX) and
// Command Line Arguments (IDD_COMMANDLINEARGSBOX). The About box uses a Qt layout: upstream's is a borderless popup
// with an owner-drawn logo over the text. Debug Info reports the Linux system (distribution, kernel, Qt, Wayland or
// X11, desktop, screens) in place of the Windows registry and display adapters.
// UpdateChecker replaces the WinGUp updater: it asks GitHub for the latest NotePadLX release.

#pragma once

#include <functional>
#include <string>

#include <QDialog>
#include <QObject>

class QNetworkAccessManager;
class QPlainTextEdit;

#define LICENCE_TXT \
L"This program is free software; you can redistribute it and/or \
modify it under the terms of the GNU General Public License \
as published by the Free Software Foundation; either \
version 3 of the License, or at your option any later version.\n\
\n\
This program is distributed in the hope that it will be useful, \
but WITHOUT ANY WARRANTY; without even the implied warranty of \
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the \
GNU General Public License for more details. \n\
\n\
You should have received a copy of the GNU General Public License \
along with this program. If not, see <https://www.gnu.org/licenses/>."

namespace nplx
{
	// the --help text of the application (app/CmdLine), shown by ? > Command Line Arguments...
	void setCommandLineHelpText(const std::string& text);
	const std::string& commandLineHelpText();

	// the command line of this run, for Debug Info (upstream NPPM_GETCURRENTCMDLINE)
	void setCommandLine(const std::wstring& cmdLine);
	const std::wstring& commandLine();
}

class AboutDlg : public QDialog
{
	Q_OBJECT

public:
	// checkForUpdates, debugInfo: the ? menu commands, run from the buttons of the About box
	AboutDlg(std::function<void()> checkForUpdates, std::function<void()> debugInfo, QWidget* parent = nullptr);
	void doDialog();
};

class DebugInfoDlg : public QDialog
{
	Q_OBJECT

public:
	explicit DebugInfoDlg(QWidget* parent = nullptr);
	void init(const std::wstring& loadedPlugins) { _loadedPlugins = loadedPlugins; } // upstream init(..., loadedPlugins)
	void doDialog();
	void refreshDebugInfo();
	std::wstring debugInfo() const { return _debugInfoStr; } // NPLX: tests

private:
	std::wstring _debugInfoStr;
	std::wstring _loadedPlugins;
	QPlainTextEdit* _edit = nullptr;
};

class CmdLineArgsDlg : public QDialog
{
	Q_OBJECT

public:
	explicit CmdLineArgsDlg(QWidget* parent = nullptr);
	void doDialog();
};

// NPLX: replaces the WinGUp updater (? > Update Notepad++). The releases URL can be overridden for tests with
// NPLX_UPDATE_URL (any URL QNetworkAccessManager reads, file:// included).
class UpdateChecker : public QObject
{
	Q_OBJECT

public:
	enum class Result { UpToDate, NewVersion, Error };

	explicit UpdateChecker(QObject* parent = nullptr);

	// GET <releases>/latest; 'done' gets the latest version, its page and an error text
	void check(std::function<void(Result, const QString& latestVersion, const QString& pageUrl, const QString& error)> done);

	// "v0.10" > "0.2": numeric comparison of dotted versions, a leading 'v' ignored
	static bool isNewer(const QString& candidate, const QString& current);

private:
	QNetworkAccessManager* _network = nullptr;
};
