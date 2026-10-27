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

#include <optional>

#include "Buffer.h"

class ScintillaEditView;

class DocTabView : public QTabBar
{
	Q_OBJECT

public:
	explicit DocTabView(ScintillaEditView* pView, QWidget* parent = nullptr);
	~DocTabView() override;

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
	// upstream TabBarPlus::tabToStart / tabToEnd (the current tab by default): a tab does not pass a tab of the other
	// pinned state, so the pinned tabs stay first
	void tabToStart(int index = -1);
	void tabToEnd(int index = -1);
	// upstream pin button (Preferences > Tab Bar: "Enable pin tab feature", "Show only pinned tab buttons")
	void updatePinButtons();

	ScintillaEditView* getScintillaEditView() const { return _pView; }

	// upstream TabBarPlus settings (NppGUI::_tabStatus, Preferences > Tab Bar): hidden, vertical, reduced, drag and
	// drop, active tab top bar, inactive tabs, close buttons, double click to close, alternate icons
	void applyTabStatus();

	// upstream Buffer::refreshCompactFileName: the labels after a change of the label length
	void refreshLabels();

	// upstream individual tab colours (View > Tab > Apply Color 1-5): the colour id of the buffer, -1 for none
	// upstream TabBarPlus::setColour: the tab bar styles of the theme (Style Configurator > Global Styles > "Active tab
	// focused indicator", ...). NPLX: a colour left at its stylers.xml default keeps the NotePadLX look
	enum tabColourIndex {
		activeText, activeFocusedTop, activeUnfocusedTop, inactiveText, inactiveBg
	};
	static void setColour(COLORREF colour2Set, tabColourIndex i);
	// upstream NPPM_INTERNAL_ISFOCUSEDTAB: the tab bar of the active view draws the focused indicator
	static void setFocusedTabBar(const DocTabView* tabBar);
	void applyColours();

	static void setIndividualTabColour(BufferID bufferId, int colorId);
	int getIndividualTabColourId(int tabIndex) const;

signals:
	// The user clicked a tab (upstream: TCN_SELCHANGE handled by Notepad_plus)
	void bufferSelected(BufferID id);
	// Close button or middle click (upstream: TCN_TABDELETE)
	void bufferCloseRequested(BufferID id);
	// a tab dragged out of the tab bar and dropped (upstream TCN_TABDROPPEDOUTSIDE): where, and if Ctrl was down
	void tabDroppedOutside(const QPoint& globalPos, bool isCtrlDown);
	// the pin button of a tab (upstream TCN_TABPINNED)
	void tabPinClicked(int index);

protected:
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void tabInserted(int index) override;
	void tabRemoved(int index) override;

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
	QIcon _pinIcon;
	QIcon _pinnedIcon;
	bool _isAlternateIcons = false;
	QPoint _dragStartPos; // the press of a tab drag (upstream _draggingPoint)
	bool _isDragging = false;

	bool isPinnedAt(int index) const;
	void keepPinnedTabsFirst(); // after a drag: upstream refuses to swap a pinned and an unpinned tab
};
