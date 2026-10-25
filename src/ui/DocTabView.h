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

// NPLX: port of upstream ScintillaComponent/DocTabView (+ the parts of WinControls/TabBar used by it)
// on QTabBar. Each tab holds a BufferID; the API keeps the upstream names.

#pragma once

#include <QTabBar>
#include <QIcon>

#include "Buffer.h"

class ScintillaEditView;

class DocTabView : public QTabBar
{
	Q_OBJECT

public:
	explicit DocTabView(ScintillaEditView* pView, QWidget* parent = nullptr);

	void addBuffer(BufferID buffer);
	void closeBuffer(BufferID buffer);
	void bufferUpdated(Buffer* buffer, int mask);

	bool activateBuffer(BufferID buffer);
	BufferID activeBuffer();

	int getIndexByBuffer(BufferID id) const;
	BufferID getBufferByIndex(size_t index) const;
	BufferID findBufferByName(const wchar_t* fullfilename) const; // BUFFER_INVALID if not found

	void setBuffer(size_t index, BufferID id);
	void deletItemAt(size_t index); // upstream name
	size_t nbItem() const { return static_cast<size_t>(count()); }
	int getCurrentTabIndex() const { return currentIndex(); }
	int getItemCount() const { return count(); } // upstream TabBar::getItemCount
	void tabToStart(); // upstream TabBarPlus::tabToStart
	void tabToEnd();

	ScintillaEditView* getScintillaEditView() const { return _pView; }

	// upstream TabBarPlus settings (NppGUI::_tabStatus, Preferences > Tab Bar): hidden, vertical, reduced, drag and
	// drop, active tab top bar, inactive tabs, close buttons, double click to close, alternate icons
	void applyTabStatus();

	// upstream Buffer::refreshCompactFileName: the labels after a change of the label length
	void refreshLabels();

signals:
	// The user clicked a tab (upstream: TCN_SELCHANGE handled by Notepad_plus)
	void bufferSelected(BufferID id);
	// Close button or middle click (upstream: TCN_TABDELETE)
	void bufferCloseRequested(BufferID id);

protected:
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void tabInserted(int index) override;

private:
	void loadIcons(bool alternate);
	void updateCloseButtons();
	void updateTab(int index, Buffer* buf);
	QIcon iconFor(const Buffer* buf) const;

	ScintillaEditView* _pView = nullptr;
	bool _isSettingBuffer = false;
	QIcon _savedIcon;
	QIcon _unsavedIcon;
	QIcon _readOnlyIcon;
	QIcon _readOnlySysIcon;
	QIcon _monitoringIcon;
	bool _isAlternateIcons = false;
};
