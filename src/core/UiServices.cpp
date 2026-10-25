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

#include "UiServices.h"

#include <cstdio>

#include "Common.h"
#include "ParametersCompat.h"

std::atomic<bool> g_bNppExitFlag{ false };

namespace nplx
{
namespace
{
	UiServices* g_uiServices = nullptr;
}

void setUiServices(UiServices* services)
{
	g_uiServices = services;
}

UiServices* uiServices()
{
	return g_uiServices;
}

int messageBox(const std::wstring& message, const std::wstring& title, unsigned int type)
{
	if (g_uiServices)
		return g_uiServices->messageBox(message, title, type);

	std::fprintf(stderr, "NotePadLX: %s: %s\n", wstring2string(title).c_str(), wstring2string(message).c_str());
	const unsigned int buttons = type & 0x0F;
	return (buttons == MB_YESNO || buttons == MB_YESNOCANCEL) ? IDYES : IDOK;
}

int networkPathWarning(const std::wstring& path, const std::string& titleTag)
{
	if (g_uiServices)
		return g_uiServices->networkPathWarning(path, titleTag);
	return IDCANCEL; // skip once
}

SIZE primaryScreenSize()
{
	if (g_uiServices)
		return g_uiServices->primaryScreenSize();
	return SIZE{ 1920, 1080 };
}

bool isRectVisibleOnAnyScreen(const RECT& rc)
{
	if (g_uiServices)
		return g_uiServices->isRectVisibleOnAnyScreen(rc);
	return true;
}
}
