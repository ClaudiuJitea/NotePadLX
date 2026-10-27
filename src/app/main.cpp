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

// NPLX: start-up, port of upstream winmain.cpp (wWinMain) and Notepad_plus_Window::init.

#include <cstdio>

#include <QApplication>
#include <QFontDatabase>
#include <QIcon>

#include "CmdLine.h"
#include "AboutDlg.h"
#include "NplxStyle.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "SingleInstance.h"
#include "localization.h"

int main(int argc, char* argv[])
{
	// Parse before QApplication: upstream options such as -n12 or -lpython are not Qt options
	const NppCommandLine cmdLine = parseNppCommandLine(argc, argv);
	if (cmdLine._showVersion)
	{
		std::printf("NotePadLX %s\n", NPLX_VERSION);
		return 0;
	}
	if (cmdLine._showHelp)
	{
		std::printf("%s", commandLineHelpText());
		return 0;
	}

	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("NotePadLX"));
	QApplication::setApplicationVersion(QStringLiteral(NPLX_VERSION));
	QApplication::setDesktopFileName(QStringLiteral("notepadlx"));
	QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("notepadlx"), QIcon(QStringLiteral(":/icons/notepadlx-256.png"))));

	QtUiServices uiServices;
	nplx::setUiServices(&uiServices);

	// upstream wWinMain: command line parameters, then NppParameters::load()
	NppParameters& nppParameters = NppParameters::getInstance();
	applyCommandLineBeforeLoad(cmdLine);
	nppParameters.load();
	nplx::style::initDarkMode(); // upstream NppDarkMode::initDarkMode()

	bool isMultiInst = cmdLine._isMultiInst;
	CmdLineParams cmdLineParams = cmdLine._params;

	// export functionlist feature will serialize functionlist on the disk, then exit Notepad++. So it's important to
	// not launch into existing instance, and keep it silent.
	if (cmdLine._doFunctionListExport)
	{
		isMultiInst = true;
		cmdLineParams._isNoSession = true;
	}
	nppParameters.setFunctionListExportBoolean(cmdLine._doFunctionListExport);

	// override the settings if notepad style is present
	if (nppParameters.asNotepadStyle())
	{
		isMultiInst = true;
		cmdLineParams._isNoTab = true;
		cmdLineParams._isNoSession = true;
	}

	// override the settings if multiInst is chosen by user in the preference dialog
	const NppGUI& nppGUI = nppParameters.getNppGUI();
	SingleInstance singleInstance(SingleInstance::defaultKey(nppParameters.getUserPath()));
	const CmdLineParamsDTO dto = CmdLineParamsDTO::FromCmdLineParams(cmdLineParams);
	if (nppGUI._multiInstSetting == multiInst)
	{
		isMultiInst = true;
	}

	// upstream: FindWindow + WM_COPYDATA to the running instance
	if (!isMultiInst && singleInstance.sendToRunningInstance(cmdLine._files, dto))
		return 0;

	NppCommandLine effectiveCmdLine = cmdLine;
	effectiveCmdLine._params = cmdLineParams;
	applyCommandLineAfterLoad(effectiveCmdLine);

	// ? > Command Line Arguments..., Debug Info
	nplx::setCommandLineHelpText(commandLineHelpText());
	{
		std::wstring cmdLineString;
		for (int i = 0; i < argc; ++i)
		{
			if (i)
				cmdLineString += L' ';
			cmdLineString += QString::fromLocal8Bit(argv[i]).toStdWString();
		}
		nplx::setCommandLine(cmdLineString);
	}

	// NPLX: the NotePadLX look (style, palette, stylesheet, icons), after load() so upstream dark mode is known
	nplx::style::apply(app);

	// upstream Notepad_plus constructor: the native language speaker, from nativeLang.xml
	NativeLangSpeaker nativeLangSpeaker;
	nativeLangSpeaker.init(nppParameters.getNativeLang());
	nppParameters.setNativeLangSpeaker(&nativeLangSpeaker);
	if (nativeLangSpeaker.isRTL())
		QApplication::setLayoutDirection(Qt::RightToLeft);

	// upstream Notepad_plus::init: nppParam.setFontList(hwnd)
	std::vector<std::wstring> fontNames;
	for (const QString& family : QFontDatabase::families())
		fontNames.push_back(family.toStdWString());
	nppParameters.setFontList(fontNames);

	Notepad_plus notepad_plus;
	notepad_plus.setPluginsDisabled(cmdLineParams._isNoPlugin); // upstream _pluginsManager.disable()
	notepad_plus.init();
	notepad_plus.notifyCmdLinePluginMessage(cmdLineParams._pluginMessage); // upstream Notepad_plus_Window::init

	// upstream Notepad_plus::init: restore the last session
	if (nppGUI._rememberLastSession && !cmdLineParams._isNoSession)
		notepad_plus.loadLastSession();

	// upstream Notepad_plus_Window::init: loadCommandlineParams
	const std::vector<std::wstring> fns = notepad_plus.loadCommandlineParams(cmdLine._files, &dto);

	// upstream Notepad_plus::init: the panels open at the last exit
	if (!nppParameters.doFunctionListExport())
	{
		notepad_plus.loadDockingParams();
		notepad_plus.applyUserDefineDlgStatus();
	}

	// Launch folder as workspace after all this dockable panel being restored from the last session
	// To avoid dockable panel toggle problem.
	if (cmdLineParams._openFoldersAsWorkspace)
	{
		std::wstring emptyStr;
		notepad_plus.launchFileBrowser(fns, emptyStr, true);
	}

	if (cmdLineParams._alwaysOnTop)
		notepad_plus.setWindowFlag(Qt::WindowStaysOnTopHint, true); // NPLX: honoured on X11; Wayland compositors decide
	if (cmdLineParams.isPointValid())
		notepad_plus.move(cmdLineParams._point.x, cmdLineParams._point.y);

	// upstream: NPPM_INTERNAL_EXPORTFUNCLISTANDQUIT, with the window hidden
	if (nppParameters.doFunctionListExport())
	{
		const bool ok = notepad_plus.exportFunctionList();
		nplx::setUiServices(nullptr);
		return ok ? 0 : 1;
	}

	if (nppGUI._isMaximized)
		notepad_plus.showMaximized();
	else
		notepad_plus.show();

	// To notify plugins that all the procedures of launchment of notepad++ are done.
	notepad_plus.notifyReady();

	// Later launches send their files here (upstream: WM_COPYDATA COPYDATA_FILENAMESW)
	if (!isMultiInst || nppGUI._multiInstSetting != multiInst)
	{
		singleInstance.listen([&notepad_plus](const std::vector<std::wstring>& files, const CmdLineParamsDTO& params) {
			notepad_plus.notifyCmdLinePluginMessage(params._pluginMessage); // upstream WM_COPYDATA COPYDATA_PARAMS
			notepad_plus.loadCommandlineParams(files, &params);
			// upstream NPPM_INTERNAL_RESTOREFROMMINIMIZED: out of the tray, or restored
			if (notepad_plus.isInTray())
				notepad_plus.restoreFromTray();
			else if (notepad_plus.isMinimized())
				notepad_plus.showNormal();
			notepad_plus.raise();
			notepad_plus.activateWindow();
		});
	}

	const int ret = app.exec();
	nplx::setUiServices(nullptr);
	return ret;
}
