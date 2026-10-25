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

#include "DocTabView.h"

#include "NplxStyle.h"

#include <QBoxLayout>
#include <QMouseEvent>
#include <QStyle>
#include <QVariant>

#include "Parameters.h"

namespace
{
	BufferID bufferFromVariant(const QVariant& v)
	{
		return reinterpret_cast<BufferID>(v.value<quintptr>());
	}

	QVariant variantFromBuffer(BufferID id)
	{
		return QVariant::fromValue(reinterpret_cast<quintptr>(id));
	}
}

DocTabView::DocTabView(ScintillaEditView* pView, QWidget* parent)
	: QTabBar(parent), _pView(pView)
{
	setDocumentMode(true);
	setExpanding(false);
	setMovable(true);
	setTabsClosable(true);
	setUsesScrollButtons(true);
	setElideMode(Qt::ElideNone);
	setSelectionBehaviorOnRemove(QTabBar::SelectRightTab);

	loadIcons(false);

	connect(this, &QTabBar::currentChanged, this, [this](int index) {
		updateCloseButtons();
		if (_isSettingBuffer || index < 0)
			return;
		emit bufferSelected(getBufferByIndex(static_cast<size_t>(index)));
	});
	connect(this, &QTabBar::tabCloseRequested, this, [this](int index) {
		emit bufferCloseRequested(getBufferByIndex(static_cast<size_t>(index)));
	});
}

void DocTabView::loadIcons(bool alternate)
{
	// Tab icons: upstream states (saved, unsaved, read-only, system read-only, monitoring). NPLX: NotePadLX icons
	_isAlternateIcons = alternate;
	_savedIcon = nplx::style::tabIcon(QStringLiteral("tab_saved"), alternate);
	_unsavedIcon = nplx::style::tabIcon(QStringLiteral("tab_unsaved"), alternate);
	_readOnlyIcon = nplx::style::tabIcon(QStringLiteral("tab_readonly"), alternate);
	_readOnlySysIcon = nplx::style::tabIcon(QStringLiteral("tab_readonly_sys"), alternate);
	_monitoringIcon = nplx::style::tabIcon(QStringLiteral("tab_monitoring"), alternate);
}

void DocTabView::applyTabStatus()
{
	const int tabStatus = NppParameters::getInstance().getNppGUI()._tabStatus;

	setVisible(!(tabStatus & TAB_HIDE));
	setMovable(tabStatus & TAB_DRAGNDROP);

	// upstream TabBarPlus::doVertical: the tabs on the left of the view
	const bool isVertical = tabStatus & TAB_VERTICAL;
	setShape(isVertical ? QTabBar::RoundedWest : QTabBar::RoundedNorth);
	if (auto* box = qobject_cast<QBoxLayout*>(parentWidget() ? parentWidget()->layout() : nullptr))
		box->setDirection(isVertical ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);

	// the stylesheet draws the reduced tabs, the top bar of the active tab and the inactive tabs
	setProperty("nplxReduced", static_cast<bool>(tabStatus & TAB_REDUCE));
	setProperty("nplxTopBar", static_cast<bool>(tabStatus & TAB_DRAWTOPBAR));
	setProperty("nplxDrawInactive", static_cast<bool>(tabStatus & TAB_DRAWINACTIVETAB));
	style()->unpolish(this);
	style()->polish(this);

	const bool closeButtons = tabStatus & TAB_CLOSEBUTTON;
	if (closeButtons != tabsClosable())
		setTabsClosable(closeButtons);
	updateCloseButtons();

	const bool alternate = tabStatus & TAB_ALTICONS;
	if (alternate != _isAlternateIcons)
	{
		loadIcons(alternate);
		for (int i = 0; i < count(); ++i)
			setTabIcon(i, iconFor(MainFileManager.getBufferByID(getBufferByIndex(static_cast<size_t>(i)))));
	}
	update();
}

void DocTabView::refreshLabels()
{
	for (int i = 0; i < count(); ++i)
	{
		BufferID id = getBufferByIndex(static_cast<size_t>(i));
		if (id == BUFFER_INVALID)
			continue;
		Buffer* buf = MainFileManager.getBufferByID(id);
		buf->refreshCompactFileName();
		updateTab(i, buf);
	}
}

void DocTabView::updateCloseButtons()
{
	// upstream: without "Show close button on each inactive tab", only the active tab has one
	if (!tabsClosable())
		return;
	const bool onInactiveTabs = NppParameters::getInstance().getNppGUI()._tabStatus & TAB_INACTIVETABSHOWBUTTON;
	const auto side = static_cast<QTabBar::ButtonPosition>(style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, this));
	for (int i = 0; i < count(); ++i)
	{
		if (QWidget* button = tabButton(i, side))
			button->setVisible(onInactiveTabs || i == currentIndex());
	}
}

void DocTabView::tabInserted(int index)
{
	QTabBar::tabInserted(index);
	updateCloseButtons();
}

void DocTabView::mouseDoubleClickEvent(QMouseEvent* event)
{
	// upstream TAB_DBCLK2CLOSE: a double click on a tab closes it
	const int index = tabAt(event->position().toPoint());
	if (index >= 0 && event->button() == Qt::LeftButton && (NppParameters::getInstance().getNppGUI()._tabStatus & TAB_DBCLK2CLOSE))
	{
		emit bufferCloseRequested(getBufferByIndex(static_cast<size_t>(index)));
		return;
	}
	QTabBar::mouseDoubleClickEvent(event);
}

QIcon DocTabView::iconFor(const Buffer* buf) const
{
	// Same priority as upstream TabBarPlus: monitoring, then read-only, then dirty state
	if (buf->isMonitoringOn())
		return _monitoringIcon;
	if (buf->getFileReadOnly())
		return _readOnlySysIcon;
	if (buf->isReadOnly())
		return _readOnlyIcon;
	return buf->isDirty() ? _unsavedIcon : _savedIcon;
}

void DocTabView::updateTab(int index, Buffer* buf)
{
	// Upstream: tab label is the compact file name; '&' is doubled so it is not a mnemonic
	QString label = QString::fromWCharArray(buf->getCompactFileName());
	label.replace(QStringLiteral("&"), QStringLiteral("&&"));
	setTabText(index, label);
	setTabToolTip(index, QString::fromWCharArray(buf->getFullPathName()));
	setTabIcon(index, iconFor(buf));
}

void DocTabView::addBuffer(BufferID buffer)
{
	if (buffer == BUFFER_INVALID)	//valid only
		return;
	if (getIndexByBuffer(buffer) != -1)	//no duplicates
		return;
	Buffer* buf = MainFileManager.getBufferByID(buffer);

	_isSettingBuffer = true;
	const int index = addTab(QString());
	setTabData(index, variantFromBuffer(buffer));
	updateTab(index, buf);
	_isSettingBuffer = false;
}

void DocTabView::closeBuffer(BufferID buffer)
{
	const int index = getIndexByBuffer(buffer);
	if (index != -1)
		deletItemAt(static_cast<size_t>(index));
}

bool DocTabView::activateBuffer(BufferID buffer)
{
	const int indexToActivate = getIndexByBuffer(buffer);
	if (indexToActivate == -1)
		return false;	//cannot activate

	_isSettingBuffer = true;
	setCurrentIndex(indexToActivate);
	_isSettingBuffer = false;
	return true;
}

BufferID DocTabView::activeBuffer()
{
	const int index = currentIndex();
	return index < 0 ? BUFFER_INVALID : getBufferByIndex(static_cast<size_t>(index));
}

int DocTabView::getIndexByBuffer(BufferID id) const
{
	for (int i = 0; i < count(); ++i)
	{
		if (bufferFromVariant(tabData(i)) == id)
			return i;
	}
	return -1;
}

BufferID DocTabView::getBufferByIndex(size_t index) const
{
	if (index >= nbItem())
		return BUFFER_INVALID;
	return bufferFromVariant(tabData(static_cast<int>(index)));
}

void DocTabView::bufferUpdated(Buffer* buffer, int mask)
{
	const int index = getIndexByBuffer(buffer->getID());
	if (index == -1)
		return;

	if (mask & (BufferChangeReadonly | BufferChangeDirty | BufferChangeFilename | BufferChangeStatus))
		updateTab(index, buffer);
}

void DocTabView::setBuffer(size_t index, BufferID id)
{
	if (index >= nbItem())
		return;

	_isSettingBuffer = true;
	setTabData(static_cast<int>(index), variantFromBuffer(id));
	updateTab(static_cast<int>(index), MainFileManager.getBufferByID(id));
	_isSettingBuffer = false;
}

void DocTabView::deletItemAt(size_t index)
{
	if (index >= nbItem())
		return;

	_isSettingBuffer = true;
	removeTab(static_cast<int>(index));
	_isSettingBuffer = false;
}

BufferID DocTabView::findBufferByName(const wchar_t* fullfilename) const
{
	for (size_t i = 0; i < nbItem(); ++i)
	{
		BufferID id = getBufferByIndex(i);
		const Buffer* buf = MainFileManager.getBufferByID(id);
		if (wcscmp(fullfilename, buf->getFullPathName()) == 0) // NPLX: case-sensitive (upstream _wcsicmp)
		{
			return id;
		}
	}
	return BUFFER_INVALID;
}

void DocTabView::tabToStart()
{
	const int index = currentIndex();
	if (index > 0)
		moveTab(index, 0);
}

void DocTabView::tabToEnd()
{
	const int index = currentIndex();
	if (index >= 0 && index < count() - 1)
		moveTab(index, count() - 1);
}
