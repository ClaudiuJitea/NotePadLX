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

// NPLX: port of upstream WinControls/ClipboardHistory/clipboardHistoryPanel. The clipboard viewer chain
// (SetClipboardViewer / WM_DRAWCLIPBOARD) is QClipboard::dataChanged; the data is kept as UTF-8 bytes (upstream:
// the CF_UNICODETEXT UTF-16 bytes), so text with NUL characters keeps its length. On Wayland, compositors report
// clipboard changes to the focused application only.

#pragma once

#include <vector>

#include <QColor>

#include "DockingPanel.h"

class QListWidget;
class ScintillaEditView;

#define CH_PROJECTPANELTITLE		L"Clipboard History"

struct ClipboardDataInfo {
	std::vector<unsigned char> _data;
	bool _isBinaryContained = false;
};

class ClipboardHistoryPanel : public DockingPanel
{
	Q_OBJECT

public:
	explicit ClipboardHistoryPanel(QWidget* parent = nullptr);

	void init(ScintillaEditView **ppEditView) {
		_ppEditView = ppEditView;
	}

	ClipboardDataInfo getClipboadData();
	void addToClipboadHistory(ClipboardDataInfo cbd);
	int getClipboardDataIndex(const ClipboardDataInfo& cbd);

	void setBackgroundColor(COLORREF bgColour);
	void setForegroundColor(COLORREF fgColour);

	bool trackClipboardOps(bool bTrack) {
		bool bPreviousState = _isTrackingClipboardOps;
		_isTrackingClipboardOps = bTrack;
		return bPreviousState;
	}

	void pasteItem(int i); // upstream LBN_DBLCLK

	// NPLX: tests
	QListWidget* listBox() const { return _listBox; }
	size_t nbItems() const { return _clipboardDataInfos.size(); }

private:
	ScintillaEditView **_ppEditView = nullptr;
	std::vector<ClipboardDataInfo> _clipboardDataInfos;
	QListWidget* _listBox = nullptr;
	QColor _lbBgColor;
	QColor _lbFgColor;
	bool _isTrackingClipboardOps = true; // false when we do not want to track & show some Clipboard operations

	void onClipboardChanged(); // upstream WM_DRAWCLIPBOARD
	static QString displayString(const ClipboardDataInfo& cbd); // upstream StringArray + drawItem
	void updateColours();
};
