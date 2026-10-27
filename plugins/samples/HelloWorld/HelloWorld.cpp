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

// Hello World: the smallest NotePadLX plugin (no Qt). It shows the whole plugin ABI:
// - menu items, one with a default shortcut (Plugins > Hello World);
// - editing text through Scintilla (SCI_* with _sciSend), opening a document (NPPM_MENUCOMMAND);
// - NPPM_* with UTF-8 strings (NPPM_GETFULLCURRENTPATH_UTF8, NPPM_SETSTATUSBAR_UTF8);
// - notifications (beNotified): it counts them, and tells NotePadLX's status bar the current file;
// - a toolbar button (NPPM_ADDTOOLBARICON_FORDARKMODE on NPPN_TBMODIFICATION, with SVG icons next to the .so);
// - NPPM_MSGTOPLUGIN: another plugin (or the test suite) asks it for its counters.
//
// Build: see plugins/CMakeLists.txt; install: ~/.local/share/notepadlx/plugins/HelloWorld/HelloWorld.so

#include <dlfcn.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "PluginInterface.h"
#include "menuCmdID.h"

#include "HelloWorld.h"

namespace
{
	NppData nppData;
	HelloWorldCounters counters;

	LRESULT npp(UINT msg, WPARAM w = 0, LPARAM l = 0)
	{
		return nppData._nppSend(nppData._nppHandle, msg, w, l);
	}

	// the Scintilla view of the current document
	NppHandle currentScintilla()
	{
		int which = -1;
		npp(NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
		if (which == -1)
			return nullptr;
		return (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
	}

	LRESULT sci(UINT msg, WPARAM w = 0, LPARAM l = 0)
	{
		return nppData._sciSend(currentScintilla(), msg, w, l);
	}

	// the folder of this .so, for its icons
	std::string pluginDir()
	{
		Dl_info info{};
		if (!::dladdr(reinterpret_cast<void*>(&pluginDir), &info) || !info.dli_fname)
			return std::string();
		std::string path = info.dli_fname;
		const size_t slash = path.find_last_of('/');
		return slash == std::string::npos ? std::string() : path.substr(0, slash);
	}

	void sayHello()
	{
		// Open a new document
		npp(NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);

		// Say hello now: Scintilla takes UTF-8
		const char* text = "Hello, NotePadLX!\n";
		sci(SCI_ADDTEXT, std::strlen(text), reinterpret_cast<LPARAM>(text));
		++counters._helloCount;
	}

	void insertFilePath()
	{
		// the UTF-8 variant of NPPM_GETFULLCURRENTPATH: the length first, then the text
		const LRESULT len = npp(NPPM_GETFULLCURRENTPATH_UTF8, 0, 0);
		if (len <= 0)
			return;
		std::string path(static_cast<size_t>(len), '\0');
		if (npp(NPPM_GETFULLCURRENTPATH_UTF8, path.size() + 1, reinterpret_cast<LPARAM>(path.data())) != len)
			return;
		sci(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(path.c_str()));
	}

	void showCounters()
	{
		char text[256];
		std::snprintf(text, sizeof(text), "Hello World: %d notifications, %d files opened, %d buffers activated",
			counters._notifications, counters._filesOpened, counters._buffersActivated);
		npp(NPPM_SETSTATUSBAR_UTF8, STATUSBAR_DOC_TYPE, reinterpret_cast<LPARAM>(text));
	}

	ShortcutKey helloKey{ true, true, true, 'H' }; // Ctrl+Alt+Shift+H

	FuncItem funcItems[HelloWorldNbFunc] = {};

	void setCommand(int index, const char* name, PFUNCPLUGINCMD func, ShortcutKey* key = nullptr)
	{
		std::snprintf(funcItems[index]._itemName, menuItemSize, "%s", name);
		funcItems[index]._pFunc = func;
		funcItems[index]._pShKey = key;
	}
}

NPLX_PLUGIN_EXPORT int getAbiVersion()
{
	return NPLX_PLUGIN_ABI_VERSION;
}

NPLX_PLUGIN_EXPORT void setInfo(NppData notepadPlusData)
{
	nppData = notepadPlusData;
	setCommand(HelloWorldSayHello, "Say Hello", sayHello, &helloKey);
	setCommand(HelloWorldInsertPath, "Insert Current File Path", insertFilePath);
	setCommand(HelloWorldSeparator, "", nullptr); // a separator
	setCommand(HelloWorldShowCounters, "Show Notification Counters", showCounters);
}

NPLX_PLUGIN_EXPORT const char* getName()
{
	return "Hello World";
}

NPLX_PLUGIN_EXPORT FuncItem* getFuncsArray(int* nbF)
{
	*nbF = HelloWorldNbFunc;
	return funcItems;
}

NPLX_PLUGIN_EXPORT void beNotified(SCNotification* notifyCode)
{
	++counters._notifications;
	switch (notifyCode->nmhdr.code)
	{
		case NPPN_TBMODIFICATION:
		{
			// the toolbar button of "Say Hello", light and dark icons
			static std::string light = pluginDir() + "/HelloWorld.svg";
			static std::string dark = pluginDir() + "/HelloWorld-dark.svg";
			toolbarIconsWithDarkMode icons{ light.c_str(), nullptr, dark.c_str() };
			npp(NPPM_ADDTOOLBARICON_FORDARKMODE, funcItems[HelloWorldSayHello]._cmdID, reinterpret_cast<LPARAM>(&icons));
			break;
		}
		case NPPN_READY:
			counters._ready = true;
			break;
		case NPPN_FILEOPENED:
			++counters._filesOpened;
			break;
		case NPPN_FILESAVED:
			++counters._filesSaved;
			break;
		case NPPN_BUFFERACTIVATED:
			++counters._buffersActivated;
			counters._lastActivatedBuffer = static_cast<uintptr_t>(notifyCode->nmhdr.idFrom);
			break;
		case NPPN_LANGCHANGED:
			++counters._langChanged;
			break;
		case NPPN_SHUTDOWN:
			counters._shutdown = true;
			break;
		case SCN_MODIFIED:
			if (notifyCode->modificationType & SC_MOD_INSERTTEXT)
			{
				++counters._textInserted;
				counters._lastScintilla = notifyCode->nmhdr.hwndFrom; // _scintillaMainHandle or _scintillaSecondHandle
			}
			break;
		default:
			break;
	}
}

NPLX_PLUGIN_EXPORT LRESULT messageProc(UINT message, WPARAM /*wParam*/, LPARAM lParam)
{
	// NPPM_MSGTOPLUGIN: the counters, for another plugin or the tests
	if (message == NPPM_MSGTOPLUGIN && lParam)
	{
		auto* info = reinterpret_cast<CommunicationInfo*>(lParam);
		if (info->internalMsg == HelloWorldMsgGetCounters && info->info)
			*static_cast<HelloWorldCounters*>(info->info) = counters;
	}
	return TRUE;
}
