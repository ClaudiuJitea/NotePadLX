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

#include "DocumentMap.h"

#include <algorithm>

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "Buffer.h"
#include "NppConstants.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "menuCmdID.h"

static constexpr bool moveDown = true;
static constexpr bool moveUp = false;

DocumentMap::DocumentMap(QWidget* parent)
	: DockingPanel(QString::fromWCharArray(DM_PANELTITLE), IDM_VIEW_DOC_MAP, QStringLiteral("DocumentMap"), parent)
{
	NativeLangSpeaker *pNativeSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
	if (pNativeSpeaker)
		setWindowTitle(QString::fromStdWString(pNativeSpeaker->getAttrNameStr(DM_PANELTITLE, "DocumentMap", "PanelTitle")));

	_container = new QWidget(this);
	_container->installEventFilter(this);
	setWidget(_container);
}

void DocumentMap::init(ScintillaEditView **ppEditView)
{
	_ppEditView = ppEditView;

	// upstream WM_INITDIALOG: NPPM_CREATESCINTILLAHANDLE
	_pMapView = new ScintillaEditView(false, _container);
	_pMapView->init();
	_pMapView->execute(SCI_SETZOOM, static_cast<WPARAM>(-10), 0);
	_pMapView->execute(SCI_SETVSCROLLBAR, FALSE, 0);
	_pMapView->execute(SCI_SETHSCROLLBAR, FALSE, 0);
	_pMapView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF); // Turn off the modification event
	_pMapView->showIndentGuideLine(false);
	_pMapView->setFocusPolicy(Qt::NoFocus);
	_pMapView->show();

	// NPLX: the view zone exists before reloadMap() draws in it (upstream: drawZone checks its canvas handle)
	_vzDlg = new ViewZoneDlg(_container);
	_vzDlg->_mouseClicked = [this](int y) { mouseClicked(y); };
	_vzDlg->_scroll = [this](bool direction, moveMode mode) { scrollMap(direction, mode); };
	_vzDlg->_mouseWheel = [this](QWheelEvent* event) {
		// upstream DOCUMENTMAP_MOUSEWHEEL: the editor scrolls
		QApplication::sendEvent((*_ppEditView)->viewport(), event);
		scrollMap();
	};
	_vzDlg->show();
	_vzDlg->raise();

	reloadMap();
	setSyntaxHiliting();

	_pMapView->showMargin(0, false);
	_pMapView->showMargin(1, false);
	_pMapView->showMargin(2, false);
	_pMapView->showMargin(3, false);
}

void DocumentMap::display(bool toShow)
{
	DockingPanel::display(toShow);
	_vzDlg->setVisible(toShow);
}

void DocumentMap::reloadMap()
{
	if (_pMapView && _ppEditView)
	{
		Document currentDoc = (*_ppEditView)->execute(SCI_GETDOCPOINTER);
		_pMapView->execute(SCI_SETDOCPOINTER, 0, static_cast<LPARAM>(currentDoc));

		//
		// sync with the current document
		//

		Buffer *editBuf = (*_ppEditView)->getCurrentBuffer();
		_pMapView->setCurrentBuffer(editBuf);

		// folding
		std::vector<size_t> lineStateVector;
		(*_ppEditView)->getCurrentFoldStates(lineStateVector);
		_pMapView->syncFoldStateWith(lineStateVector);

		// Wrapping
		if ((*_ppEditView)->isWrap() && needToRecomputeWith())
		{
			wrapMap();
		}

		scrollMap();
	}
}

void DocumentMap::showInMapTemporarily(Buffer *buf2show, ScintillaEditView *fromEditView)
{
	if (_pMapView && fromEditView)
	{
		_pMapView->execute(SCI_SETDOCPOINTER, 0, static_cast<LPARAM>(buf2show->getDocument()));
		_pMapView->setCurrentBuffer(buf2show);

		// folding
		const std::vector<size_t> & lineStateVector = buf2show->getHeaderLineState(fromEditView);
		_pMapView->syncFoldStateWith(lineStateVector);

		// Wrapping
		if (fromEditView->isWrap() && needToRecomputeWith(fromEditView))
		{
			wrapMap(fromEditView);
		}

		MapPosition mp = buf2show->getMapPosition();
		if (mp.isValid())
			scrollMapWith(mp);
	}
}

void DocumentMap::setSyntaxHiliting()
{
	Buffer *buf = _pMapView->getCurrentBuffer();
	_pMapView->defineDocType(buf->getLangType());
	_pMapView->showMargin(ScintillaEditView::_SC_MARGE_FOLDER, false);
}

bool DocumentMap::needToRecomputeWith(const ScintillaEditView *editView)
{
	const ScintillaEditView *pEditView = editView ? editView : *_ppEditView;

	auto currentZoom = pEditView->execute(SCI_GETZOOM);
	if (_displayZoom != currentZoom)
		return true;

	int currentTextZoneWidth = pEditView->getTextZoneWidth();
	if (_displayWidth != currentTextZoneWidth)
		return true;

	return false;
}

void DocumentMap::initWrapMap()
{
	if (_pMapView && _ppEditView)
	{
		_pMapView->setGeometry(_container->rect());
		_pMapView->wrap(false);
		_pMapView->redraw();

		bool isRTL = (*_ppEditView)->isTextDirectionRTL();
		if (_pMapView->isTextDirectionRTL() != isRTL)
			_pMapView->changeTextDirection(isRTL);
	}
}

void DocumentMap::changeTextDirection(bool isRTL)
{
	_pMapView->changeTextDirection(isRTL);
}

/*
	The zoom ratios measured upstream (edit view width / map width for each editor zoom, the map being at -10):
		-10 .. -7 => 1, -6 => 1.5, -5 => 2, ... 0 => 4, ... 20 => 12
*/
static constexpr double zoomRatio[]{ 1, 1, 1, 1, 1.5, 2, 2.5, 2.5, 3.5, 3.5,
4, 4.5, 5, 5, 5.5, 6, 6.5, 7, 7, 7.5, 8, 8.5, 8.5, 9.5, 9.5, 10, 10.5, 11, 11, 11.5, 12 };

void DocumentMap::wrapMap(const ScintillaEditView *editView)
{
	const ScintillaEditView *pEditView = editView ? editView : *_ppEditView;
	if (pEditView->isWrap())
	{
		// get current scintilla width W1
		int editZoneWidth = pEditView->getTextZoneWidth();

		// update the wrap needed data
		_displayWidth = editZoneWidth;
		_displayZoom = pEditView->execute(SCI_GETZOOM);
		double zr = zoomRatio[std::clamp<intptr_t>(_displayZoom, -10, 20) + 10]; // NPLX: clamped (Scintilla allows wider zooms)

		// compute doc map width: dzw/ezw = 1/zoomRatio
		double docMapWidth = editZoneWidth / zr;

		_pMapView->setGeometry(0, 0, int(docMapWidth), _container->height());
		_pMapView->wrap(true);

		// sync wrapping indent mode
		_pMapView->execute(SCI_SETWRAPINDENTMODE, pEditView->execute(SCI_GETWRAPINDENTMODE));

		const ScintillaViewParams& svp = NppParameters::getInstance().getSVP();

		if (svp._paddingLeft || svp._paddingRight)
		{
			intptr_t paddingMapLeft = static_cast<intptr_t>(svp._paddingLeft / (editZoneWidth / docMapWidth));
			intptr_t paddingMapRight = static_cast<intptr_t>(svp._paddingRight / (editZoneWidth / docMapWidth));
			_pMapView->execute(SCI_SETMARGINLEFT, 0, paddingMapLeft);
			_pMapView->execute(SCI_SETMARGINRIGHT, 0, paddingMapRight);
		}
	}
	doMove();
}

void DocumentMap::scrollMap()
{
	if (_pMapView && _ppEditView)
	{
		// Get the position of the 1st and last showing chars from the original edit view
		const QRect rcEditView = (*_ppEditView)->viewport()->rect();
		LRESULT higherPos = (*_ppEditView)->execute(SCI_POSITIONFROMPOINT, 0, 0);
		LRESULT lowerPos = (*_ppEditView)->execute(SCI_POSITIONFROMPOINT, rcEditView.width(), rcEditView.height());

		// Let Scintilla scroll the map
		_pMapView->execute(SCI_GOTOPOS, higherPos);
		_pMapView->execute(SCI_GOTOPOS, lowerPos);

		// Get top position of orange marker window
		LRESULT higherY = _pMapView->execute(SCI_POINTYFROMPOSITION, 0, higherPos);

		// Get bottom position of orange marker window
		LRESULT lowerY = 0;
		LRESULT lineHeightMapView  = _pMapView->execute(SCI_TEXTHEIGHT, 0);
		if (!(*_ppEditView)->isWrap())
		{ // not wrapped: mimic height of edit view
			LRESULT lineHeightEditView = (*_ppEditView)->execute(SCI_TEXTHEIGHT, 0);
			lowerY = higherY + lineHeightMapView * rcEditView.height() / lineHeightEditView;
		}
		else
		{ // wrapped: ask Scintilla, since in the map view the current range of edit view might be wrapped differently
			lowerY = _pMapView->execute(SCI_POINTYFROMPOSITION, 0, lowerPos) + lineHeightMapView;
		}

		//
		// Mark view zone in map
		//
		_vzDlg->drawZone(static_cast<int32_t>(higherY), static_cast<int32_t>(lowerY));
	}
}

void DocumentMap::scrollMapWith(const MapPosition & mapPos)
{
	if (_pMapView)
	{
		// Visible document line for the map view
		auto firstVisibleDisplayLineMap = _pMapView->execute(SCI_GETFIRSTVISIBLELINE);
		auto firstVisibleDocLineMap = _pMapView->execute(SCI_DOCLINEFROMVISIBLE, firstVisibleDisplayLineMap);
		auto nbLineMap = _pMapView->execute(SCI_LINESONSCREEN, firstVisibleDocLineMap);
		auto lastVisibleDocLineMap = _pMapView->execute(SCI_DOCLINEFROMVISIBLE, firstVisibleDisplayLineMap + nbLineMap);

		// If part of editor view is out of map, then scroll map
		LRESULT mapLineToScroll = 0;
		if (lastVisibleDocLineMap < mapPos._lastVisibleDocLine)
			mapLineToScroll = mapPos._lastVisibleDocLine;
		else
			mapLineToScroll = mapPos._firstVisibleDocLine;
		//
		// Scroll to make whole view zone visible
		//
		_pMapView->execute(SCI_GOTOLINE, mapLineToScroll);

		// Get the editor's higher/lower Y, then compute the map's higher/lower Y
		LRESULT higherY = 0;
		LRESULT lowerY = 0;
		if (!mapPos._isWrap)
		{
			auto higherPos = _pMapView->execute(SCI_POSITIONFROMLINE, mapPos._firstVisibleDocLine);
			auto lowerPos = _pMapView->execute(SCI_POSITIONFROMLINE, mapPos._lastVisibleDocLine);
			higherY = _pMapView->execute(SCI_POINTYFROMPOSITION, 0, higherPos);
			lowerY = _pMapView->execute(SCI_POINTYFROMPOSITION, 0, lowerPos);
			if (lowerY == 0)
			{
				auto lineHeight = _pMapView->execute(SCI_TEXTHEIGHT, mapPos._firstVisibleDocLine);
				lowerY = mapPos._nbLine * lineHeight + mapPos._firstVisibleDocLine;
			}
		}
		else
		{
			higherY = _pMapView->execute(SCI_POINTYFROMPOSITION, 0, mapPos._higherPos);
			auto lineHeight = _pMapView->execute(SCI_TEXTHEIGHT, mapPos._firstVisibleDocLine);
			lowerY = mapPos._nbLine * lineHeight + higherY;
		}

		//
		// Mark view zone in map
		//
		_vzDlg->drawZone(static_cast<int32_t>(higherY), static_cast<int32_t>(lowerY));
	}
}

void DocumentMap::doMove()
{
	// upstream: the view zone window over the map's Scintilla window
	if (_vzDlg && _pMapView)
	{
		_vzDlg->setGeometry(_pMapView->geometry());
		_vzDlg->raise();
	}
}

void DocumentMap::fold(size_t line, bool foldOrNot)
{
	_pMapView->fold(line, foldOrNot, false);
}

void DocumentMap::foldAll(bool mode)
{
	_pMapView->foldAll(mode);
}

void DocumentMap::scrollMap(bool direction, moveMode whichMode)
{
	// Visible line for the code view
	auto firstVisibleDisplayLine = (*_ppEditView)->execute(SCI_GETFIRSTVISIBLELINE);
	auto nbLine = (*_ppEditView)->execute(SCI_LINESONSCREEN, firstVisibleDisplayLine);
	auto nbLine2go = (whichMode == perLine ? 1 : nbLine);
	(*_ppEditView)->execute(SCI_LINESCROLL, 0, (direction == moveDown) ? nbLine2go : -nbLine2go);

	scrollMap();
}

void DocumentMap::redraw()
{
	_pMapView->execute(SCI_COLOURISE, 0, -1);
	_pMapView->redraw();
}

void DocumentMap::mouseClicked(int newPosY)
{
	// upstream DOCUMENTMAP_MOUSECLICKED: the editor scrolls so that the zone centre moves to the click
	int currentCenterPosY = _vzDlg->getCurrentCenterPosY();
	intptr_t pixelPerLine = _pMapView->execute(SCI_TEXTHEIGHT, 0);
	if (pixelPerLine <= 0)
		return;
	intptr_t jumpDistance = newPosY - currentCenterPosY;
	intptr_t nbLine2jump = jumpDistance/pixelPerLine;
	(*_ppEditView)->execute(SCI_LINESCROLL, 0, nbLine2jump);

	scrollMap();
}

void DocumentMap::onSwitchIn()
{
	// upstream DMN_SWITCHIN
	if (!_pMapView)
		return;
	_vzDlg->show();
	reloadMap();
	setSyntaxHiliting();
}

bool DocumentMap::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == _container && event->type() == QEvent::Resize && _pMapView)
	{
		// upstream WM_SIZE
		if (!_pMapView->isWrap())
			_pMapView->setGeometry(_container->rect());
		else
			_pMapView->resize(_pMapView->width(), _container->height());
		doMove();
		scrollMap();
	}
	return DockingPanel::eventFilter(watched, event);
}

// ============================================================================
// ViewZoneDlg
// ============================================================================

COLORREF ViewZoneDlg::_focus = RGB(0xFF, 0x80, 0x00);
COLORREF ViewZoneDlg::_frost = RGB(0xFF, 0xFF, 0xFF);

ViewZoneDlg::ViewZoneDlg(QWidget* parent)
	: QWidget(parent)
{
	setFocusPolicy(Qt::ClickFocus);
	setCursor(Qt::PointingHandCursor);
}

void ViewZoneDlg::setColour(COLORREF colour2Set, ViewZoneColorIndex i)
{
	switch (i)
	{
		case ViewZoneColorIndex::focus:
		{
			_focus = colour2Set;
			break;
		}

		case ViewZoneColorIndex::frost:
		{
			_frost = colour2Set;
			break;
		}

		default:
			return;
	}
}

void ViewZoneDlg::paintEvent(QPaintEvent*)
{
	// upstream drawPreviewZone, on a window made 50/255 opaque (NppParameters::setTransparent(_vzDlg, 50))
	QPainter painter(this);
	painter.setOpacity(50.0 / 255.0);
	painter.fillRect(rect(), QColor(GetRValue(_frost), GetGValue(_frost), GetBValue(_frost)));
	QRect zone = rect();
	zone.setTop(_higherY);
	zone.setBottom(_lowerY);
	painter.fillRect(zone, QColor(GetRValue(_focus), GetGValue(_focus), GetBValue(_focus)));
}

void ViewZoneDlg::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton && _mouseClicked)
		_mouseClicked(static_cast<int>(event->position().y()));
}

void ViewZoneDlg::mouseMoveEvent(QMouseEvent* event)
{
	if ((event->buttons() & Qt::LeftButton) && _mouseClicked)
		_mouseClicked(static_cast<int>(event->position().y()));
}

void ViewZoneDlg::wheelEvent(QWheelEvent* event)
{
	//Have to perform the scroll first, because the first/last line do not get updated until after the scroll has been parsed
	if (_mouseWheel)
		_mouseWheel(event);
}

void ViewZoneDlg::keyPressEvent(QKeyEvent* event)
{
	if (!_scroll)
		return;
	switch (event->key())
	{
		case Qt::Key_Up:       _scroll(moveUp, perLine); break;
		case Qt::Key_Down:     _scroll(moveDown, perLine); break;
		case Qt::Key_PageUp:   _scroll(moveUp, perPage); break;
		case Qt::Key_PageDown: _scroll(moveDown, perPage); break;
		default:
			QWidget::keyPressEvent(event);
	}
}
