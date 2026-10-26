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

// NPLX: Qt implementation of the core UI services (message boxes, screens).

#pragma once

#include "UiServices.h"

class QtUiServices : public nplx::UiServices
{
public:
	int messageBox(const std::wstring& message, const std::wstring& title, unsigned int type) override;
	int saveOrNotMultiBox(const std::wstring& message, const std::wstring& title) override;
	std::wstring getExistingDirectory(const std::wstring& title) override;
	std::wstring getOpenFileName(const std::wstring& title, const std::wstring& filter, const std::wstring& dir) override;
	std::vector<std::wstring> getOpenFileNames(const std::wstring& title, const std::wstring& filter, const std::wstring& dir) override;
	std::wstring getSaveFileName(const std::wstring& title, const std::wstring& filter, const std::wstring& dir) override;
	std::wstring getSaveFileNameEx(const std::wstring& title, const std::wstring& filter, const std::wstring& dir,
		const std::wstring& defExt, const std::wstring& checkboxLabel, bool* checkboxState) override;
	bool getText(const std::wstring& title, const std::wstring& label, std::wstring& text) override;
	int networkPathWarning(const std::wstring& path, const std::string& titleTag) override;
	SIZE primaryScreenSize() override;
	bool isRectVisibleOnAnyScreen(const RECT& rc) override;
};
