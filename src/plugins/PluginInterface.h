// This file is part of Notepad++ project
// Copyright (C)2025 Don HO <don.h@free.fr>
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


// NotePadLX plugin ABI (Linux). A plugin is a shared library <Name>.so in a folder <Name>, in
// $XDG_DATA_HOME/notepadlx/plugins (usually ~/.local/share/notepadlx/plugins) or in the system plugin folder
// (<prefix>/lib/notepadlx/plugins). It exports the same functions as a Notepad++ plugin, with these changes:
//
// - Strings are UTF-8 (char), never wchar_t: getName() and FuncItem::_itemName. wchar_t is 32-bit on Linux, so
//   the NPPM_* messages that pass wchar_t strings have NPPM_*_UTF8 variants (Notepad_plus_msgs.h, end of file).
//   The wchar_t versions still work, with the Linux (UTF-32) wchar_t.
// - There is no HWND and no SendMessage. NppData holds opaque handles and the function _nppSend:
//       nppData._nppSend(nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
//       nppData._sciSend(nppData._scintillaMainHandle, SCI_GETLENGTH, 0, 0);
//   NPPM_* keep their numbers and their meaning. _nppSend also takes a Scintilla handle (as _sciSend does), so
//   a plugin can define "#define SendMessage(h, m, w, l) nppData._nppSend(h, m, w, l)" and port its calls as they are.
//   SCI_GETDIRECTFUNCTION / SCI_GETDIRECTPOINTER work for fast Scintilla calls.
// - A plugin also exports getAbiVersion(), which returns NPLX_PLUGIN_ABI_VERSION (isUnicode() is not needed).
// - Calls are made from the GUI thread only.
// - Docking panels are QWidgets (PluginDockingQt.h); toolbar icons are image files (toolbarIconsWithDarkMode).

#pragma once

#include "Scintilla.h"
#include "Notepad_plus_msgs.h"

#define NPLX_PLUGIN_ABI_VERSION 1

#define NPLX_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))

typedef void* NppHandle; // NPLX: was HWND

// NPLX: in place of ::SendMessage(HWND, ...)
typedef LRESULT (*PFUNCNPPSEND)(NppHandle handle, UINT Message, WPARAM wParam, LPARAM lParam);

typedef const char * (* PFUNCGETNAME)(); // NPLX: UTF-8

struct NppData
{
	NppHandle _nppHandle = nullptr;
	NppHandle _scintillaMainHandle = nullptr;
	NppHandle _scintillaSecondHandle = nullptr;
	// NPLX: the message functions. _nppSend: NPPM_* (and WM_COMMAND) to _nppHandle, or SCI_* to a Scintilla handle.
	// _sciSend: SCI_* to a Scintilla handle (_scintillaMainHandle, _scintillaSecondHandle, NPPM_CREATESCINTILLAHANDLE)
	PFUNCNPPSEND _nppSend = nullptr;
	PFUNCNPPSEND _sciSend = nullptr;
	int _abiVersion = NPLX_PLUGIN_ABI_VERSION; // the host's
};

typedef void (* PFUNCSETINFO)(NppData);
typedef void (* PFUNCPLUGINCMD)();
typedef void (* PBENOTIFIED)(SCNotification *);
typedef LRESULT (* PMESSAGEPROC)(UINT Message, WPARAM wParam, LPARAM lParam);
typedef int (* PFUNCGETABIVERSION)(); // NPLX


struct ShortcutKey
{
	bool _isCtrl = false;
	bool _isAlt = false;
	bool _isShift = false;
	UCHAR _key = 0; // a Windows virtual-key code (VK_*, 'A'..'Z', '0'..'9'), as in shortcuts.xml
};

const int menuItemSize = 64;

struct FuncItem
{
	char _itemName[menuItemSize] = { '\0' }; // NPLX: UTF-8; an empty name with _pFunc == nullptr is a separator
	PFUNCPLUGINCMD _pFunc = nullptr;
	int _cmdID = 0;          // set by NotePadLX
	bool _init2Check = false;
	ShortcutKey *_pShKey = nullptr;
};

typedef FuncItem * (* PFUNCGETFUNCSARRAY)(int *);

// You should implement (or define an empty function body) those functions which are called by NotePadLX plugin manager
NPLX_PLUGIN_EXPORT void setInfo(NppData);
NPLX_PLUGIN_EXPORT const char * getName();
NPLX_PLUGIN_EXPORT FuncItem * getFuncsArray(int *);
NPLX_PLUGIN_EXPORT void beNotified(SCNotification *);
NPLX_PLUGIN_EXPORT LRESULT messageProc(UINT Message, WPARAM wParam, LPARAM lParam);
NPLX_PLUGIN_EXPORT int getAbiVersion(); // NPLX: return NPLX_PLUGIN_ABI_VERSION
