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

#include <QApplication>
#include <QBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QPaintEvent>
#include <QStyle>
#include <QVariant>

#include "NppDarkMode.h"
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

	// the theme's tab bar colours, unset while they are the stylers.xml defaults
	std::optional<QColor> g_tabColours[DocTabView::inactiveBg + 1];
	const COLORREF g_defaultTabColours[DocTabView::inactiveBg + 1] = {
		RGB(0x00, 0x00, 0x00), RGB(0xFA, 0xAA, 0x3C), RGB(0xFF, 0xCA, 0xB0), RGB(0x80, 0x80, 0x80), RGB(0xC0, 0xC0, 0xC0)
	};
	const DocTabView* g_focusedTabBar = nullptr;
	QList<DocTabView*> g_tabBars;
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
	g_tabBars.push_back(this);

	connect(this, &QTabBar::currentChanged, this, [this](int index) {
		updateCloseButtons();
		updatePinButtons();
		if (_isSettingBuffer || index < 0)
			return;
		emit bufferSelected(getBufferByIndex(static_cast<size_t>(index)));
	});
	connect(this, &QTabBar::tabCloseRequested, this, [this](int index) {
		emit bufferCloseRequested(getBufferByIndex(static_cast<size_t>(index)));
	});
}

DocTabView::~DocTabView()
{
	g_tabBars.removeAll(this);
	if (g_focusedTabBar == this)
		g_focusedTabBar = nullptr;
}

void DocTabView::setColour(COLORREF colour2Set, tabColourIndex i)
{
	if (i < activeText || i > inactiveBg)
		return;
	if (colour2Set == g_defaultTabColours[i])
		g_tabColours[i].reset();
	else
		g_tabColours[i] = QColor(GetRValue(colour2Set), GetGValue(colour2Set), GetBValue(colour2Set));
	for (DocTabView* tabBar : g_tabBars)
		tabBar->applyColours();
}

void DocTabView::setFocusedTabBar(const DocTabView* tabBar)
{
	if (g_focusedTabBar == tabBar)
		return;
	g_focusedTabBar = tabBar;
	if (g_tabColours[activeUnfocusedTop] || g_tabColours[activeFocusedTop])
	{
		for (DocTabView* t : g_tabBars)
			t->applyColours();
	}
}

void DocTabView::applyColours()
{
	// upstream TabBarPlus::drawItem: the top bar colours in both modes; the text and inactive background colours only
	// in light mode (dark mode uses its own). NPLX: a style sheet on the tab bar, over the application's tab rules
	const int tabStatus = NppParameters::getInstance().getNppGUI()._tabStatus;
	const bool isDarkMode = NppDarkMode::isEnabled();
	const bool isFocused = (g_focusedTabBar == nullptr || g_focusedTabBar == this);
	QString qss;

	const std::optional<QColor>& top = g_tabColours[isFocused ? activeFocusedTop : activeUnfocusedTop];
	if (top && (tabStatus & TAB_DRAWTOPBAR))
	{
		qss += QStringLiteral("QTabBar::tab:selected { border-bottom: 2px solid %1; }"
			"QTabBar::tab:left:selected { border-bottom: none; border-right: 2px solid %1; }").arg(top->name());
	}
	if (!isDarkMode)
	{
		if (const auto& c = g_tabColours[activeText])
			qss += QStringLiteral("QTabBar::tab:selected { color: %1; }").arg(c->name());
		if (const auto& c = g_tabColours[inactiveText])
			qss += QStringLiteral("QTabBar::tab:!selected { color: %1; }").arg(c->name());
		if (const auto& c = g_tabColours[inactiveBg]; c && (tabStatus & TAB_DRAWINACTIVETAB))
		{
			qss += QStringLiteral("QTabBar::tab:!selected { background: %1; }"
				"QTabBar::tab:!selected:hover { background: %2; }").arg(c->name(), c->darker(110).name());
		}
	}
	if (qss != styleSheet())
		setStyleSheet(qss);
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
	_pinIcon = nplx::style::icon(QStringLiteral("tab_pin"));
	_pinnedIcon = nplx::style::icon(QStringLiteral("tab_pinned"));
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
	updatePinButtons();
	applyColours();

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

void DocTabView::updatePinButtons()
{
	// upstream TabBarPlus: the pin button shows on the tabs that show a close button, and always on a pinned tab;
	// with "Show only pinned tab buttons", only on the pinned tabs. NPLX: on the other side of the tab from the
	// close button
	const int tabStatus = NppParameters::getInstance().getNppGUI()._tabStatus;
	const bool isPinEnabled = tabStatus & TAB_PINBUTTON;
	const bool isPinSimplest = tabStatus & TAB_SHOWONLYPINNEDBUTTON;
	const bool onInactiveTabs = tabStatus & TAB_INACTIVETABSHOWBUTTON;
	const auto closeSide = static_cast<QTabBar::ButtonPosition>(style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, this));
	const QTabBar::ButtonPosition side = (closeSide == QTabBar::RightSide) ? QTabBar::LeftSide : QTabBar::RightSide;

	for (int i = 0; i < count(); ++i)
	{
		const bool isPinned = isPinnedAt(i);
		auto* button = qobject_cast<QToolButton*>(tabButton(i, side));
		if (!isPinEnabled || (isPinSimplest && !isPinned))
		{
			if (button)
			{
				setTabButton(i, side, nullptr);
				button->deleteLater();
			}
			continue;
		}
		if (!button)
		{
			button = new QToolButton(this);
			button->setObjectName(QStringLiteral("nplxPinButton"));
			button->setAutoRaise(true);
			button->setFocusPolicy(Qt::NoFocus);
			button->setIconSize(QSize(14, 14));
			connect(button, &QToolButton::clicked, this, [this, button]() {
				for (int j = 0; j < count(); ++j)
				{
					if (tabButton(j, QTabBar::LeftSide) == button || tabButton(j, QTabBar::RightSide) == button)
					{
						emit tabPinClicked(j);
						return;
					}
				}
			});
			setTabButton(i, side, button);
		}
		button->setIcon(isPinned ? _pinnedIcon : _pinIcon);
		button->setToolTip(isPinned ? tr("Unpin Tab") : tr("Pin Tab"));
		button->setVisible(isPinned || onInactiveTabs || i == currentIndex());
	}
}

bool DocTabView::isPinnedAt(int index) const
{
	BufferID id = getBufferByIndex(static_cast<size_t>(index));
	return id != BUFFER_INVALID && MainFileManager.getBufferByID(id)->isPinned();
}

void DocTabView::keepPinnedTabsFirst()
{
	int firstUnpinned = 0;
	for (int i = 0; i < count(); ++i)
	{
		if (!isPinnedAt(i))
			continue;
		if (i != firstUnpinned)
			moveTab(i, firstUnpinned);
		++firstUnpinned;
	}
}

void DocTabView::setIndividualTabColour(BufferID bufferId, int colorId)
{
	if (bufferId != BUFFER_INVALID)
		bufferId->setDocColorId(colorId);
}

int DocTabView::getIndividualTabColourId(int tabIndex) const
{
	BufferID bufferId = getBufferByIndex(static_cast<size_t>(tabIndex));
	if (bufferId == BUFFER_INVALID)
		return -1;

	return bufferId->getDocColorId();
}

void DocTabView::paintEvent(QPaintEvent* event)
{
	QTabBar::paintEvent(event);

	// upstream TabBarPlus::drawItem: an inactive tab is filled with its individual colour, the active one has the
	// top bar in the saturated colour. NPLX: the stylesheet draws the tabs, the colours are drawn over them (a
	// translucent fill keeps the label readable)
	NppParameters& nppParam = NppParameters::getInstance();
	const bool isDarkMode = NppDarkMode::isEnabled();
	const bool drawTopBar = nppParam.getNppGUI()._tabStatus & TAB_DRAWTOPBAR;
	const bool isVertical = (shape() == QTabBar::RoundedWest);
	QPainter painter(this);
	for (int i = 0; i < count(); ++i)
	{
		const int colourId = getIndividualTabColourId(i);
		if (colourId < 0)
			continue;
		const QRect r = tabRect(i);
		if (!r.intersects(event->rect()))
			continue;
		const bool isSelected = (i == currentIndex());
		const COLORREF c = nppParam.getIndividualTabColor(colourId, isDarkMode, isSelected);
		QColor colour(GetRValue(c), GetGValue(c), GetBValue(c));
		if (isSelected)
		{
			if (!drawTopBar)
				continue;
			const int barSize = 3;
			painter.fillRect(isVertical ? QRect(r.right() - barSize + 1, r.top(), barSize, r.height())
				: QRect(r.left(), r.bottom() - barSize + 1, r.width(), barSize), colour);
		}
		else
		{
			colour.setAlpha(isDarkMode ? 110 : 140);
			painter.fillRect(r, colour);
		}
	}
}

void DocTabView::tabInserted(int index)
{
	QTabBar::tabInserted(index);
	updateCloseButtons();
	updatePinButtons();
}

void DocTabView::mousePressEvent(QMouseEvent* event)
{
	_isDragging = (event->button() == Qt::LeftButton) && tabAt(event->position().toPoint()) >= 0;
	_dragStartPos = event->position().toPoint();
	QTabBar::mousePressEvent(event);
}

void DocTabView::mouseReleaseEvent(QMouseEvent* event)
{
	const bool wasDragging = _isDragging && (event->button() == Qt::LeftButton)
		&& (event->position().toPoint() - _dragStartPos).manhattanLength() >= QApplication::startDragDistance();
	_isDragging = false;
	QTabBar::mouseReleaseEvent(event);
	if (wasDragging)
		keepPinnedTabsFirst();

	// upstream TabBarPlus: a tab dropped outside of the tab bar (TCN_TABDROPPEDOUTSIDE)
	if (wasDragging && !rect().contains(event->position().toPoint()))
		emit tabDroppedOutside(event->globalPosition().toPoint(), event->modifiers() & Qt::ControlModifier);
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

void DocTabView::tabToStart(int index)
{
	if (index < 0 || index >= count())
		index = currentIndex();
	if (index <= 0)
		return;
	const bool isPinned = isPinnedAt(index);
	int i = index;
	while (i > 0 && isPinnedAt(i - 1) == isPinned)
		--i;
	if (i != index)
		moveTab(index, i);
}

void DocTabView::tabToEnd(int index)
{
	if (index < 0 || index >= count())
		index = currentIndex();
	if (index < 0)
		return;
	const bool isPinned = isPinnedAt(index);
	int i = index;
	while (i < count() - 1 && isPinnedAt(i + 1) == isPinned)
		++i;
	if (i != index)
		moveTab(index, i);
}
