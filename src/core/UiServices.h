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

// NPLX: the few UI services that ported core code needs (upstream calls ::MessageBox /
// NppDarkMode::darkMessageBoxW directly). ui/ installs a Qt implementation at startup.
// Without one (tests, command-line tools) a message is written to stderr and the
// default answer is returned: IDYES for MB_YESNO / MB_YESNOCANCEL, IDOK otherwise.

#pragma once

#include <string>
#include <vector>

#include "win32compat.h"

namespace nplx
{
	class UiServices
	{
	public:
		virtual ~UiServices() = default;

		// type: MB_* flags from win32compat.h; returns IDOK / IDCANCEL / IDYES / IDNO
		virtual int messageBox(const std::wstring& message, const std::wstring& title, unsigned int type) = 0;

		// Upstream DoSaveOrNotBox (several dirty documents): returns IDYES, IDNO, IDRETRY ("Yes to all"),
		// IDIGNORE ("No to all") or IDCANCEL
		virtual int saveOrNotMultiBox(const std::wstring& message, const std::wstring& title) = 0;

		// Upstream NetworkPathWarningBox. Returns IDCANCEL (skip once), IDNO (always skip),
		// IDYES (always load) or IDOK (load once).
		virtual int networkPathWarning(const std::wstring& path, const std::string& titleTag) = 0;

		virtual SIZE primaryScreenSize() = 0;
		virtual bool isRectVisibleOnAnyScreen(const RECT& rc) = 0; // rc: x, y, width, height (like upstream's caller)

		// Upstream folderBrowser (SHBrowseForFolder / IFileOpenDialog with FOS_PICKFOLDERS); empty when cancelled
		virtual std::wstring getExistingDirectory(const std::wstring& title) { (void)title; return std::wstring(); }

		// Upstream CustomFileDialog: doOpenSingleFileDlg / doOpenMultiFilesDlg / doSaveDlg. filter: "Name (*.ext);;..."
		virtual std::wstring getOpenFileName(const std::wstring& title, const std::wstring& filter, const std::wstring& dir) { (void)title; (void)filter; (void)dir; return std::wstring(); }
		virtual std::vector<std::wstring> getOpenFileNames(const std::wstring& title, const std::wstring& filter, const std::wstring& dir) { (void)title; (void)filter; (void)dir; return {}; }
		virtual std::wstring getSaveFileName(const std::wstring& title, const std::wstring& filter, const std::wstring& dir) { (void)title; (void)filter; (void)dir; return std::wstring(); }

		// A one-line text input (e.g. upstream FileRelocalizerDlg); false when cancelled
		virtual bool getText(const std::wstring& title, const std::wstring& label, std::wstring& text) { (void)title; (void)label; (void)text; return false; }
	};

	void setUiServices(UiServices* services);
	UiServices* uiServices();

	int messageBox(const std::wstring& message, const std::wstring& title, unsigned int type);
	int networkPathWarning(const std::wstring& path, const std::string& titleTag); // default: IDCANCEL
	SIZE primaryScreenSize();                         // default: 1920x1080
	bool isRectVisibleOnAnyScreen(const RECT& rc);    // default: true
}
