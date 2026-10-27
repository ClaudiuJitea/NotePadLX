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

// NPLX: upstream MISC/PluginsManager on dlopen / dlsym. Plugins are <dir>/<Name>/<Name>.so (PluginInterface.h).
// The Plugins menu is a QMenu; each plugin command is a QAction handed to the caller (registerAction), which runs
// Notepad_plus::command(cmdID). Not ported: Plugins Admin (the plugin list download and update checks), lexer
// plugins (Lexilla exports: such a plugin loads, its lexers are not added).

#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "IDAllocator.h"
#include "PluginInterface.h"
#include "resource.h"

class QAction;
class QMenu;

using PFUNCISUNICODE = BOOL (*)();

struct PluginCommand
{
	std::wstring _pluginName;
	int _funcID = 0;
	PFUNCPLUGINCMD _pFunc = nullptr;
	PluginCommand(const wchar_t* pluginName, int funcID, PFUNCPLUGINCMD pFunc): _pluginName(pluginName), _funcID(funcID), _pFunc(pFunc) {}
};

struct PluginInfo
{
	PluginInfo() = default;
	~PluginInfo();
	PluginInfo(const PluginInfo&) = delete;
	PluginInfo& operator=(const PluginInfo&) = delete;

	void* _hLib = nullptr; // NPLX: dlopen handle (was HINSTANCE)
	QMenu* _pluginMenu = nullptr; // NPLX: owned by the Plugins menu (was HMENU)

	PFUNCSETINFO _pFuncSetInfo = nullptr;
	PFUNCGETNAME _pFuncGetName = nullptr;
	PBENOTIFIED	_pBeNotified = nullptr;
	PFUNCGETFUNCSARRAY _pFuncGetFuncsArray = nullptr;
	PMESSAGEPROC _pMessageProc = nullptr;
	PFUNCGETABIVERSION _pFuncGetAbiVersion = nullptr; // NPLX: in place of isUnicode

	FuncItem *_funcItems = nullptr;
	int _nbFuncItem = 0;
	std::wstring _moduleName; // "<Name>.so"
	std::wstring _funcName;   // getName()
};

struct LoadedDllInfo
{
	std::wstring _fullFilePath;
	std::wstring _fileName;
	std::wstring _displayName; // the plugin module's name, without '.so'

	LoadedDllInfo(const std::wstring& fullFilePath, const std::wstring& fileName)
		: _fullFilePath(fullFilePath), _fileName(fileName), _displayName(fileName.substr(0, fileName.find_last_of('.')))
	{}
};

class PluginsManager
{
public:
	PluginsManager() : _dynamicIDAlloc(ID_PLUGINS_CMD_DYNAMIC, ID_PLUGINS_CMD_DYNAMIC_LIMIT),
					   _markerAlloc(MARKER_PLUGINS, MARKER_PLUGINS_LIMIT),
					   _indicatorAlloc(INDICATOR_PLUGINS, INDICATOR_PLUGINS_LIMIT + 1)	{}
	~PluginsManager() = default;

	void init(const NppData & nppData)
	{
		_nppData = nppData;
	}

	// NPLX: dir is one plugin root (<dir>/<Name>/<Name>.so); call it for each root, the first found module wins
	bool loadPlugins(const wchar_t* dir);
	// NPLX: the default roots: the user's (NppParameters::getPluginRootDir), $NPLX_PLUGIN_PATH, the system one
	static std::vector<std::wstring> defaultPluginRoots();

	void runPluginCommand(size_t i);
	void runPluginCommand(const wchar_t *pluginName, int commandID);

	// NPLX: registerAction receives each command's QAction, to connect it and to keep it with the menu commands
	void addInMenuFromPMIndex(int i, const std::function<void(int cmdID, QAction*)>& registerAction);
	QMenu* initMenu(QMenu* hPluginsMenu, const std::function<void(int cmdID, QAction*)>& registerAction);
	static bool getShortcutByCmdID(int cmdID, ShortcutKey* sk);
	static bool removeShortcutByCmdID(int cmdID);

	void notify(size_t indexPluginInfo, const SCNotification *notification); // to a plugin
	void notify(const SCNotification *notification); // broadcast
	void relayNppMessages(UINT Message, WPARAM wParam, LPARAM lParam);
	bool relayPluginMessages(UINT Message, WPARAM wParam, LPARAM lParam);

	QMenu* getMenuHandle() const { return _hPluginsMenu; }

	void disable() {_isDisabled = true;}
	bool hasPlugins() const { return (_pluginInfos.size()!= 0); }

	bool allocateCmdID(int numberRequired, int *start);
	bool inDynamicRange(int id) const { return _dynamicIDAlloc.isInRange(id); }

	bool allocateMarker(int numberRequired, int* start);
	bool allocateIndicator(int numberRequired, int* start);
	std::wstring getLoadedPluginNames() const;

	// NPLX: for the docking panels and the toolbar
	const PluginInfo* findPlugin(const std::wstring& moduleName) const;
	int getCmdIdFromFuncIndex(const std::wstring& moduleName, int funcIndex) const;
	size_t nbPlugins() const { return _pluginInfos.size(); }
	const PluginInfo& pluginAt(size_t i) const { return *_pluginInfos[i]; }

private:
	NppData _nppData;
	QMenu* _hPluginsMenu = nullptr;

	std::vector<std::unique_ptr<PluginInfo>> _pluginInfos;
	std::vector<PluginCommand> _pluginsCommands;
	std::vector<LoadedDllInfo> _loadedDlls;
	bool _isDisabled = false;
	IDAllocator _dynamicIDAlloc;
	IDAllocator _markerAlloc;
	IDAllocator _indicatorAlloc;
	bool _noMoreNotification = false;

	int loadPluginFromPath(const wchar_t* pluginFilePath);

	static void pluginCrashAlert(const wchar_t* pluginName, const wchar_t* funcSignature);
	static void pluginExceptionAlert(const wchar_t* pluginName, const std::exception& e);

	bool isInLoadedDlls(const wchar_t* fn) const;

	void addInLoadedDlls(const wchar_t *fullPath, const wchar_t *fn) {
		_loadedDlls.push_back(LoadedDllInfo(fullPath, fn));
	}
};
