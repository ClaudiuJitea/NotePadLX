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


// NotePadLX docking for plugins (the Qt extension of PluginInterface.h; upstream Docking.h). A plugin that docks a
// panel links Qt 6 Widgets (the Qt NotePadLX runs with) and registers a QWidget:
//
//     static DockedWidgetData data;
//     data.hClient = myWidget;               // a QWidget without a parent; NotePadLX puts it in a dock
//     data.pszName = "My panel";             // the dock title, UTF-8
//     data.dlgID = 2;                        // the index in the FuncItem array of the command showing the panel
//     data.uMask = DWS_DF_CONT_RIGHT;        // the default side: DWS_DF_CONT_LEFT / RIGHT / TOP / BOTTOM, DWS_DF_FLOATING
//     data.pszModuleName = "MyPlugin.so";    // the plugin file name
//     nppData._nppSend(nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&data));
//
// NPPM_DMMSHOW / NPPM_DMMHIDE / NPPM_DMMUPDATEDISPINFO take the QWidget* (hClient) as lParam. The strings must stay
// valid while the panel exists (NotePadLX reads pszAddInfo again on NPPM_DMMUPDATEDISPINFO). When the user closes
// the dock, the widget is hidden (it gets a QHideEvent) and the check mark of the menu item dlgID is removed, as
// upstream plugins do on DMN_CLOSE. The panel's container and visibility are saved in config.xml; at the next start
// NotePadLX runs the command dlgID again when the panel was visible.

#pragma once

#include "PluginInterface.h"
#include "DockingCont.h" // CONT_*, DWS_*

class QWidget;

struct DockedWidgetData
{
	QWidget* hClient = nullptr;           // NPLX: was the client HWND
	const char* pszName = nullptr;        // NPLX: UTF-8. Name of plugin (shown in window)
	int dlgID = 0;                        // a funcItem provides the function pointer to start a dialog. Please parse here these ID
	UINT uMask = 0;                       // mask params: look to the defines in DockingCont.h
	const char* pszIconTab = nullptr;     // NPLX: an image file (UTF-8 path) for the tab, or nullptr (was HICON hIconTab)
	const char* pszAddInfo = nullptr;     // for plugin to display additional informations (DWS_ADDINFO)
	RECT rcFloat = {};                    // floating position (not used: the window manager places floating docks)
	int iPrevCont = 0;                    // stores the privious container (toggling between float and dont)
	const char* pszModuleName = nullptr;  // it's the plugin file name. It's used to identify the plugin
};

using tTbData = DockedWidgetData; // the old name
