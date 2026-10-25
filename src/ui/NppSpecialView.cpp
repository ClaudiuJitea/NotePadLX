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

// NPLX: View > Toggle Full Screen Mode, Post-It and Distraction Free Mode (upstream Notepad_plus::fullScreenToggle,
// postItToggle, distractionFreeToggle). Upstream changes the Win32 window style (WS_POPUP) and placement; here
// QWidget::showFullScreen, a frameless always-on-top window, and the menu bar / toolbar / tab bars / status bar
// hidden. The restore button (upstream ButtonDlg) is a small button in the top-right corner.

#include "Notepad_plus.h"

#include <QDockWidget>
#include <QMenuBar>
#include <QResizeEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include "DockingPanel.h"
#include "DocTabView.h"
#include "NplxStyle.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "menuCmdID.h"

void Notepad_plus::placeRestoreButton()
{
	if (!_restoreButton)
		return;
	const QSize size = _restoreButton->sizeHint();
	_restoreButton->setGeometry(width() - size.width() - 8, 6, size.width(), size.height());
	_restoreButton->raise();
}

void Notepad_plus::resizeEvent(QResizeEvent* event)
{
	QMainWindow::resizeEvent(event);
	placeRestoreButton();
}

namespace
{
	void showRestoreButton(QToolButton*& button, QWidget* parent, const std::function<void()>& onClick)
	{
		// upstream _restoreButton.doDialog(): a small button to leave the mode
		if (!button)
		{
			button = new QToolButton(parent);
			button->setObjectName(QStringLiteral("restoreButton"));
			button->setIcon(nplx::style::icon(QStringLiteral("restore")));
			button->setIconSize(QSize(18, 18));
			button->setAutoRaise(false);
			button->setToolTip(QObject::tr("Restore"));
			button->setCursor(Qt::PointingHandCursor);
			QObject::connect(button, &QToolButton::clicked, parent, onClick);
		}
		button->show();
		button->raise();
	}
}

void Notepad_plus::fullScreenToggle()
{
	if (!_beforeSpecialView._isFullScreen)	//toggle fullscreen on
	{
		// ::GetWindowPlacement
		_beforeSpecialView._wasMaximized = isMaximized();
		_beforeSpecialView._normalGeometry = normalGeometry();

		//Setup GUI
		if (!_beforeSpecialView._isPostIt)
		{
			//only change the GUI if not already done by postit
			_beforeSpecialView._isMenuShown = menuBar()->isVisible();
			if (_beforeSpecialView._isMenuShown)
				menuBar()->hide();

			//Hide rebar
			_beforeSpecialView._isToolbarShown = _toolBar && _toolBar->isVisible();
			if (_toolBar)
				_toolBar->hide();
		}

		showFullScreen();

		// show restore button
		showRestoreButton(_restoreButton, this, [this]() {
			// upstream ButtonDlg: leave the active special mode
			if (_beforeSpecialView._isDistractionFree)
				command(IDM_VIEW_DISTRACTIONFREE);
			else if (_beforeSpecialView._isPostIt)
				command(IDM_VIEW_POSTIT);
			else if (_beforeSpecialView._isFullScreen)
				command(IDM_VIEW_FULLSCREENTOGGLE);
		});
		_pEditView->setFocus();
	}
	else	//toggle fullscreen off
	{
		if (_restoreButton && !_beforeSpecialView._isPostIt)
			_restoreButton->hide();

		//Setup GUI
		if (!_beforeSpecialView._isPostIt)
		{
			//only change the GUI if postit isn't active
			if (_beforeSpecialView._isMenuShown)
				menuBar()->show();

			//Show rebar
			if (_toolBar && _beforeSpecialView._isToolbarShown)
				_toolBar->show();
		}

		// ::SetWindowPlacement
		if (_beforeSpecialView._wasMaximized)
		{
			showMaximized();
		}
		else
		{
			showNormal();
			if (_beforeSpecialView._normalGeometry.isValid())
				setGeometry(_beforeSpecialView._normalGeometry);
		}
	}
	_beforeSpecialView._isFullScreen = !_beforeSpecialView._isFullScreen;
	checkMenuItem(IDM_VIEW_FULLSCREENTOGGLE, _beforeSpecialView._isFullScreen);
	placeRestoreButton();
}

void Notepad_plus::postItToggle()
{
	if (!_beforeSpecialView._isPostIt)	// PostIt disabled, enable it
	{
		const NppGUI & nppGUI = NppParameters::getInstance().getNppGUI();
		// get current status before switch to postIt
		//check these always
		{
			_beforeSpecialView._isAlwaysOnTop = isMenuItemChecked(IDM_VIEW_ALWAYSONTOP);
			_beforeSpecialView._isTabbarShown = _mainDocTab->isVisible() || _subDocTab->isVisible();
			_beforeSpecialView._isStatusbarShown = nppGUI._statusBarShow;
			if (nppGUI._statusBarShow)
				statusBar()->hide();
			if (_beforeSpecialView._isTabbarShown)
			{
				_mainDocTab->hide();
				_subDocTab->hide();
			}
			if (!_beforeSpecialView._isAlwaysOnTop)
				command(IDM_VIEW_ALWAYSONTOP);
		}
		//Only check these if not fullscreen
		if (!_beforeSpecialView._isFullScreen)
		{
			_beforeSpecialView._isMenuShown = menuBar()->isVisible();
			if (_beforeSpecialView._isMenuShown)
				menuBar()->hide();

			//Hide rebar
			_beforeSpecialView._isToolbarShown = _toolBar && _toolBar->isVisible();
			if (_toolBar)
				_toolBar->hide();

			// PostIt! (upstream WS_POPUP: no title bar)
			setWindowFlag(Qt::FramelessWindowHint, true);
			show();
		}

		// show restore button
		showRestoreButton(_restoreButton, this, [this]() {
			if (_beforeSpecialView._isDistractionFree)
				command(IDM_VIEW_DISTRACTIONFREE);
			else if (_beforeSpecialView._isPostIt)
				command(IDM_VIEW_POSTIT);
			else if (_beforeSpecialView._isFullScreen)
				command(IDM_VIEW_FULLSCREENTOGGLE);
		});
		_pEditView->setFocus();
	}
	else	//PostIt enabled, disable it
	{
		if (_restoreButton && !_beforeSpecialView._isFullScreen)
			_restoreButton->hide();

		//Setup GUI
		if (!_beforeSpecialView._isFullScreen)
		{
			//only change the these parts of GUI if not already done by fullscreen
			if (_beforeSpecialView._isMenuShown)
				menuBar()->show();

			//Show rebar
			if (_toolBar && _beforeSpecialView._isToolbarShown)
				_toolBar->show();
		}
		//Do this GUI config always
		if (_beforeSpecialView._isStatusbarShown)
			statusBar()->show();
		if (_beforeSpecialView._isTabbarShown)
		{
			_mainDocTab->show();
			_subDocTab->setVisible(viewVisible(SUB_VIEW));
		}
		if (!_beforeSpecialView._isAlwaysOnTop)
			command(IDM_VIEW_ALWAYSONTOP);

		//restore window style if not fullscreen
		if (!_beforeSpecialView._isFullScreen)
		{
			setWindowFlag(Qt::FramelessWindowHint, false);
			show();
		}
	}

	_beforeSpecialView._isPostIt = !_beforeSpecialView._isPostIt;
	checkMenuItem(IDM_VIEW_POSTIT, _beforeSpecialView._isPostIt);
	placeRestoreButton();
}

// Distraction Free mode uses full screen mode + post-it mode + setting padding on the both left & right sides.
// In order to keep the coherence of data, when full screen mode or (and) post-it mode is (are) active,
// Distraction Free mode should be innaccible, and vice versa.
void Notepad_plus::distractionFreeToggle()
{
	// Toggle Distraction Free Mode
	fullScreenToggle();
	postItToggle();

	// Get padding info
	const ScintillaViewParams& svp = NppParameters::getInstance().getSVP();
	int paddingLeft = 0;
	int paddingRight = 0;


	// Enable or disable Distraction Free Mode
	if (_beforeSpecialView._isDistractionFree) // disable it
	{
		// restore another view if 2 views mode was on
		if (_beforeSpecialView._was2ViewModeOn)
		{
			showView(otherView());
			_beforeSpecialView._was2ViewModeOn = false;
		}

		// restore dockable panels
		for (QDockWidget* i : _beforeSpecialView._pVisibleDockingContainers)
		{
			i->show();
		}

		// restore padding
		paddingLeft = svp._paddingLeft;
		paddingRight = svp._paddingRight;

		// hide restore button
		if (_restoreButton)
			_restoreButton->hide();
	}
	else // enable it
	{
		// check if 2 views mode is on
		ScintillaEditView* nonFocusedView = (otherView() == MAIN_VIEW) ? _mainEditView : _subEditView;
		if (nonFocusedView->isVisible())
		{
			hideView(otherView());
			_beforeSpecialView._was2ViewModeOn = true;
		}
		else
		{
			_beforeSpecialView._was2ViewModeOn = false;
		}

		// check if any dockable panel is visible
		_beforeSpecialView._pVisibleDockingContainers.clear();
		for (QDockWidget* i : findChildren<QDockWidget*>(Qt::FindDirectChildrenOnly))
		{
			if (i->isVisible())
			{
				_beforeSpecialView._pVisibleDockingContainers.push_back(i);
			}
		}

		for (QDockWidget* i : _beforeSpecialView._pVisibleDockingContainers)
		{
			i->hide();
		}

		// set padding
		paddingLeft = paddingRight = svp.getDistractionFreePadding(_pEditView->width());
	}

	_beforeSpecialView._isDistractionFree = !_beforeSpecialView._isDistractionFree;
	checkMenuItem(IDM_VIEW_DISTRACTIONFREE, _beforeSpecialView._isDistractionFree);

	// set Distraction Free Mode paddin or restore the normal padding
	_pEditView->execute(SCI_SETMARGINLEFT, 0, paddingLeft);
	_pEditView->execute(SCI_SETMARGINRIGHT, 0, paddingRight);
}
