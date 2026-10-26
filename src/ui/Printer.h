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

// NPLX: port of upstream ScintillaComponent/Printer. The Win32 print dialog (PrintDlg) becomes QPrintDialog and the
// printer DC a QPrinter (or any QPagedPaintDevice, e.g. QPdfWriter for the tests): Scintilla's Qt backend draws
// SCI_FORMATRANGEFULL on the active QPainter of the device, the header and footer are drawn by the same painter.

#pragma once

#include <cstddef>

#include "ParentMessenger.h"

class QPagedPaintDevice;
class QWidget;
class ScintillaEditView;

class Printer
{
public :
	Printer() = default;

	// NPLX: upstream init(hInst, hwnd, ...); sendToNpp expands the $(...) variables of the header and footer
	void init(QWidget* parent, const ParentMessenger& sendToNpp, ScintillaEditView *pSEView, bool showDialog, size_t startPos, size_t endPos, bool isRTL = false);

	// upstream: PrintDlg, then doPrint(true); without the dialog (Print Now), the default printer
	size_t doPrint();

	// the pages: all of them, or fromPage..toPage (1-based, 0 = no limit), or the selection
	size_t doPrint(QPagedPaintDevice& device, bool justDoIt, bool printSelection = false, int fromPage = 0, int toPage = 0);

private :
	QWidget* _parent = nullptr;
	ParentMessenger _sendToNpp;
	ScintillaEditView *_pSEView = nullptr;
	bool _showDialog = true;
	size_t _startPos = 0;
	size_t _endPos = 0;
	bool _isRTL = false;
};
