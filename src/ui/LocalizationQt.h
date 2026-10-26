// This file is part of NotePadLX, a Linux port of Notepad++.
// Copyright (C)2021 Don HO <don.h@free.fr>
// Modified for NotePadLX (Linux port).
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

// NPLX: upstream NativeLangSpeaker::changeDlgLang and friends on Qt widgets. The translations come from the toolkit-free
// NativeLangSpeaker (core/localization.h); the dialogs find their controls by upstream control ID.

#pragma once

#include <functional>
#include <string>

class NativeLangSpeaker;
class QWidget;
class RcDialog;

namespace nplx
{
	using DlgItemLookup = std::function<QWidget*(int id)>;

	// the language of the session (NppParameters::getNativeLangSpeaker), nullptr in a test without one
	const NativeLangSpeaker* nativeLangSpeaker();

	// upstream changeDlgLang: the dialog title and the text of its labels, buttons, group boxes and combo box lists;
	// false when the language does not translate this dialog. titleOut: the translated title (empty when none)
	bool changeDlgLang(QWidget* dlg, const DlgItemLookup& item, const char* dlgTagName, const char* titleTag = nullptr,
		std::wstring* titleOut = nullptr);
	bool changeDlgLang(RcDialog& dlg, const char* dlgTagName, const char* titleTag = nullptr, std::wstring* titleOut = nullptr);
	// a dialog built in code: its controls carry their upstream ID (setCtrlId)
	bool changeDlgLang(QWidget* dlg, const char* dlgTagName, const char* titleTag = nullptr, std::wstring* titleOut = nullptr);
	void setCtrlId(QWidget* w, int id);
	QWidget* ctrlById(const QWidget* dlg, int id);

	// the text of a label, button or group box (not of an edit field)
	void setControlText(QWidget* w, const std::wstring& text);
}
