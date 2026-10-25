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

// NPLX: port of upstream WinControls/DocumentMap/documentMap: a zoomed-out ScintillaEditView sharing the current
// document, with the "view zone" marking the part shown by the editor. Upstream's ViewZoneDlg is a separate
// translucent window (alpha 50) above the map; here it is a transparent child widget painting the zone with the same
// alpha, and receiving the clicks, drags, wheel and keys.

#pragma once

#include <functional>

#include <QWidget>

#include "DockingPanel.h"

class Buffer;
class ScintillaEditView;
struct MapPosition;

#define DM_PANELTITLE     L"Document Map"

enum moveMode {
	perLine,
	perPage
};

class ViewZoneDlg : public QWidget
{
	Q_OBJECT

public :
	explicit ViewZoneDlg(QWidget* parent = nullptr);

	enum class ViewZoneColorIndex {
		focus,
		frost
	};

	void drawZone(long hY, long lY) {
		_higherY = hY;
		_lowerY = lY;
		update();
	}

	int getViewerHeight() const {
		return (_lowerY - _higherY);
	}

	int getCurrentCenterPosY() const {
		return (_lowerY - _higherY)/2 + _higherY;
	}

	static void setColour(COLORREF colour2Set, ViewZoneColorIndex i);

	// NPLX: upstream sends DOCUMENTMAP_MOUSECLICKED / DOCUMENTMAP_SCROLL / DOCUMENTMAP_MOUSEWHEEL to the map
	std::function<void(int y)> _mouseClicked;
	std::function<void(bool direction, moveMode mode)> _scroll;
	std::function<void(QWheelEvent*)> _mouseWheel;

	long higherY() const { return _higherY; } // NPLX: tests
	long lowerY() const { return _lowerY; }

protected :
	void paintEvent(QPaintEvent* event) override; // upstream drawPreviewZone
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override; // upstream CanvasProc WM_KEYDOWN

	static COLORREF _focus;
	static COLORREF _frost;

private :
	long _higherY = 0;
	long _lowerY = 0;
};

class DocumentMap : public DockingPanel
{
	Q_OBJECT

public:
	explicit DocumentMap(QWidget* parent = nullptr);

	void init(ScintillaEditView **ppEditView);

	void display(bool toShow = true);
	void redraw();

	void vzDlgDisplay(bool toShow = true) {
		_vzDlg->setVisible(toShow);
	}

	void reloadMap();
	void showInMapTemporarily(Buffer *buf2show, ScintillaEditView *fromEditView);
	void wrapMap(const ScintillaEditView *editView = nullptr);
	void initWrapMap();
	void scrollMap();
	void scrollMap(bool direction, moveMode whichMode);
	void scrollMapWith(const MapPosition & mapPos);
	void doMove();
	void fold(size_t line, bool foldOrNot);
	void foldAll(bool mode);
	void setSyntaxHiliting();
	void changeTextDirection(bool isRTL);
	bool isTemporarilyShowing() const { return _isTemporarilyShowing; }
	void setTemporarilyShowing(bool tempShowing) { _isTemporarilyShowing = tempShowing; }

	// NPLX: tests
	ScintillaEditView* mapView() const { return _pMapView; }
	ViewZoneDlg* viewZone() const { return _vzDlg; }
	void mouseClicked(int newPosY); // upstream DOCUMENTMAP_MOUSECLICKED

protected:
	bool needToRecomputeWith(const ScintillaEditView *editView = nullptr);
	void onSwitchIn() override; // upstream DMN_SWITCHIN
	bool eventFilter(QObject* watched, QEvent* event) override; // upstream WM_SIZE of the panel

private:
	ScintillaEditView**_ppEditView = nullptr;
	ScintillaEditView*_pMapView = nullptr;
	ViewZoneDlg* _vzDlg = nullptr;
	QWidget* _container = nullptr;
	bool _isTemporarilyShowing = false;

	// for needToRecomputeWith function
	intptr_t _displayZoom = -1;
	intptr_t _displayWidth = 0;
};
