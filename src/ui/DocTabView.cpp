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

	// Tab icons: upstream states (saved, unsaved, read-only, system read-only, monitoring). NPLX: NotePadLX icons
	_savedIcon = nplx::style::icon(QStringLiteral("tab_saved"));
	_unsavedIcon = nplx::style::icon(QStringLiteral("tab_unsaved"));
	_readOnlyIcon = nplx::style::icon(QStringLiteral("tab_readonly"));
	_readOnlySysIcon = nplx::style::icon(QStringLiteral("tab_readonly_sys"));
	_monitoringIcon = nplx::style::icon(QStringLiteral("tab_monitoring"));

	connect(this, &QTabBar::currentChanged, this, [this](int index) {
		if (_isSettingBuffer || index < 0)
			return;
		emit bufferSelected(getBufferByIndex(static_cast<size_t>(index)));
	});
	connect(this, &QTabBar::tabCloseRequested, this, [this](int index) {
		emit bufferCloseRequested(getBufferByIndex(static_cast<size_t>(index)));
	});
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
