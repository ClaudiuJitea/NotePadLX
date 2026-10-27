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

// NPLX: LoadLibraryEx / GetProcAddress / FreeLibrary became dlopen / dlsym / dlclose. The PE architecture check is
// gone (dlopen refuses a library of another architecture, with a message), isUnicode is replaced by getAbiVersion.
// FindFirstFile / FindNextFile became std::filesystem. Plugin crashes: a C++ exception thrown by a plugin is caught
// as upstream; a crash (SIGSEGV) is not, there is no SEH on Linux.

#include "PluginsManager.h"

#include <dlfcn.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <system_error>

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QMenu>

#include "Common.h"
#include "Notepad_plus_msgs.h"
#include "Parameters.h"
#include "UiServices.h"
#include "localization.h"
#include "menuCmdID.h"
#include "shortcut.h"

using namespace std;

namespace
{
	const wchar_t * USERMSG = L" is not compatible with the current version of NotePadLX.\n\n\
Do you want to remove this plugin from the plugins directory to prevent this message from the next launch?";

	template <typename T>
	T getProc(void* hLib, const char* name)
	{
		return reinterpret_cast<T>(::dlsym(hLib, name));
	}

	int messageBox(const wstring& message, const wstring& title, unsigned int type)
	{
		nplx::UiServices* ui = nplx::uiServices();
		return ui ? ui->messageBox(message, title, type) : IDOK;
	}
}

PluginInfo::~PluginInfo()
{
	// NPLX: the menu belongs to the Plugins menu (Qt parent). The library is not closed (upstream FreeLibrary):
	// the main window deletes its child widgets after its members, and a plugin's docking panel is code of the
	// library; the process exit unloads it.
}

void PluginsManager::pluginCrashAlert(const wchar_t* pluginName, const wchar_t* funcSignature)
{
	std::wstring msg = pluginName;
	msg += L" just crashed in\r";
	msg += funcSignature;
	messageBox(msg, L"Plugin Crash", MB_OK | MB_ICONSTOP);
}

void PluginsManager::pluginExceptionAlert(const wchar_t* pluginName, const std::exception& e)
{
	std::wstring msg = L"An exception occurred due to plugin: ";
	msg += pluginName;
	msg += L"\r\n\r\nException reason: ";
	msg += string2wstring(e.what(), CP_UTF8);

	messageBox(msg, L"Plugin Exception", MB_OK);
}

bool PluginsManager::isInLoadedDlls(const wchar_t* fn) const
{
	// NPLX: file names are case-sensitive on Linux (was _wcsicmp)
	return std::any_of(_loadedDlls.begin(), _loadedDlls.end(),
		[&fn](const auto& dll) { return dll._fileName == fn; });
}

int PluginsManager::loadPluginFromPath(const wchar_t *pluginFilePath)
{
	const wstring pluginFileName = std::filesystem::path(pluginFilePath).filename().wstring(); // NPLX: was ::PathFindFileName
	if (isInLoadedDlls(pluginFileName.c_str()))
		return 0;

	auto pi = std::make_unique<PluginInfo>();
	try
	{
		pi->_moduleName = pluginFileName;

		// NPLX: RTLD_LOCAL so that the plugins do not see each other's symbols (as Windows DLLs)
		const string path8 = wstring2string(pluginFilePath, CP_UTF8);
		pi->_hLib = ::dlopen(path8.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (!pi->_hLib)
		{
			const char* err = ::dlerror();
			throw wstring(err ? string2wstring(err, CP_UTF8) : wstring(L"Load Library has failed."));
		}

		pi->_pFuncGetAbiVersion = getProc<PFUNCGETABIVERSION>(pi->_hLib, "getAbiVersion");
		if (!pi->_pFuncGetAbiVersion)
			throw wstring(L"Missing \"getAbiVersion\" function: this is not a NotePadLX plugin (a Windows Notepad++ plugin must be rebuilt for Linux).");
		const int abiVersion = pi->_pFuncGetAbiVersion();
		if (abiVersion < 1 || abiVersion > NPLX_PLUGIN_ABI_VERSION)
			throw wstring(L"This plugin was built for the NotePadLX plugin ABI version " + std::to_wstring(abiVersion)
				+ L"; this NotePadLX supports version " + std::to_wstring(NPLX_PLUGIN_ABI_VERSION) + L".");

		pi->_pFuncSetInfo = getProc<PFUNCSETINFO>(pi->_hLib, "setInfo");
		if (!pi->_pFuncSetInfo)
			throw wstring(L"Missing \"setInfo\" function");

		pi->_pFuncGetName = getProc<PFUNCGETNAME>(pi->_hLib, "getName");
		if (!pi->_pFuncGetName)
			throw wstring(L"Missing \"getName\" function");
		const char* name = pi->_pFuncGetName();
		pi->_funcName = string2wstring(name ? name : "", CP_UTF8); // NPLX: UTF-8

		pi->_pBeNotified = getProc<PBENOTIFIED>(pi->_hLib, "beNotified");
		if (!pi->_pBeNotified)
			throw wstring(L"Missing \"beNotified\" function");

		pi->_pMessageProc = getProc<PMESSAGEPROC>(pi->_hLib, "messageProc");
		if (!pi->_pMessageProc)
			throw wstring(L"Missing \"messageProc\" function");

		pi->_pFuncSetInfo(_nppData);

		pi->_pFuncGetFuncsArray = getProc<PFUNCGETFUNCSARRAY>(pi->_hLib, "getFuncsArray");
		if (!pi->_pFuncGetFuncsArray)
			throw wstring(L"Missing \"getFuncsArray\" function");

		pi->_funcItems = pi->_pFuncGetFuncsArray(&pi->_nbFuncItem);

		if ((!pi->_funcItems) || (pi->_nbFuncItem <= 0))
			throw wstring(L"Missing \"FuncItems\" array, or the nb of Function Item is not set correctly");

		// NPLX_TODO(Phase 9+): lexer plugins (upstream: GetLexerCount / GetLexerName / CreateLexer and
		// plugins/Config/<module>.xml); such a plugin loads, its lexers are not added

		addInLoadedDlls(pluginFilePath, pluginFileName.c_str());
		_pluginInfos.push_back(std::move(pi));
		return static_cast<int>(_pluginInfos.size() - 1);
	}
	catch (std::exception& e)
	{
		pluginExceptionAlert(pluginFileName.c_str(), e);
		return -1;
	}
	catch (const std::wstring& s)
	{
		if (pi && pi->_hLib)
		{
			::dlclose(pi->_hLib);
			pi->_hLib = nullptr;
		}

		std::wstring msg = s;
		msg += L"\n\n";
		msg += pluginFileName;
		msg += USERMSG;
		if (messageBox(msg, pluginFilePath, MB_YESNO) == IDYES)
		{
			std::error_code ec;
			std::filesystem::remove(pluginFilePath, ec); // NPLX: was ::DeleteFile
		}
		return -1;
	}
	catch (...)
	{
		if (pi && pi->_hLib)
		{
			::dlclose(pi->_hLib);
			pi->_hLib = nullptr;
		}

		wstring msg = L"Failed to load";
		msg += L"\n\n";
		msg += pluginFileName;
		msg += USERMSG;
		if (messageBox(msg, pluginFilePath, MB_YESNO) == IDYES)
		{
			std::error_code ec;
			std::filesystem::remove(pluginFilePath, ec);
		}
		return -1;
	}
}

std::vector<std::wstring> PluginsManager::defaultPluginRoots()
{
	std::vector<std::wstring> roots;
	auto add = [&roots](const std::wstring& dir) {
		if (!dir.empty() && std::find(roots.begin(), roots.end(), dir) == roots.end())
			roots.push_back(dir);
	};

	// the user's plugins first: they win over a system plugin with the same name
	add(NppParameters::getInstance().getPluginRootDir());

	// NPLX: extra roots (development, tests), separated by ':'
	if (const char* env = std::getenv("NPLX_PLUGIN_PATH"))
	{
		for (const QString& dir : QString::fromLocal8Bit(env).split(QLatin1Char(':'), Qt::SkipEmptyParts))
			add(dir.toStdWString());
	}

	// next to the program (AppImage, relocated installs), then the configured install prefix
	add(QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/notepadlx/plugins")).toStdWString());
#ifdef NPLX_SYSTEM_PLUGIN_DIR
	add(QStringLiteral(NPLX_SYSTEM_PLUGIN_DIR).toStdWString());
#endif
	return roots;
}

bool PluginsManager::loadPlugins(const wchar_t* dir)
{
	if (_isDisabled)
		return false;

	if (!dir || !dir[0])
		return false;

	// NPLX: <dir>/<Name>/<Name>.so (upstream: <dir>\<Name>\<Name>.dll), sorted so the load order is stable.
	// The plugin compatibility list of Plugins Admin is not ported.
	namespace fs = std::filesystem;
	std::error_code ec;
	vector<wstring> dllNames;
	for (fs::directory_iterator it(fs::path(dir), ec), end; !ec && it != end; it.increment(ec))
	{
		if (!it->is_directory(ec))
			continue;

		wstring foundFileName = it->path().filename().wstring();
		// "Config" holds settings, not plugins; "disabled" holds deactivated plugins moved by
		// the Plugins Admin dialog - both must never be scanned/loaded as plugin folders.
		if (foundFileName == L"Config" || foundFileName == L"disabled")
			continue;

		fs::path pluginsFullPathFilter = it->path() / (foundFileName + L".so");
		if (fs::is_regular_file(pluginsFullPathFilter, ec))
			dllNames.push_back(pluginsFullPathFilter.wstring());
	}
	std::sort(dllNames.begin(), dllNames.end());

	for (size_t i = 0, len = dllNames.size(); i < len; ++i)
	{
		loadPluginFromPath(dllNames[i].c_str());
	}

	return true;
}

// return true if cmdID found and its shortcut is enable
// false otherwise
bool PluginsManager::getShortcutByCmdID(int cmdID, ShortcutKey* sk)
{
	if (cmdID == 0 || !sk)
		return false;

	const vector<PluginCmdShortcut> & pluginCmdSCList = (NppParameters::getInstance()).getPluginCommandList();

	for (size_t i = 0, len = pluginCmdSCList.size(); i < len ; ++i)
	{
		if (pluginCmdSCList[i].getID() == static_cast<unsigned long>(cmdID))
		{
			const KeyCombo & kc = pluginCmdSCList[i].getKeyCombo();
			if (kc._key == 0x00)
				return false;

			sk->_isAlt = kc._isAlt;
			sk->_isCtrl = kc._isCtrl;
			sk->_isShift = kc._isShift;
			sk->_key = kc._key;
			return true;
		}
	}
	return false;
}

// returns false if cmdID not provided, true otherwise
bool PluginsManager::removeShortcutByCmdID(int cmdID)
{
	if (cmdID == 0)
		return false;

	NppParameters& nppParam = NppParameters::getInstance();
	vector<PluginCmdShortcut> & pluginCmdSCList = nppParam.getPluginCommandList();

	for (size_t i = 0, len = pluginCmdSCList.size(); i < len; ++i)
	{
		if (pluginCmdSCList[i].getID() == static_cast<unsigned long>(cmdID))
		{
			//remove shortcut
			pluginCmdSCList[i].clear();

			// NPLX: the caller updates the QAction shortcuts (upstream: nppParam.getAccelerator()->updateShortcuts())

			// set dirty flag to force writing shortcuts.xml on shutdown
			nppParam.setShortcutDirty();
			break;
		}
	}
	return true;
}

void PluginsManager::addInMenuFromPMIndex(int i, const std::function<void(int cmdID, QAction*)>& registerAction)
{
	vector<PluginCmdShortcut> & pluginCmdSCList = (NppParameters::getInstance()).getPluginCommandList();
	// NPLX: ::InsertMenu(_hPluginsMenu, i, MF_BYPOSITION | MF_POPUP, ...) with the plugin name
	QAction* before = _hPluginsMenu->actions().value(i, nullptr);
	_pluginInfos[i]->_pluginMenu = new QMenu(QString::fromStdWString(_pluginInfos[i]->_funcName), _hPluginsMenu);
	_hPluginsMenu->insertMenu(before, _pluginInfos[i]->_pluginMenu);

	unsigned short j = 0;
	for ( ; j < _pluginInfos[i]->_nbFuncItem ; ++j)
	{
		if (_pluginInfos[i]->_funcItems[j]._pFunc == NULL)
		{
			_pluginInfos[i]->_pluginMenu->addSeparator();
			continue;
		}

		_pluginsCommands.push_back(PluginCommand(_pluginInfos[i]->_moduleName.c_str(), j, _pluginInfos[i]->_funcItems[j]._pFunc));

		const int cmdID = ID_PLUGINS_CMD + static_cast<int>(_pluginsCommands.size() - 1);
		_pluginInfos[i]->_funcItems[j]._cmdID = cmdID;
		// NPLX: the name is UTF-8 already; at most menuItemSize bytes even without a NUL
		const char* rawName = _pluginInfos[i]->_funcItems[j]._itemName;
		string itemName(rawName, ::strnlen(rawName, menuItemSize));

		if (_pluginInfos[i]->_funcItems[j]._pShKey)
		{
			ShortcutKey & sKey = *(_pluginInfos[i]->_funcItems[j]._pShKey);
			PluginCmdShortcut pcs(Shortcut(itemName.c_str(), sKey._isCtrl, sKey._isAlt, sKey._isShift, sKey._key), cmdID, wstring2string(_pluginInfos[i]->_moduleName, CP_UTF8).c_str(), j);
			pluginCmdSCList.push_back(pcs);
			// NPLX: the QAction shows its shortcut itself (upstream: itemName += "\t" + pcs.toString())
		}
		else
		{	//no ShortcutKey is provided, add an disabled shortcut (so it can still be mapped, Paramaters class can still index any changes and the toolbar won't funk out
			Shortcut sc(itemName.c_str(), false, false, false, 0x00);
			PluginCmdShortcut pcs(sc, cmdID, wstring2string(_pluginInfos[i]->_moduleName, CP_UTF8).c_str(), j);	//VK_NULL and everything disabled, the menu name is left alone
			pluginCmdSCList.push_back(pcs);
		}

		// NPLX: ::InsertMenu(_pluginInfos[i]->_pluginMenu, j, MF_BYPOSITION, cmdID, itemName)
		QAction* action = _pluginInfos[i]->_pluginMenu->addAction(QString::fromStdString(itemName));
		action->setData(cmdID);
		if (_pluginInfos[i]->_funcItems[j]._init2Check)
		{
			action->setCheckable(true);
			action->setChecked(true);
		}
		if (registerAction)
			registerAction(cmdID, action);
	}
}

QMenu* PluginsManager::initMenu(QMenu* hPluginsMenu, const std::function<void(int cmdID, QAction*)>& registerAction)
{
	size_t nbPlugin = _pluginInfos.size();

	if (!_hPluginsMenu)
	{
		_hPluginsMenu = hPluginsMenu; // upstream ::GetSubMenu(hMenu, MENUINDEX_PLUGINS)
		if (!_hPluginsMenu)
			return nullptr;

		if (nbPlugin > 0)
			_hPluginsMenu->insertSeparator(_hPluginsMenu->actions().value(0, nullptr));
		// NPLX: Plugins Admin is not ported (no Linux plugin list to download)
	}

	for (size_t i = 0; i < nbPlugin; ++i)
	{
		addInMenuFromPMIndex(static_cast<int>(i), registerAction);
	}
	return _hPluginsMenu;
}

void PluginsManager::runPluginCommand(size_t i)
{
	if (i < _pluginsCommands.size())
	{
		if (_pluginsCommands[i]._pFunc != NULL)
		{
			try
			{
				_pluginsCommands[i]._pFunc();
			}
			catch (std::exception& e)
			{
				messageBox(string2wstring(e.what(), CP_UTF8), L"PluginsManager::runPluginCommand Exception", MB_OK);
			}
			catch (...)
			{
				static constexpr size_t bufSize = 128;
				wchar_t funcInfo[bufSize] = { '\0' };
				swprintf(funcInfo, bufSize, L"runPluginCommand(size_t i : %zu)", i);
				pluginCrashAlert(_pluginsCommands[i]._pluginName.c_str(), funcInfo);
			}
		}
	}
}

void PluginsManager::runPluginCommand(const wchar_t *pluginName, int commandID)
{
	for (size_t i = 0, len = _pluginsCommands.size() ; i < len ; ++i)
	{
		if (_pluginsCommands[i]._pluginName == pluginName) // NPLX: was _wcsicmp
		{
			if (_pluginsCommands[i]._funcID == commandID)
			{
				try
				{
					_pluginsCommands[i]._pFunc();
				}
				catch (std::exception& e)
				{
					pluginExceptionAlert(_pluginsCommands[i]._pluginName.c_str(), e);
				}
				catch (...)
				{
					static constexpr size_t bufSize = 128;
					wchar_t funcInfo[bufSize] = { '\0' };
					swprintf(funcInfo, bufSize, L"runPluginCommand(const wchar_t *pluginName : %ls, int commandID : %d)", pluginName, commandID);
					pluginCrashAlert(_pluginsCommands[i]._pluginName.c_str(), funcInfo);
				}
			}
		}
	}
}

// send the notification to a specific plugin
void PluginsManager::notify(size_t indexPluginInfo, const SCNotification *notification)
{
	if (indexPluginInfo >= _pluginInfos.size())
		return;

	if (_pluginInfos[indexPluginInfo]->_hLib)
	{
		// To avoid the plugin change the data in SCNotification
		// Each notification to pass to a plugin is a copy of SCNotification instance
		SCNotification scNotif = *notification;
		try
		{
			_pluginInfos[indexPluginInfo]->_pBeNotified(&scNotif);
		}
		catch (std::exception& e)
		{
			pluginExceptionAlert(_pluginInfos[indexPluginInfo]->_moduleName.c_str(), e);
		}
		catch (...)
		{
			static constexpr size_t bufSize = 256;
			wchar_t funcInfo[bufSize] = { '\0' };
			swprintf(funcInfo, bufSize, L"notify(SCNotification *notification) : \r notification->nmhdr.code == %d\r notification->nmhdr.hwndFrom == %p\r notification->nmhdr.idFrom == %" PRIuPTR, \
				scNotif.nmhdr.code, scNotif.nmhdr.hwndFrom, scNotif.nmhdr.idFrom);
			pluginCrashAlert(_pluginInfos[indexPluginInfo]->_moduleName.c_str(), funcInfo);
		}
	}
}

// broadcast the notification to all plugins
void PluginsManager::notify(const SCNotification *notification)
{
	if (_noMoreNotification) // this boolean should be enabled after NPPN_SHUTDOWN has been sent
		return;
	_noMoreNotification = notification->nmhdr.code == NPPN_SHUTDOWN;

	for (size_t i = 0, len = _pluginInfos.size() ; i < len ; ++i)
	{
		notify(i, notification);
	}
}

void PluginsManager::relayNppMessages(UINT Message, WPARAM wParam, LPARAM lParam)
{
	for (size_t i = 0, len = _pluginInfos.size(); i < len ; ++i)
	{
		if (_pluginInfos[i]->_hLib)
		{
			try
			{
				_pluginInfos[i]->_pMessageProc(Message, wParam, lParam);
			}
			catch (std::exception& e)
			{
				pluginExceptionAlert(_pluginInfos[i]->_moduleName.c_str(), e);
			}
			catch (...)
			{
				static constexpr size_t bufSize = 128;
				wchar_t funcInfo[bufSize] = { '\0' };
				swprintf(funcInfo, bufSize, L"relayNppMessages(UINT Message : %u, WPARAM wParam : %" PRIuPTR ", LPARAM lParam : %" PRIiPTR ")", Message, wParam, lParam);
				pluginCrashAlert(_pluginInfos[i]->_moduleName.c_str(), funcInfo);
			}
		}
	}
}

bool PluginsManager::relayPluginMessages(UINT Message, WPARAM wParam, LPARAM lParam)
{
	const auto* moduleName = reinterpret_cast<wchar_t*>(wParam);
	if (!moduleName || !moduleName[0] || !lParam)
		return false;

	for (size_t i = 0, len = _pluginInfos.size() ; i < len ; ++i)
	{
		if (_pluginInfos[i]->_moduleName == moduleName)
		{
			if (_pluginInfos[i]->_hLib)
			{
				try
				{
					_pluginInfos[i]->_pMessageProc(Message, wParam, lParam);
				}
				catch (std::exception& e)
				{
					pluginExceptionAlert(_pluginInfos[i]->_moduleName.c_str(), e);
				}
				catch (...)
				{
					static constexpr size_t bufSize = 128;
					wchar_t funcInfo[bufSize] = { '\0' };
					swprintf(funcInfo, bufSize, L"relayPluginMessages(UINT Message : %u, WPARAM wParam : %" PRIuPTR ", LPARAM lParam : %" PRIiPTR ")", Message, wParam, lParam);
					pluginCrashAlert(_pluginInfos[i]->_moduleName.c_str(), funcInfo);
				}
				return true;
			}
		}
	}
	return false;
}

bool PluginsManager::allocateCmdID(int numberRequired, int *start)
{
	bool retVal = true;

	*start = _dynamicIDAlloc.allocate(numberRequired);

	if (*start == -1)
	{
		*start = 0;
		retVal = false;
	}
	return retVal;
}

bool PluginsManager::allocateMarker(int numberRequired, int* start)
{
	bool retVal = true;
	*start = _markerAlloc.allocate(numberRequired);
	if (*start == -1)
	{
		*start = 0;
		retVal = false;
	}
	return retVal;
}

bool PluginsManager::allocateIndicator(int numberRequired, int* start)
{
	bool retVal = false;
	int possibleStart = _indicatorAlloc.allocate(numberRequired);
	if (possibleStart != -1)
	{
		*start = possibleStart;
		retVal = true;
	}
	return retVal;
}

wstring PluginsManager::getLoadedPluginNames() const
{
	// NPLX: upstream shows the DLL file version (PluginUpdateInfo); a .so has none, so the folder is shown
	wstring pluginPaths;
	for (const auto &dll : _loadedDlls)
	{
		pluginPaths += L"\n    ";
		pluginPaths += dll._displayName;
		pluginPaths += L" (";
		pluginPaths += dll._fullFilePath;
		pluginPaths += L")";
	}
	return pluginPaths;
}

const PluginInfo* PluginsManager::findPlugin(const std::wstring& moduleName) const
{
	for (const auto& pi : _pluginInfos)
	{
		if (pi->_moduleName == moduleName)
			return pi.get();
	}
	return nullptr;
}

int PluginsManager::getCmdIdFromFuncIndex(const std::wstring& moduleName, int funcIndex) const
{
	const PluginInfo* pi = findPlugin(moduleName);
	if (!pi || funcIndex < 0 || funcIndex >= pi->_nbFuncItem)
		return 0;
	return pi->_funcItems[funcIndex]._cmdID;
}
