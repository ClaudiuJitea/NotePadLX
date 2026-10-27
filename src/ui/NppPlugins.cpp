// This file is part of Notepad++ project
// Copyright (C)2021 Don HO <don.h@free.fr>
// Modified for NotePadLX (Linux port)

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

// NPLX: the plugin host of Notepad_plus (Phase 9).
// - the plugin section of upstream Notepad_plus::init (PluginsManager, the Plugins menu, NPPN_TBMODIFICATION);
// - the NPPM_* messages of upstream NppBigSwitch that the plugins use (pluginApiMessage); the internal ones stay
//   in nppMessage;
// - the plugin entry points: NppData::_nppSend / _sciSend, the NPPM_*_UTF8 variants (Notepad_plus_msgs.h), the
//   docking panels (PluginDockingQt.h), the toolbar icons;
// - notifyPlugins, the NPPN_* notifications.

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>

#include <ILexer.h>
#include <Lexilla.h>

#include "Notepad_plus.h"

#include "AboutDlg.h"
#include "Buffer.h"
#include "DesktopIntegration.h"
#include "DocTabView.h"
#include "DockingPanel.h"
#include "FindReplaceDlg.h"
#include "NppDarkMode.h"
#include "Parameters.h"
#include "PluginDockingQt.h"
#include "PluginsManager.h"
#include "RunDlg.h" // CURRENTWORD_MAXLENGTH
#include "ScintillaEditView.h"
#include "UiServices.h"
#include "Utf8_16.h"
#include "VerticalFileSwitcher.h"
#include "menuCmdID.h"
#include "resource.h"

using namespace std;

namespace
{
	// the one main window (upstream: the HWND the plugins send to)
	Notepad_plus* s_pluginHost = nullptr;

	LRESULT nppSendEntry(NppHandle handle, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (!s_pluginHost)
			return 0;
		if (handle == static_cast<NppHandle>(s_pluginHost))
			return s_pluginHost->pluginMessage(message, wParam, lParam);
		if (ScintillaEditView* view = s_pluginHost->pluginScintilla(handle))
			return view->execute(message, wParam, lParam);
		return 0;
	}

	LRESULT sciSendEntry(NppHandle handle, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (!s_pluginHost)
			return 0;
		if (ScintillaEditView* view = s_pluginHost->pluginScintilla(handle))
			return view->execute(message, wParam, lParam);
		return 0;
	}

	wstring fromUtf8(sptr_t p)
	{
		return p ? string2wstring(reinterpret_cast<const char*>(p), CP_UTF8) : wstring();
	}

	// the "(size_t bufLen, char* buf)" getters of the _UTF8 messages
	intptr_t copyUtf8Out(const string& s, uptr_t bufLen, sptr_t buf)
	{
		if (!buf)
			return static_cast<intptr_t>(s.size());
		if (s.size() >= bufLen)
			return 0;
		std::memcpy(reinterpret_cast<char*>(buf), s.c_str(), s.size() + 1);
		return static_cast<intptr_t>(s.size());
	}

	// the "(id, char* buf)" getters: buf is large enough, as upstream
	intptr_t copyUtf8OutUnchecked(const string& s, sptr_t buf)
	{
		if (buf)
			std::memcpy(reinterpret_cast<char*>(buf), s.c_str(), s.size() + 1);
		return static_cast<intptr_t>(s.size());
	}

	// upstream: lstrcpy into a buffer of the plugin; with lParam == 0 the length only
	intptr_t copyWideOut(const wstring& str, uptr_t bufLen, sptr_t buf, bool returnTrueOnCopy = false)
	{
		if (buf != 0)
		{
			if (str.length() >= static_cast<size_t>(bufLen))
				return 0;
			wcscpy(reinterpret_cast<wchar_t*>(buf), str.c_str());
			if (returnTrueOnCopy)
				return TRUE;
		}
		return static_cast<intptr_t>(str.length());
	}

	// the "(id, wchar_t* buf)" getters: buf is large enough, as upstream
	intptr_t copyWideOutUnchecked(const wstring& str, sptr_t buf)
	{
		if (buf)
			wcscpy(reinterpret_cast<wchar_t*>(buf), str.c_str());
		return static_cast<intptr_t>(str.length());
	}

	Qt::DockWidgetArea areaOfCont(int cont)
	{
		switch (cont)
		{
			case CONT_LEFT:   return Qt::LeftDockWidgetArea;
			case CONT_TOP:    return Qt::TopDockWidgetArea;
			case CONT_BOTTOM: return Qt::BottomDockWidgetArea;
			default:          return Qt::RightDockWidgetArea;
		}
	}
}

PluginsManager* Notepad_plus::pluginsManager() const
{
	return _pluginsManager.get();
}

ScintillaEditView* Notepad_plus::pluginScintilla(NppHandle handle) const
{
	if (!handle)
		return nullptr;
	if (handle == static_cast<NppHandle>(_mainEditView))
		return _mainEditView;
	if (handle == static_cast<NppHandle>(_subEditView))
		return _subEditView;
	for (ScintillaEditView* view : _scintillaCtrls4Plugins)
	{
		if (handle == static_cast<NppHandle>(view))
			return view;
	}
	return nullptr;
}

void Notepad_plus::detachPluginHost()
{
	if (s_pluginHost == this)
		s_pluginHost = nullptr;
}

void Notepad_plus::initPlugins()
{
	// upstream Notepad_plus::init: "Plugin Manager", then "Plugin menu" (after the Macro and Run menus)
	NppParameters& nppParam = NppParameters::getInstance();
	s_pluginHost = this;

	NppData nppData;
	nppData._nppHandle = static_cast<NppHandle>(this);
	nppData._scintillaMainHandle = static_cast<NppHandle>(_mainEditView);
	nppData._scintillaSecondHandle = static_cast<NppHandle>(_subEditView);
	nppData._nppSend = nppSendEntry;
	nppData._sciSend = sciSendEntry;

	_pluginsManager = std::make_unique<PluginsManager>();
	_pluginsManager->init(nppData);
	if (_isNoPlugin)
		_pluginsManager->disable();

	for (const wstring& root : PluginsManager::defaultPluginRoots())
		_pluginsManager->loadPlugins(root.c_str());

	_pluginsManager->initMenu(menuOfCommand(IDM_SETTING_OPENPLUGINSDIR), [this](int cmdID, QAction* action) {
		_actions.emplace(cmdID, action);
		connect(action, &QAction::triggered, this, [this, cmdID, action]() {
			// a Win32 menu item keeps its check mark until the plugin sets it (NPPM_SETMENUITEMCHECK)
			if (action->isCheckable())
				action->setChecked(!action->isChecked());
			command(cmdID);
		});
	});

	// upstream Notepad_plus.cpp: the plugin shortcuts customized in shortcuts.xml, once the plugins are loaded
	nppParam.reloadPluginCmds();
	updateShortcuts();
}

void Notepad_plus::notifyPlugins(const SCNotification* notification)
{
	if (_pluginsManager)
		_pluginsManager->notify(notification);
}

void Notepad_plus::notifyPlugins(unsigned int code, const void* hwndFrom, uptr_t idFrom)
{
	if (!_pluginsManager)
		return;
	SCNotification scnN{};
	scnN.nmhdr.code = code;
	scnN.nmhdr.hwndFrom = const_cast<void*>(hwndFrom);
	scnN.nmhdr.idFrom = idFrom;
	_pluginsManager->notify(&scnN);
}

void Notepad_plus::notifyPluginsFromNpp(unsigned int code, uptr_t idFrom)
{
	notifyPlugins(code, static_cast<NppHandle>(this), idFrom);
}

void Notepad_plus::notifyReady()
{
	// upstream Notepad_plus_Window::init, the end
	notifyPluginsFromNpp(NPPN_READY, 0);
}

void Notepad_plus::notifyCmdLinePluginMessage(const wstring& pluginMessage)
{
	// upstream NPPN_CMDLINEPLUGINMSG; NPLX: idFrom is a UTF-8 char* (upstream: a wchar_t*)
	if (pluginMessage.empty())
		return;
	const string msg = wstring2string(pluginMessage, CP_UTF8);
	notifyPluginsFromNpp(NPPN_CMDLINEPLUGINMSG, reinterpret_cast<uptr_t>(msg.c_str()));
}

std::vector<std::wstring> Notepad_plus::addNppPlugins(const wchar_t *extFilterName, const wchar_t *extFilter)
{
	// NPLX: QFileDialog (through UiServices) in place of CustomFileDialog
	std::wstring filter = extFilterName;
	filter += L" (*";
	filter += extFilter;
	filter += L")";
	const std::vector<std::wstring> fns = nplx::uiServices()
		? nplx::uiServices()->getOpenFileNames(L"Open", filter, L"") : std::vector<std::wstring>();

	vector<wstring> copiedFiles;

	if (!fns.empty())
	{
		// Get plugins dir
		wstring destDirName = (NppParameters::getInstance()).getPluginRootDir();

		if (!doesDirectoryExist(destDirName.c_str()))
		{
			nplx::createDirectories(destDirName); // NPLX: was ::CreateDirectory
		}

		size_t sz = fns.size();
		for (size_t i = 0 ; i < sz ; ++i)
		{
			if (doesFileExist(fns.at(i).c_str()))
			{
				// copy to plugins directory
				wstring destName = destDirName;

				wstring nameExt = std::filesystem::path(fns.at(i)).filename().wstring(); // NPLX: was ::PathFindFileName
				auto pos = nameExt.find_last_of(L".");
				if (pos == wstring::npos)
					continue;

				wstring name = nameExt.substr(0, pos);
				pathAppend(destName, name);
				if (!doesDirectoryExist(destName.c_str()))
				{
					nplx::createDirectories(destName);
				}
				pathAppend(destName, nameExt);

				if (nplx::copyFile(fns.at(i), destName, false)) // NPLX: was ::CopyFile
					copiedFiles.push_back(destName.c_str());
			}
		}
	}
	return copiedFiles;
}

void Notepad_plus::openPluginsFolder()
{
	// upstream IDM_SETTING_OPENPLUGINSDIR: ShellExecute on the plugin home; NPLX: created first, so it can be opened
	const wchar_t* pluginHomePath = NppParameters::getInstance().getPluginRootDir();
	if (pluginHomePath && pluginHomePath[0])
	{
		if (!doesDirectoryExist(pluginHomePath))
			nplx::createDirectories(pluginHomePath);
		nplx::desktop::openFolder(QString::fromWCharArray(pluginHomePath));
	}
}

wstring Notepad_plus::getLoadedPluginNames() const
{
	return _pluginsManager ? _pluginsManager->getLoadedPluginNames() : wstring();
}

// ---- docking panels of the plugins (upstream DockingManager::createDockableDlg, showDockableDlg)

Notepad_plus::PluginPanel* Notepad_plus::findPluginPanel(const QWidget* client)
{
	for (auto& p : _pluginPanels)
	{
		if (p->_client == client)
			return p.get();
	}
	return nullptr;
}

void Notepad_plus::updatePluginPanelTitle(PluginPanel& p)
{
	// upstream DockingCont: "name - additional info" with DWS_ADDINFO
	QString title = p._name;
	if ((p._mask & DWS_ADDINFO) && p._addInfo && p._addInfo[0])
		title += QStringLiteral(" - ") + QString::fromUtf8(p._addInfo);
	p._panel->setWindowTitle(title);
}

bool Notepad_plus::registerPluginPanel(const DockedWidgetData* pData)
{
	if (!pData || !pData->hClient || findPluginPanel(pData->hClient))
		return false;

	auto p = std::make_unique<PluginPanel>();
	p->_client = pData->hClient;
	p->_moduleName = string2wstring(pData->pszModuleName ? pData->pszModuleName : "", CP_UTF8);
	p->_name = QString::fromUtf8(pData->pszName ? pData->pszName : "");
	p->_dlgID = pData->dlgID;
	p->_addInfo = pData->pszAddInfo;
	p->_mask = pData->uMask;

	// upstream getIntegralDockingData: the saved container and visibility, else the default of uMask
	int iCont = (pData->uMask & DWS_DF_FLOATING) ? DOCKCONT_MAX : static_cast<int>((pData->uMask & 0x30000000) >> 28);
	int iPrevCont = pData->iPrevCont;
	bool isVisible = false;
	const DockingManagerData& dockingData = NppParameters::getInstance().getNppGUI()._dockingData;
	for (const PluginDlgDockingInfo& pddi : dockingData._pluginDockInfo)
	{
		if (pddi._name == p->_moduleName && pddi._internalID == p->_dlgID)
		{
			iCont = pddi._currContainer;
			iPrevCont = pddi._prevContainer;
			isVisible = pddi._isVisible;
			break;
		}
	}
	const bool isFloating = iCont >= DOCKCONT_MAX;
	const Qt::DockWidgetArea area = areaOfCont(isFloating ? iPrevCont : iCont);

	const QString objectName = QStringLiteral("plugin:%1:%2").arg(QString::fromStdWString(p->_moduleName)).arg(p->_dlgID);
	p->_panel = new DockingPanel(p->_name, p->_dlgID, objectName, this);
	p->_panel->setWidget(pData->hClient);
	if ((pData->uMask & DWS_ICONTAB) && pData->pszIconTab && pData->pszIconTab[0])
		p->_panel->setWindowIcon(QIcon(QString::fromUtf8(pData->pszIconTab)));
	updatePluginPanelTitle(*p);

	// the panels of a container share it as tabs, like upstream
	QDockWidget* sameArea = nullptr;
	for (QDockWidget* other : findChildren<QDockWidget*>(Qt::FindDirectChildrenOnly))
	{
		if (other->isVisible() && !other->isFloating() && dockWidgetArea(other) == area)
			sameArea = other;
	}
	addDockWidget(area, p->_panel);
	if (sameArea)
		tabifyDockWidget(sameArea, p->_panel);
	p->_panel->setFloating(isFloating);

	// upstream DMN_CLOSE: the plugin unchecks its menu item; NPLX: done here, the widget gets a QHideEvent
	DockingPanel* panel = p->_panel;
	const wstring moduleName = p->_moduleName;
	const int dlgID = p->_dlgID;
	panel->setCloseHandler([this, panel, moduleName, dlgID](int) {
		panel->setClosed(true);
		panel->hide();
		if (_pluginsManager)
		{
			const int cmdID = _pluginsManager->getCmdIdFromFuncIndex(moduleName, dlgID);
			if (cmdID)
				checkMenuItem(cmdID, false);
		}
	});

	// NPLX: registered hidden; the plugin shows it (NPPM_DMMSHOW). The saved visibility is used at start-up, where
	// loadPluginDockingParams runs the plugin command that shows the panel (upstream: createDockableDlg(isVisible))
	(void)isVisible;
	panel->setClosed(true);
	panel->setVisible(false);
	_pluginPanels.push_back(std::move(p));
	return true;
}

void Notepad_plus::savePluginDockingParams(std::vector<PluginDlgDockingInfo>& infos) const
{
	// upstream DockingManager: the plugin panels, by module name and dlgID; those of plugins not loaded this time
	// are kept as they are
	const NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	for (const PluginDlgDockingInfo& pddi : nppGUI._dockingData._pluginDockInfo)
	{
		if (pddi._name == NPP_INTERNAL_FUNCTION_STR)
			continue;
		const bool isLive = std::any_of(_pluginPanels.begin(), _pluginPanels.end(), [&pddi](const auto& p) {
			return p->_moduleName == pddi._name && p->_dlgID == pddi._internalID;
		});
		if (!isLive)
			infos.push_back(pddi);
	}
	for (const auto& p : _pluginPanels)
	{
		const int dockedCont = [this, &p]() {
			switch (dockWidgetArea(p->_panel))
			{
				case Qt::LeftDockWidgetArea:   return CONT_LEFT;
				case Qt::TopDockWidgetArea:    return CONT_TOP;
				case Qt::BottomDockWidgetArea: return CONT_BOTTOM;
				default:                       return CONT_RIGHT;
			}
		}();
		const int curr = p->_panel->isFloating() ? DOCKCONT_MAX : dockedCont;
		const int prev = p->_panel->isFloating() ? dockedCont : -1;
		infos.emplace_back(p->_moduleName.c_str(), p->_dlgID, curr, prev, !p->_panel->isClosed() && p->_panel->isVisible());
	}
}

void Notepad_plus::loadPluginDockingParams()
{
	// upstream Notepad_plus::init: the plugin panels visible at the last exit are opened by their command
	if (!_pluginsManager)
		return;
	const NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	const std::vector<PluginDlgDockingInfo> infos = nppGUI._dockingData._pluginDockInfo;
	for (const PluginDlgDockingInfo& pdi : infos)
	{
		if (pdi._name == NPP_INTERNAL_FUNCTION_STR || !pdi._isVisible)
			continue;
		const bool showPanel = !nppGUI._isCmdlineNosessionActivated || nppGUI._pluginPanelKeepState;
		if (showPanel)
			_pluginsManager->runPluginCommand(pdi._name.c_str(), pdi._internalID);
	}
}

// ---- toolbar icons of the plugins (upstream ToolBar::registerDynBtn / registerDynBtnDM)

void Notepad_plus::addPluginToolbarButtons()
{
	// upstream ToolBar::init: the dynamic buttons after the standard ones, behind a separator
	if (!_toolBar || _pluginToolbarIcons.empty())
		return;
	_toolBar->addSeparator();
	for (const auto& icon : _pluginToolbarIcons)
	{
		if (QAction* action = actionFor(icon->_cmdID))
			_toolBar->addAction(action);
	}
	updatePluginToolbarIcons();
}

void Notepad_plus::updatePluginToolbarIcons()
{
	const bool isDark = NppDarkMode::isEnabled();
	for (const auto& icon : _pluginToolbarIcons)
	{
		QAction* action = actionFor(icon->_cmdID);
		if (!action)
			continue;
		const QString& file = (isDark && !icon->_iconDarkMode.isEmpty()) ? icon->_iconDarkMode : icon->_icon;
		if (!file.isEmpty())
			action->setIcon(QIcon(file));
		action->setIconVisibleInMenu(false); // upstream menus have no icons
	}
}

// ---- the messages of the plugins

intptr_t Notepad_plus::pluginMessage(unsigned int message, uptr_t wParam, sptr_t lParam)
{
	// the _UTF8 variants of NPPMSG + n and of RUNCOMMAND_USER + n (NPPM_GETFULLCURRENTPATH...)
	if (message >= NPPMSG + NPPM_UTF8_OFFSET && message < RUNCOMMAND_USER + NPPM_UTF8_OFFSET + 1000)
		return utf8PluginMessage(message, wParam, lParam);

	switch (message)
	{
		case NPPM_DMMREGASDCKDLG:
		{
			// NPLX: a DockedWidgetData of PluginDockingQt.h (the internal NPPM_DMMREGASDCKDLG of nppMessage takes the
			// QDockWidget of a Finder)
			return registerPluginPanel(reinterpret_cast<const DockedWidgetData*>(lParam));
		}

		case NPPM_DMMSHOW:
		case NPPM_DMMHIDE:
		{
			PluginPanel* p = findPluginPanel(reinterpret_cast<QWidget*>(lParam));
			if (!p)
				return FALSE;
			const bool toShow = message == NPPM_DMMSHOW;
			p->_panel->setClosed(!toShow);
			p->_panel->display(toShow);
			return TRUE;
		}

		case NPPM_DMMUPDATEDISPINFO:
		{
			PluginPanel* p = findPluginPanel(reinterpret_cast<QWidget*>(lParam));
			if (p && p->_panel->isVisible())
				updatePluginPanelTitle(*p);
			return TRUE;
		}

		case NPPM_DMMVIEWOTHERTAB:
		{
			// upstream showDockableDlg(name, SW_SHOW): the panel whose name is lParam
			const QString name = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(lParam));
			for (auto& p : _pluginPanels)
			{
				if (p->_name == name)
				{
					p->_panel->setClosed(false);
					p->_panel->display(true);
				}
			}
			return TRUE;
		}

		case NPPM_DMMGETPLUGINHWNDBYNAME : //(const wchar_t *windowName, const wchar_t *moduleName)
		{
			if (!lParam)
				return static_cast<LRESULT>(NULL);

			const wstring moduleName = reinterpret_cast<const wchar_t*>(lParam);
			const wchar_t* windowName = reinterpret_cast<const wchar_t*>(wParam);
			for (const auto& p : _pluginPanels)
			{
				// NPLX: case-sensitive module names (was _wcsicmp)
				if (p->_moduleName == moduleName)
				{
					if (!windowName)
						return reinterpret_cast<LRESULT>(p->_client);

					if (p->_name == QString::fromWCharArray(windowName))
						return reinterpret_cast<LRESULT>(p->_client);
				}
			}
			return static_cast<LRESULT>(NULL);
		}

		case NPPM_ADDTOOLBARICON_DEPRECATED:
		case NPPM_ADDTOOLBARICON_FORDARKMODE:
		{
			// NPLX: image files; registered during NPPN_TBMODIFICATION, as upstream (later: added at once)
			if (!lParam)
				return FALSE;
			auto icon = std::make_unique<PluginToolbarIcon>();
			icon->_cmdID = static_cast<int>(wParam);
			if (message == NPPM_ADDTOOLBARICON_FORDARKMODE)
			{
				const auto* icons = reinterpret_cast<const toolbarIconsWithDarkMode*>(lParam);
				const char* light = icons->hToolbarBmp ? icons->hToolbarBmp : icons->hToolbarIcon;
				icon->_icon = QString::fromUtf8(light ? light : "");
				icon->_iconDarkMode = QString::fromUtf8(icons->hToolbarIconDarkMode ? icons->hToolbarIconDarkMode : "");
			}
			else
			{
				const auto* icons = reinterpret_cast<const toolbarIcons*>(lParam);
				const char* light = icons->hToolbarBmp ? icons->hToolbarBmp : icons->hToolbarIcon;
				icon->_icon = QString::fromUtf8(light ? light : "");
			}
			const int cmdID = icon->_cmdID;
			_pluginToolbarIcons.push_back(std::move(icon));
			if (_toolBar && actionFor(cmdID))
			{
				if (_pluginToolbarIcons.size() == 1)
					_toolBar->addSeparator();
				_toolBar->addAction(actionFor(cmdID));
				updatePluginToolbarIcons();
			}
			return TRUE;
		}

		case NPPM_CREATESCINTILLAHANDLE:
		{
			// upstream ScintillaCtrls::createSintilla: a hidden ScintillaEditView; NPLX: lParam may be a QWidget* parent
			QWidget* parent = lParam ? reinterpret_cast<QWidget*>(lParam) : static_cast<QWidget*>(this);
			auto* view = new ScintillaEditView(false, parent);
			view->hide();
			view->init();
			_scintillaCtrls4Plugins.push_back(view);
			return reinterpret_cast<LRESULT>(static_cast<NppHandle>(view));
		}

		case NPPM_GETMENUHANDLE:
		{
			// NPLX: a QMenu* (NPPPLUGINMENU) or the QMenuBar* (NPPMAINMENU) in place of the HMENU
			if (wParam == NPPPLUGINMENU)
				return reinterpret_cast<LRESULT>(_pluginsManager ? _pluginsManager->getMenuHandle() : nullptr);
			else if (wParam == NPPMAINMENU)
				return reinterpret_cast<LRESULT>(menuBar());
			else
				return static_cast<LRESULT>(NULL);
		}

		default:
			break;
	}
	return nppMessage(message, wParam, lParam);
}

intptr_t Notepad_plus::utf8PluginMessage(unsigned int message, uptr_t wParam, sptr_t lParam)
{
	const unsigned int base = message - NPPM_UTF8_OFFSET;
	switch (base)
	{
		// getters (size_t bufLen, char* buf)
		case NPPM_GETFULLCURRENTPATH:
		case NPPM_GETCURRENTDIRECTORY:
		case NPPM_GETFILENAME:
		case NPPM_GETNAMEPART:
		case NPPM_GETEXTPART:
		case NPPM_GETCURRENTWORD:
		case NPPM_GETCURRENTLINESTR:
		case NPPM_GETFILENAMEATCURSOR:
		case NPPM_GETNPPDIRECTORY:
		case NPPM_GETNPPFULLFILEPATH:
		case NPPM_GETPLUGINSCONFIGDIR:
		case NPPM_GETPLUGINHOMEPATH:
		case NPPM_GETNPPSETTINGSDIRPATH:
		case NPPM_GETSETTINGSONCLOUDPATH:
		case NPPM_GETCURRENTCMDLINE:
		{
			std::vector<wchar_t> wbuf(std::max<size_t>(CURRENTWORD_MAXLENGTH, 1 << 16), L'\0');
			pluginMessage(base, wbuf.size(), reinterpret_cast<sptr_t>(wbuf.data()));
			wbuf.back() = L'\0';
			return copyUtf8Out(wstring2string(wbuf.data(), CP_UTF8), wParam, lParam);
		}

		// getters (id, char* buf)
		case NPPM_GETFULLPATHFROMBUFFERID:
		{
			BufferID id = reinterpret_cast<BufferID>(wParam);
			if (MainFileManager.getBufferIndexByID(id) == -1)
				return -1;
			return copyUtf8OutUnchecked(wstring2string(id->getFullPathName(), CP_UTF8), lParam);
		}

		case NPPM_GETLANGUAGENAME:
		case NPPM_GETLANGUAGEDESC:
		{
			const wstring langName = getLangDesc(static_cast<LangType>(wParam), base == NPPM_GETLANGUAGENAME);
			return copyUtf8OutUnchecked(wstring2string(langName, CP_UTF8), lParam);
		}

		// setters (lParam: const char*)
		case NPPM_DOOPEN:
		case NPPM_SWITCHTOFILE:
		case NPPM_RELOADFILE:
		case NPPM_SAVECURRENTFILEAS:
		case NPPM_SAVEFILE:
		case NPPM_LOADSESSION:
		case NPPM_SETSTATUSBAR:
		case NPPM_SETUNTITLEDNAME:
		case NPPM_DMMVIEWOTHERTAB:
		{
			if (!lParam)
				return FALSE;
			const wstring str = fromUtf8(lParam);
			return pluginMessage(base, wParam, reinterpret_cast<sptr_t>(str.c_str()));
		}

		case NPPM_CREATELEXER:
		{
			if (!lParam)
				return 0;
			return reinterpret_cast<LRESULT>(CreateLexer(reinterpret_cast<const char*>(lParam)));
		}

		// setters (wParam and lParam: const char*)
		case NPPM_LAUNCHFINDINFILESDLG:
		case NPPM_DMMGETPLUGINHWNDBYNAME:
		{
			const wstring w = fromUtf8(static_cast<sptr_t>(wParam));
			const wstring l = fromUtf8(lParam);
			return pluginMessage(base, wParam ? reinterpret_cast<uptr_t>(w.c_str()) : 0, lParam ? reinterpret_cast<sptr_t>(l.c_str()) : 0);
		}

		case NPPM_MSGTOPLUGIN:
		{
			const wstring dest = fromUtf8(static_cast<sptr_t>(wParam));
			return pluginMessage(base, wParam ? reinterpret_cast<uptr_t>(dest.c_str()) : 0, lParam);
		}

		default:
			return FALSE;
	}
}

bool Notepad_plus::pluginApiMessage(unsigned int message, uptr_t wParam, sptr_t lParam, intptr_t& result)
{
	// upstream NppBigSwitch.cpp: the NPPM_* of the plugin API (in the upstream order)
	NppParameters& nppParam = NppParameters::getInstance();
	switch (message)
	{
		case NPPM_LAUNCHFINDINFILESDLG:
		{
			// Find in files function code should be here due to the number of parameters (2) cannot be passed via WM_COMMAND
			_findReplaceDlg->doDialog(FINDINFILES_DLG, _nativeLangSpeaker.isRTL()); // NPLX: translated when created

			_findReplaceDlg->setSearchTextWithSettings();
			setFindReplaceFolderFilter(reinterpret_cast<const wchar_t*>(wParam), reinterpret_cast<const wchar_t*>(lParam));

			result = TRUE;
			return true;
		}

		case NPPM_GETBUFFERLANGTYPE:
		{
			result = -1;
			if (!wParam)
				return true;

			BufferID id = (BufferID)wParam;
			if (MainFileManager.getBufferIndexByID(id) == -1)
				return true;

			Buffer * b = MainFileManager.getBufferByID(id);
			result = b->getLangType();
			return true;
		}

		case NPPM_SETBUFFERLANGTYPE:
		{
			result = FALSE;
			if (!wParam || lParam < L_TEXT || lParam >= L_EXTERNAL || lParam == L_USER)
				return true;

			BufferID id = (BufferID)wParam;
			if (MainFileManager.getBufferIndexByID(id) == -1)
				return true;

			Buffer* b = MainFileManager.getBufferByID(id);
			b->setLangType((LangType)lParam);
			result = TRUE;
			return true;
		}

		case NPPM_GETBUFFERENCODING:
		{
			result = -1;
			if (!wParam)
				return true;

			BufferID id = (BufferID)wParam;
			if (MainFileManager.getBufferIndexByID(id) == -1)
				return true;

			Buffer* b = MainFileManager.getBufferByID(id);
			result = b->getUnicodeMode();
			return true;
		}

		case NPPM_SETBUFFERENCODING:
		{
			result = FALSE;
			if (!wParam)
				return true;
			if (lParam < uni8Bit || lParam >= uniEnd)
				return true;

			BufferID id = (BufferID)wParam;
			if (MainFileManager.getBufferIndexByID(id) == -1)
				return true;

			Buffer * b = MainFileManager.getBufferByID(id);
			if (b->getStatus() != DOC_UNNAMED || b->isDirty())	//do not allow to change the encoding if the file has any content
				return true;
			b->setUnicodeMode((UniMode)lParam);
			result = TRUE;
			return true;
		}

		case NPPM_GETBUFFERFORMAT:
		{
			result = -1;
			if (!wParam)
				return true;

			BufferID id = (BufferID)wParam;
			if (MainFileManager.getBufferIndexByID(id) == -1)
				return true;

			Buffer * b = MainFileManager.getBufferByID(id);
			result = static_cast<LRESULT>(b->getEolFormat());
			return true;
		}

		case NPPM_SETBUFFERFORMAT:
		{
			result = FALSE;
			if (!wParam)
				return true;

			EolType newFormat = convertIntToFormatType(static_cast<int>(lParam), EolType::unknown);
			if (EolType::unknown == newFormat)
				return true;

			BufferID id = (BufferID)wParam;
			if (MainFileManager.getBufferIndexByID(id) == -1)
				return true;

			Buffer * b = MainFileManager.getBufferByID(id);
			b->setEolFormat(newFormat);
			result = TRUE;
			return true;
		}

		case NPPM_GETBUFFERIDFROMPOS:
		{
			DocTabView* pView = nullptr;
			if (lParam == MAIN_VIEW)
				pView = _mainDocTab;
			else if (lParam == SUB_VIEW)
				pView = _subDocTab;

			result = reinterpret_cast<LRESULT>(BUFFER_INVALID);
			if (pView && (size_t)wParam < pView->nbItem())
				result = reinterpret_cast<LRESULT>(pView->getBufferByIndex(wParam));
			return true;
		}

		case NPPM_GETCURRENTBUFFERID:
		{
			result = reinterpret_cast<LRESULT>(_pEditView->getCurrentBufferID());
			return true;
		}

		case NPPM_RELOADBUFFERID:
		{
			result = FALSE;
			if (!wParam)
				return true;

			BufferID id = (BufferID)wParam;
			if (MainFileManager.getBufferIndexByID(id) == -1)
				return true;

			result = doReload(id, lParam != 0);
			return true;
		}

		case NPPM_RELOADFILE:
		{
			result = FALSE;
			if (!lParam)
				return true;

			// NPLX: no 8.3 short names to expand (upstream GetLongPathName)
			const wchar_t* pFilePath = reinterpret_cast<const wchar_t*>(lParam);
			BufferID id = MainFileManager.getBufferFromName(pFilePath);
			if (id != BUFFER_INVALID)
				result = doReload(id, wParam != 0);

			// For relaying this message to other plugin, as upstream
			if (_pluginsManager)
				_pluginsManager->relayNppMessages(message, wParam, lParam);
			return true;
		}

		case NPPM_SWITCHTOFILE :
		{
			BufferID id = MainFileManager.getBufferFromName(reinterpret_cast<const wchar_t *>(lParam));
			result = (id != BUFFER_INVALID) ? switchToFile(id) : false;
			return true;
		}

		case NPPM_SAVECURRENTFILE:
		{
			result = fileSave();
			return true;
		}

		case NPPM_SAVECURRENTFILEAS:
		{
			BufferID currentBufferID = _pEditView->getCurrentBufferID();
			bool asCopy = wParam == TRUE;
			const wchar_t *filename = reinterpret_cast<const wchar_t *>(lParam);
			result = filename ? doSave(currentBufferID, filename, asCopy) : FALSE;
			return true;
		}

		case NPPM_SAVEALLFILES:
		{
			result = fileSaveAll();
			return true;
		}

		case NPPM_SAVEFILE:
		{
			result = lParam ? fileSaveSpecific(reinterpret_cast<const wchar_t *>(lParam)) : false;
			return true;
		}

		case NPPM_GETCURRENTNATIVELANGENCODING:
		{
			result = _nativeLangSpeaker.getLangEncoding();
			return true;
		}

		case NPPM_DISABLEAUTOUPDATE:
		{
			// NPLX: no auto-updater (WinGUp)
			result = TRUE;
			return true;
		}

		case NPPM_GETSHORTCUTBYCMDID:
		{
			int cmdID = static_cast<int32_t>(wParam); // cmdID
			ShortcutKey *sk = reinterpret_cast<ShortcutKey *>(lParam); // ShortcutKey structure

			result = PluginsManager::getShortcutByCmdID(cmdID, sk);
			return true;
		}

		case NPPM_MENUCOMMAND:
		{
			command(static_cast<int32_t>(lParam));
			result = TRUE;
			return true;
		}

		case NPPM_GETCURRENTSCINTILLA:
		{
			int* id = reinterpret_cast<int *>(lParam);
			result = FALSE;
			if (!id)
				return true;

			if (_pEditView == _mainEditView)
				*id = MAIN_VIEW;
			else if (_pEditView == _subEditView)
				*id = SUB_VIEW;
			else
				*id = -1; // cannot happen
			result = TRUE;
			return true;
		}

		case NPPM_GETCURRENTLANGTYPE:
		{
			result = FALSE;
			if (!lParam)
				return true;

			*(reinterpret_cast<LangType *>(lParam)) = _pEditView->getCurrentBuffer()->getLangType();
			result = TRUE;
			return true;
		}

		case NPPM_SETCURRENTLANGTYPE:
		{
			_pEditView->getCurrentBuffer()->setLangType(static_cast<LangType>(lParam));
			result = TRUE;
			return true;
		}

		case NPPM_GETNBOPENFILES:
		{
			size_t nbDocPrimary = _mainDocTab->nbItem();
			size_t nbDocSecond = _subDocTab->nbItem();
			if (lParam == ALL_OPEN_FILES)
				result = nbDocPrimary + nbDocSecond;
			else if (lParam == PRIMARY_VIEW)
				result = nbDocPrimary;
			else if (lParam == SECOND_VIEW)
				result = nbDocSecond;
			else
				result = 0;
			return true;
		}

		case NPPM_GETOPENFILENAMESPRIMARY_DEPRECATED:
		case NPPM_GETOPENFILENAMESSECOND_DEPRECATED:
		case NPPM_GETOPENFILENAMES_DEPRECATED:
		{
			result = 0;
			if (!wParam)
				return true;

			wchar_t** fileNames = reinterpret_cast<wchar_t**>(wParam);
			size_t nbFileNames = static_cast<size_t>(lParam);

			size_t j = 0;
			if (message != NPPM_GETOPENFILENAMESSECOND_DEPRECATED)
			{
				for (size_t i = 0; i < _mainDocTab->nbItem() && j < nbFileNames; ++i)
				{
					BufferID id = _mainDocTab->getBufferByIndex(i);
					Buffer * buf = MainFileManager.getBufferByID(id);
					wcscpy(fileNames[j++], buf->getFullPathName());
				}
			}

			if (message != NPPM_GETOPENFILENAMESPRIMARY_DEPRECATED)
			{
				for (size_t i = 0; i < _subDocTab->nbItem() && j < nbFileNames; ++i)
				{
					BufferID id = _subDocTab->getBufferByIndex(i);
					Buffer * buf = MainFileManager.getBufferByID(id);
					wcscpy(fileNames[j++], buf->getFullPathName());
				}
			}
			result = j;
			return true;
		}

		case NPPM_GETNBSESSIONFILES:
		{
			size_t nbSessionFiles = 0;
			const wchar_t* sessionFileName = reinterpret_cast<const wchar_t*>(lParam);
			BOOL* pbIsValidXML = reinterpret_cast<BOOL*>(wParam);
			if (pbIsValidXML)
				*pbIsValidXML = false;
			if (sessionFileName && (sessionFileName[0] != '\0'))
			{
				Session session2Load;
				if (nppParam.loadSession(session2Load, sessionFileName, true))
				{
					if (pbIsValidXML)
						*pbIsValidXML = true;
					nbSessionFiles = session2Load.nbMainFiles() + session2Load.nbSubFiles();
				}
			}
			result = nbSessionFiles;
			return true;
		}

		case NPPM_GETSESSIONFILES:
		{
			const wchar_t *sessionFileName = reinterpret_cast<const wchar_t *>(lParam);
			wchar_t **sessionFileArray = reinterpret_cast<wchar_t **>(wParam);

			result = FALSE;
			if ((!sessionFileName) || (sessionFileName[0] == '\0') || !sessionFileArray)
				return true;

			Session session2Load;
			if (nppParam.loadSession(session2Load, sessionFileName, true))
			{
				size_t i = 0;
				for ( ; i < session2Load.nbMainFiles() ; )
				{
					const wchar_t *pFn = session2Load._mainViewFiles[i]._fileName.c_str();
					wcscpy(sessionFileArray[i++], pFn);
				}

				for (size_t j = 0, len = session2Load.nbSubFiles(); j < len ; ++j)
				{
					const wchar_t *pFn = session2Load._subViewFiles[j]._fileName.c_str();
					wcscpy(sessionFileArray[i++], pFn);
				}
				result = TRUE;
			}
			return true;
		}

		case NPPM_DECODESCI:
		{
			// convert to ASCII
			ScintillaEditView *pSci = nullptr;
			if (wParam == MAIN_VIEW)
				pSci = _mainEditView;
			else if (wParam == SUB_VIEW)
				pSci = _subEditView;
			else
			{
				result = -1;
				return true;
			}

			// get text of current scintilla
			auto length = pSci->execute(SCI_GETTEXTLENGTH, 0, 0) + 1;
			std::vector<char> buffer(length); // NPLX: was new char[]
			pSci->execute(SCI_GETTEXT, length, reinterpret_cast<LPARAM>(buffer.data()));

			// convert here
			UniMode unicodeMode = pSci->getCurrentBuffer()->getUnicodeMode();
			Utf8_16_Write UnicodeConvertor;
			UnicodeConvertor.setEncoding(unicodeMode);
			length = UnicodeConvertor.convert(buffer.data(), length-1);

			// set text in target
			pSci->execute(SCI_CLEARALL);
			pSci->addText(length, UnicodeConvertor.getNewBuf());
			pSci->execute(SCI_EMPTYUNDOBUFFER);

			pSci->execute(SCI_SETCODEPAGE);

			// set cursor position
			pSci->execute(SCI_GOTOPOS);

			result = unicodeMode;
			return true;
		}

		case NPPM_ENCODESCI:
		{
			// convert
			ScintillaEditView *pSci = nullptr;
			if (wParam == MAIN_VIEW)
				pSci = _mainEditView;
			else if (wParam == SUB_VIEW)
				pSci = _subEditView;
			else
			{
				result = -1;
				return true;
			}

			// get text of current scintilla
			auto length = pSci->execute(SCI_GETTEXTLENGTH, 0, 0) + 1;
			std::vector<char> buffer(length);
			pSci->execute(SCI_GETTEXT, length, reinterpret_cast<LPARAM>(buffer.data()));

			Utf8_16_Read UnicodeConvertor;
			length = UnicodeConvertor.convert(buffer.data(), length-1);

			// set text in target
			pSci->execute(SCI_CLEARALL);
			pSci->addText(length, UnicodeConvertor.getNewBuf());

			pSci->execute(SCI_EMPTYUNDOBUFFER);

			// set cursor position
			pSci->execute(SCI_GOTOPOS);

			// set new encoding if BOM was changed by other programs
			UniMode um = UnicodeConvertor.getEncoding();
			(pSci->getCurrentBuffer())->setUnicodeMode(um);
			(pSci->getCurrentBuffer())->setDirty(true);
			result = um;
			return true;
		}

		case NPPM_ACTIVATEDOC:
		case NPPM_TRIGGERTABBARCONTEXTMENU:
		{
			// similar to NPPM_ACTIVEDOC
			int whichView = ((wParam != MAIN_VIEW) && (wParam != SUB_VIEW)) ? currentView() : static_cast<int32_t>(wParam);
			int index = static_cast<int32_t>(lParam);

			switchEditViewTo(whichView);
			activateDoc(index);

			if (message == NPPM_TRIGGERTABBARCONTEXTMENU)
			{
				// open here tab menu (NPLX: upstream WM_NOTIFY NM_RCLICK), at the tab
				DocTabView* tab = getDocTab(whichView);
				const QRect r = tab->tabRect(tab->currentIndex());
				showTabContextMenu(whichView, tab->mapToGlobal(r.center()));
			}
			result = TRUE;
			return true;
		}

		case NPPM_GETNPPVERSION:
		{
			// NPLX: the Notepad++ version whose plugin API NotePadLX follows (upstream VERSION_INTERNAL_VALUE), so
			// that the version checks of ported plugins work; the NotePadLX version is in About
			const wchar_t* verStr = VERSION_INTERNAL_VALUE;
			wchar_t mainVerStr[16]{};
			wchar_t auxVerStr[16]{};
			bool isDot = false;
			int j = 0;
			int k = 0;
			for (int i = 0; verStr[i]; ++i)
			{
				if (verStr[i] == '.')
				{
					isDot = true;
				}
				else
				{
					if (!isDot)
						mainVerStr[j++] = verStr[i];
					else
						auxVerStr[k++] = verStr[i];
				}
			}

			mainVerStr[j] = '\0';
			auxVerStr[k] = '\0';

			// if auxVerStr length should less or equal to 3.
			// if auxVer is less 3 digits, the padding (0) will be added.
			bool addZeroPadding = wParam == TRUE;
			if (addZeroPadding)
			{
				size_t nbDigit = wcslen(auxVerStr);
				if (nbDigit > 0 && nbDigit <= 3)
				{
					if (nbDigit == 3)
					{
						// OK, nothing to do.
					}
					else if (nbDigit == 2)
					{
						auxVerStr[2] = '0';
						auxVerStr[3] = '\0';
					}
					else // if (nbDigit == 1)
					{
						auxVerStr[1] = '0';
						auxVerStr[2] = '0';
						auxVerStr[3] = '\0';
					}
				}
			}

			int mainVer = 0, auxVer = 0;
			if (mainVerStr[0])
				mainVer = _wtoi(mainVerStr);

			if (auxVerStr[0])
				auxVer = _wtoi(auxVerStr);

			result = MAKELONG(auxVer, mainVer);
			return true;
		}

		case NPPM_GETCURRENTCMDLINE:
		{
			result = copyWideOut(nplx::commandLine(), wParam, lParam);
			return true;
		}

		case NPPM_CREATELEXER:
		{
			// NPLX: the name is converted to UTF-8 (upstream: CP_ACP)
			const string lexer_name = wstring2string(reinterpret_cast<const wchar_t*>(lParam), CP_UTF8);
			result = reinterpret_cast<LRESULT>(CreateLexer(lexer_name.c_str()));
			return true;
		}

		case NPPM_DESTROYSCINTILLAHANDLE_DEPRECATED:
		{
			// Destroying allocated Scintilla makes Notepad++ crash
			// because created Scintilla view's pointer is added into _referees of Buffer object automatically.
			// The deallocated scintilla view in _referees is used in Buffer::nextUntitledNewNumber().

			// So we do nothing here and let Notepad++ destroy allocated Scintilla while it exits
			// and we keep this message for the sake of compatibility with the existing plugins.
			result = true;
			return true;
		}

		case NPPM_GETNBUSERLANG:
		{
			if (lParam)
				*(reinterpret_cast<int *>(lParam)) = IDM_LANG_USER;
			result = nppParam.getNbUserLang();
			return true;
		}

		case NPPM_GETCURRENTDOCINDEX:
		{
			if (lParam == SUB_VIEW)
				result = viewVisible(SUB_VIEW) ? _subDocTab->getCurrentTabIndex() : -1;
			else //MAIN_VIEW
				result = viewVisible(MAIN_VIEW) ? _mainDocTab->getCurrentTabIndex() : -1;
			return true;
		}

		case NPPM_SETSTATUSBAR:
		{
			wchar_t *str2set = reinterpret_cast<wchar_t *>(lParam);
			result = FALSE;
			if (!str2set || !str2set[0])
				return true;

			switch (wParam)
			{
				case STATUSBAR_DOC_TYPE:
				case STATUSBAR_DOC_SIZE:
				case STATUSBAR_CUR_POS:
				case STATUSBAR_EOF_FORMAT:
				case STATUSBAR_UNICODE_TYPE:
				case STATUSBAR_TYPING_MODE:
					_statusBarParts[wParam]->setText(QString::fromWCharArray(str2set));
					result = TRUE;
					return true;
				default :
					return true;
			}
		}

		case NPPM_LOADSESSION:
		{
			fileLoadSession(reinterpret_cast<const wchar_t *>(lParam));
			result = TRUE;
			return true;
		}

		case NPPM_SAVECURRENTSESSION:
		{
			result = (LRESULT)fileSaveSession(0, NULL, reinterpret_cast<const wchar_t *>(lParam));
			return true;
		}

		case NPPM_SAVESESSION:
		{
			result = FALSE;
			if (!lParam)
				return true;
			sessionInfo *pSi = reinterpret_cast<sessionInfo *>(lParam);
			result = (LRESULT)fileSaveSession(pSi->nbFile, pSi->files, pSi->sessionFilePathName);
			return true;
		}

		case NPPM_MODELESSDIALOG:
		{
			// NPLX: Qt dispatches the keys of modeless dialogs itself (upstream IsDialogMessage); the list is kept for
			// the return values
			result = TRUE;
			if (wParam == MODELESSDIALOGADD)
			{
				if (std::find(_hModelessDlgs.begin(), _hModelessDlgs.end(), lParam) != _hModelessDlgs.end())
					result = static_cast<LRESULT>(NULL);
				else
				{
					_hModelessDlgs.push_back(lParam);
					result = lParam;
				}
			}
			else if (wParam == MODELESSDIALOGREMOVE)
			{
				auto it = std::find(_hModelessDlgs.begin(), _hModelessDlgs.end(), lParam);
				if (it != _hModelessDlgs.end())
				{
					_hModelessDlgs.erase(it);
					result = static_cast<LRESULT>(NULL);
				}
				else
					result = lParam;
			}
			return true;
		}

		case NPPM_GETPOSFROMBUFFERID:
		{
			int i;
			result = -1;

			if (lParam == SUB_VIEW) // priorityView is sub view, so we search in sub view firstly
			{
				if ((i = _subDocTab->getIndexByBuffer((BufferID)wParam)) != -1)
				{
					long view = SUB_VIEW;
					view <<= 30;
					result = view|i;
				}
				else if ((i = _mainDocTab->getIndexByBuffer((BufferID)wParam)) != -1)
				{
					long view = MAIN_VIEW;
					view <<= 30;
					result = view|i;
				}
			}
			else // (lParam == SUB_VIEW): priorityView is main view, so we search in main view firstly
			{
				if ((i = _mainDocTab->getIndexByBuffer((BufferID)wParam)) != -1)
				{
					long view = MAIN_VIEW;
					view <<= 30;
					result = view|i;
				}
				else if ((i = _subDocTab->getIndexByBuffer((BufferID)wParam)) != -1)
				{
					long view = SUB_VIEW;
					view <<= 30;
					result = view|i;
				}
			}
			return true;
		}

		case NPPM_GETFULLPATHFROMBUFFERID:
		{
			BufferID id = (BufferID)wParam;
			// NPLX: -1 for an unknown buffer, as the message documentation says (upstream returns FALSE)
			if (MainFileManager.getBufferIndexByID(id) == -1)
				result = -1;
			else
				result = copyWideOutUnchecked(id->getFullPathName(), lParam);
			return true;
		}

		case NPPM_GETTOOLBARICONSETCHOICE:
		{
			result = nppParam.getNppGUI()._tbIconInfo._tbIconSet;
			return true;
		}

		case NPPM_SETMENUITEMCHECK:
		{
			// NPLX: the menu item and its toolbar button are one QAction
			checkMenuItem(static_cast<int>(wParam), lParam != 0);
			result = TRUE;
			return true;
		}

		case NPPM_GETWINDOWSVERSION:
		{
			result = WV_UNKNOWN; // NPLX: not Windows
			return true;
		}

		case NPPM_MAKECURRENTBUFFERDIRTY:
		{
			_pEditView->getCurrentBuffer()->setDirty(true);
			result = TRUE;
			return true;
		}

		case NPPM_GETENABLETHEMETEXTUREFUNC_DEPRECATED:
		{
			result = 0; // NPLX: no EnableThemeDialogTexture
			return true;
		}

		case NPPM_GETPLUGINSCONFIGDIR:
		{
			// For the retro-compatibility, TRUE when copied
			result = copyWideOut(nppParam.getUserPluginConfDir(), wParam, lParam, true);
			return true;
		}

		case NPPM_GETPLUGINHOMEPATH:
		{
			result = copyWideOut(nppParam.getPluginRootDir(), wParam, lParam);
			return true;
		}

		case NPPM_GETSETTINGSONCLOUDPATH:
		{
			result = copyWideOut(nppParam.getNppGUI()._cloudPath, wParam, lParam);
			return true;
		}

		case NPPM_GETNPPSETTINGSDIRPATH:
		{
			// get the path (-settingsDir, Cloud directory, AppData or NppDir selection handled by _userPath, returned by .getUserPath())
			result = copyWideOut(nppParam.getUserPath(), wParam, lParam);
			return true;
		}

		case NPPM_SETLINENUMBERWIDTHMODE:
		{
			result = FALSE;
			if (lParam != LINENUMWIDTH_DYNAMIC && lParam != LINENUMWIDTH_CONSTANT)
				return true;

			ScintillaViewParams &svp = const_cast<ScintillaViewParams &>(nppParam.getSVP());
			svp._lineNumberMarginDynamicWidth = lParam == LINENUMWIDTH_DYNAMIC;
			nppMessage(NPPM_INTERNAL_LINENUMBER, 0, 0);

			result = TRUE;
			return true;
		}

		case NPPM_GETLINENUMBERWIDTHMODE:
		{
			const ScintillaViewParams &svp = nppParam.getSVP();
			result = svp._lineNumberMarginDynamicWidth ? LINENUMWIDTH_DYNAMIC : LINENUMWIDTH_CONSTANT;
			return true;
		}

		case NPPM_MSGTOPLUGIN :
		{
			result = _pluginsManager ? _pluginsManager->relayPluginMessages(message, wParam, lParam) : false;
			return true;
		}

		case NPPM_ALLOCATESUPPORTED_DEPRECATED:
		{
			result = TRUE;
			return true;
		}

		case NPPM_ALLOCATECMDID:
		{
			result = (lParam && _pluginsManager) ? _pluginsManager->allocateCmdID(static_cast<int32_t>(wParam), reinterpret_cast<int *>(lParam)) : FALSE;
			return true;
		}

		case NPPM_ALLOCATEMARKER:
		{
			result = (lParam && _pluginsManager) ? _pluginsManager->allocateMarker(static_cast<int32_t>(wParam), reinterpret_cast<int *>(lParam)) : FALSE;
			return true;
		}

		case NPPM_ALLOCATEINDICATOR:
		{
			result = (lParam && _pluginsManager) ? _pluginsManager->allocateIndicator(static_cast<int32_t>(wParam), reinterpret_cast<int *>(lParam)) : FALSE;
			return true;
		}

		case NPPM_GETTABCOLORID:
		{
			const auto view = static_cast<int>(wParam);
			auto tabIndex = static_cast<int>(lParam);

			auto colorId = -1;  // no color (or unknown)

			auto pDt = _pDocTab;  // active view
			if (view == MAIN_VIEW)
			{
				pDt = _mainDocTab;
			}
			else if (view == SUB_VIEW)
			{
				pDt = _subDocTab;
			}

			if (tabIndex == -1)
			{
				tabIndex = pDt->getCurrentTabIndex();
			}

			if ((tabIndex >= 0) && (tabIndex < static_cast<int>(pDt->nbItem())))
			{
				colorId = pDt->getIndividualTabColourId(tabIndex);
			}

			result = colorId;
			return true;
		}

		case NPPM_SETUNTITLEDNAME:
		{
			result = fileRenameUntitledPluginAPI(reinterpret_cast<BufferID>(wParam), reinterpret_cast<const wchar_t*>(lParam));
			return true;
		}

		case NPPM_GETBOOKMARKID:
		{
			result = MARK_BOOKMARK;
			return true;
		}

		case NPPM_HIDETABBAR:
		{
			bool hide = (lParam != 0);

			NppGUI& nppGUI = nppParam.getNppGUI();
			bool oldVal = (nppGUI._tabStatus & TAB_HIDE);
			result = oldVal;
			if (hide == oldVal)
				return true;

			if (hide)
				nppGUI._tabStatus |= TAB_HIDE;
			else
				nppGUI._tabStatus &= ~TAB_HIDE;

			// NPLX: upstream WM_SIZE re-lays the tab bars out
			_mainDocTab->applyTabStatus();
			_subDocTab->applyTabStatus();
			return true;
		}

		case NPPM_ISTABBARHIDDEN:
		{
			result = (nppParam.getNppGUI()._tabStatus & TAB_HIDE) != 0;
			return true;
		}

		case NPPM_ISTOOLBARHIDDEN :
		{
			result = !(_toolBar && _toolBar->isVisible());
			return true;
		}

		case NPPM_ISMENUHIDDEN:
		{
			result = !menuBar()->isVisible();
			return true;
		}

		case NPPM_ISSTATUSBARHIDDEN:
		{
			result = !nppParam.getNppGUI()._statusBarShow;
			return true;
		}

		case NPPM_GETCURRENTVIEW:
		{
			result = _activeView;
			return true;
		}

		case NPPM_GETLANGUAGENAME:
		case NPPM_GETLANGUAGEDESC:
		{
			wstring langName = getLangDesc((LangType)wParam, message == NPPM_GETLANGUAGENAME);
			result = copyWideOutUnchecked(langName, lParam);
			return true;
		}

		case NPPM_GETEXTERNALLEXERAUTOINDENTMODE:
		{
			result = FALSE;
			int index = nppParam.getExternalLangIndexFromName(reinterpret_cast<wchar_t*>(wParam));
			if (index < 0 || !lParam)
				return true;

			*(reinterpret_cast<ExternalLexerAutoIndentMode*>(lParam)) = nppParam.getELCFromIndex(index)->_autoIndentMode;
			result = TRUE;
			return true;
		}

		case NPPM_SETEXTERNALLEXERAUTOINDENTMODE:
		{
			result = FALSE;
			int index = nppParam.getExternalLangIndexFromName(reinterpret_cast<wchar_t*>(wParam));
			if (index < 0)
				return true;

			nppParam.getELCFromIndex(index)->_autoIndentMode = static_cast<ExternalLexerAutoIndentMode>(lParam);
			result = TRUE;
			return true;
		}

		case NPPM_ISAUTOINDENTON:
		{
			result = nppParam.getNppGUI()._maintainIndent;
			return true;
		}

		case NPPM_ISDARKMODEENABLED:
		{
			result = NppDarkMode::isEnabled();
			return true;
		}

		case NPPM_GETDARKMODECOLORS:
		{
			result = static_cast<LRESULT>(false);
			if (static_cast<size_t>(wParam) != sizeof(NppDarkMode::Colors))
				return true;

			NppDarkMode::Colors* currentColors = reinterpret_cast<NppDarkMode::Colors*>(lParam);

			if (currentColors != NULL)
			{
				// NPLX: the colours of the selected tone (upstream reads them from the dark mode module)
				const auto& darkmode = nppParam.getNppGUI()._darkmode;
				*currentColors = darkmode._colorTone == NppDarkMode::ColorTone::customizedTone
					? darkmode._customColors : NppDarkMode::getDarkModeDefaultColors(darkmode._colorTone);
				result = static_cast<LRESULT>(true);
			}
			return true;
		}

		case NPPM_DARKMODESUBCLASSANDTHEME:
		{
			// NPLX: the Qt palette and stylesheet already theme the plugin widgets
			result = 0;
			return true;
		}

		case NPPM_DOCLISTDISABLEPATHCOLUMN:
		case NPPM_DOCLISTDISABLEEXTCOLUMN:
		{
			BOOL isOff = static_cast<BOOL>(lParam);
			NppGUI & nppGUI = nppParam.getNppGUI();

			if (message == NPPM_DOCLISTDISABLEEXTCOLUMN)
				nppGUI._fileSwitcherWithoutExtColumn = isOff == TRUE;
			else
				nppGUI._fileSwitcherWithoutPathColumn = isOff == TRUE;

			if (_pDocumentListPanel)
			{
				_pDocumentListPanel->reload();
			}
			// else nothing to do
			result = TRUE;
			return true;
		}

		case NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR:
		case NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR:
		{
			result = (message == NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR
					? nppParam.getCurrentDefaultFgColor()
					: nppParam.getCurrentDefaultBgColor());
			return true;
		}

		case NPPM_SHOWDOCLIST:
		{
			BOOL toShow = static_cast<BOOL>(lParam);
			if (toShow)
			{
				if (!_pDocumentListPanel || !_pDocumentListPanel->isVisible())
					launchDocumentListPanel();
			}
			else
			{
				if (_pDocumentListPanel)
					_pDocumentListPanel->display(false);
			}
			result = TRUE;
			return true;
		}

		case NPPM_ISDOCLISTSHOWN:
		{
			result = _pDocumentListPanel ? _pDocumentListPanel->isVisible() : FALSE;
			return true;
		}

		// NEW BEHAVIOUR:
		// No more file "allowAppDataPlugins.xml"
		// if doLocal - not allowed. Otherwise - allowed.
		case NPPM_GETAPPDATAPLUGINSALLOWED:
		{
			const wchar_t *appDataNpp = nppParam.getAppDataNppDir();
			result = (appDataNpp && appDataNpp[0]) ? TRUE : FALSE; // if not doLocal
			return true;
		}

		case NPPM_REMOVESHORTCUTBYCMDID:
		{
			int cmdID = static_cast<int32_t>(wParam);
			result = PluginsManager::removeShortcutByCmdID(cmdID);
			updateShortcuts(); // NPLX: upstream: the accelerator update in removeShortcutByCmdID
			return true;
		}

		case NPPM_GETNATIVELANGFILENAME:
		{
			const char* fn = _nativeLangSpeaker.getFileName();

			result = 0;
			if (!fn)
				return true;

			string fileName = fn;
			if (lParam != 0)
			{
				if (fileName.length() >= static_cast<size_t>(wParam))
				{
					return true;
				}
				strcpy(reinterpret_cast<char*>(lParam), fileName.c_str());
			}
			result = fileName.length();
			return true;
		}

		case NPPM_ADDSCNMODIFIEDFLAGS:
		{
			nppParam.addScintillaModEventMask(static_cast<unsigned long>(lParam));

			auto newModEventMask = nppParam.getScintillaModEventMask();
			_mainEditView->execute(SCI_SETMODEVENTMASK, newModEventMask);
			_subEditView->execute(SCI_SETMODEVENTMASK, newModEventMask);
			result = TRUE;
			return true;
		}

		default:
			return false;
	}
}
