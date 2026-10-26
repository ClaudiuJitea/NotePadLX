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

// NPLX: what upstream Parameters.cpp takes from UI headers (Notepad_plus.h, AboutDlg.h,
// NppBigSwitch.cpp, ...), provided here so that the core does not depend on the UI.

#pragma once

#include <atomic>
#include <string>

#include "UiServices.h"
#include "win32compat.h"


// Upstream: NppBigSwitch.cpp. Set when the application is exiting.
extern std::atomic<bool> g_bNppExitFlag;

// Upstream: WinControls/AboutDlg/AboutDlg.h. Same call shape; the dialog is shown by ui/
// through UiServices::networkPathWarning.
// Button ids (upstream): IDCANCEL = skip, IDYES = always load from this server, IDNO = always skip network paths,
// IDRETRY = always load network paths.
class NetworkPathWarningBox
{
public:
	void init(const std::wstring& path, const char* titleTag = nullptr) {
		_networkPath = path;
		if (titleTag)
			_titleTag = titleTag;
	}

	void doDialog(bool /*isRTL*/ = false) {
		_clickedButtonId = nplx::networkPathWarning(_networkPath, _titleTag);
	}

	void destroy() {}

	int getClickedButtonId() const {
		return _clickedButtonId;
	}

private:
	int _clickedButtonId = -1;
	std::wstring _networkPath;
	std::string _titleTag;
};
