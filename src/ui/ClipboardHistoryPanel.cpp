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

#include "ClipboardHistoryPanel.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QListWidget>
#include <QMimeData>

#include "Buffer.h"
#include "Common.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "menuCmdID.h"

#define MAX_DISPLAY_LENGTH 64

ClipboardHistoryPanel::ClipboardHistoryPanel(QWidget* parent)
	: DockingPanel(QString::fromWCharArray(CH_PROJECTPANELTITLE), IDM_EDIT_CLIPBOARDHISTORY_PANEL, QStringLiteral("ClipboardHistory"), parent)
{
	NativeLangSpeaker* pNativeSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
	if (pNativeSpeaker)
		setWindowTitle(QString::fromStdWString(pNativeSpeaker->getAttrNameStr(CH_PROJECTPANELTITLE, "ClipboardHistory", "PanelTitle")));

	_listBox = new QListWidget(this);
	_listBox->setFrameShape(QFrame::NoFrame);
	_listBox->setUniformItemSizes(true);
	setWidget(_listBox);

	// upstream WM_INITDIALOG: SetClipboardViewer
	connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() { onClipboardChanged(); });
	// upstream LBN_DBLCLK (NPLX: Enter too)
	connect(_listBox, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) { pasteItem(_listBox->row(item)); });
}

ClipboardDataInfo ClipboardHistoryPanel::getClipboadData()
{
	ClipboardDataInfo clipboardData;
	const QMimeData* mime = QApplication::clipboard()->mimeData();
	if (!mime || !mime->hasText())
		return clipboardData;

	const QByteArray utf8 = mime->text().toUtf8();
	clipboardData._data.assign(utf8.begin(), utf8.end());
	clipboardData._isBinaryContained = utf8.contains('\0'); // NPLX: upstream CF_NPPTEXTLEN (text with NULs)
	return clipboardData;
}

void ClipboardHistoryPanel::onClipboardChanged()
{
	if (_isTrackingClipboardOps)
	{
		ClipboardDataInfo clipboardData = getClipboadData();
		if (clipboardData._data.size())
		{
			addToClipboadHistory(clipboardData);
		}
	}
}

// Search clipboard data in internal storage
// return -1 if not found, else return the index of internal array
int ClipboardHistoryPanel::getClipboardDataIndex(const ClipboardDataInfo& cbd)
{
	for (size_t i = 0, len = _clipboardDataInfos.size() ; i < len ; ++i)
	{
		if (cbd._data == _clipboardDataInfos[i]._data)
			return static_cast<int32_t>(i);
	}
	return -1;
}

QString ClipboardHistoryPanel::displayString(const ClipboardDataInfo& cbd)
{
	// upstream StringArray(cbd, MAX_DISPLAY_LENGTH): the first bytes, "..." when cut; one line (DT_SINGLELINE)
	QString s = QString::fromUtf8(reinterpret_cast<const char*>(cbd._data.data()), static_cast<qsizetype>(cbd._data.size()));
	const bool isCompleted = s.size() <= MAX_DISPLAY_LENGTH;
	if (!isCompleted)
		s = s.left(MAX_DISPLAY_LENGTH - 3) + QStringLiteral("...");
	s.replace(QLatin1Char('\0'), QLatin1Char(' '));
	s.replace(QLatin1String("\r\n"), QStringLiteral(" "));
	s.replace(QLatin1Char('\n'), QLatin1Char(' '));
	s.replace(QLatin1Char('\r'), QLatin1Char(' '));
	s.replace(QLatin1Char('\t'), QLatin1Char(' '));
	return s;
}

void ClipboardHistoryPanel::addToClipboadHistory(ClipboardDataInfo cbd)
{
	int i = getClipboardDataIndex(cbd);
	if (i == 0) return;

	if (i != -1)
	{
		_clipboardDataInfos.erase(_clipboardDataInfos.begin() + i);
		delete _listBox->takeItem(i);
	}
	_clipboardDataInfos.insert(_clipboardDataInfos.begin(), cbd);
	auto* item = new QListWidgetItem(displayString(cbd));
	const QString full = QString::fromUtf8(reinterpret_cast<const char*>(cbd._data.data()), static_cast<qsizetype>(cbd._data.size()));
	item->setToolTip(full.left(1024)); // NPLX: the text in a tooltip
	_listBox->insertItem(0, item);
}

void ClipboardHistoryPanel::pasteItem(int i)
{
	if (i < 0 || static_cast<size_t>(i) >= _clipboardDataInfos.size())
		return;

	const ClipboardDataInfo& cbd = _clipboardDataInfos[i];
	ScintillaEditView* view = *_ppEditView;

	// upstream: UTF-16 converted to the document's code page (UTF-8, or the ANSI code page)
	int codepage = view->getCurrentBuffer()->getEncoding();
	if (codepage == -1)
	{
		auto cp = view->execute(SCI_GETCODEPAGE);
		codepage = cp == SC_CP_UTF8 ? SC_CP_UTF8 : 0;
	}
	else
		codepage = SC_CP_UTF8;

	std::string text(cbd._data.begin(), cbd._data.end());
	if (codepage != SC_CP_UTF8 && !cbd._isBinaryContained)
	{
		WcharMbcsConvertor& wmc = WcharMbcsConvertor::getInstance();
		int lenOut = 0;
		const char* converted = wmc.encode(SC_CP_UTF8, codepage, text.c_str(), static_cast<int>(text.size()), &lenOut);
		text.assign(converted, static_cast<size_t>(lenOut));
	}

	view->execute(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(""));
	view->execute(SCI_ADDTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()));
	view->setFocus();
}

void ClipboardHistoryPanel::setBackgroundColor(COLORREF bgColour)
{
	_lbBgColor = QColor(GetRValue(bgColour), GetGValue(bgColour), GetBValue(bgColour));
	updateColours();
}

void ClipboardHistoryPanel::setForegroundColor(COLORREF fgColour)
{
	_lbFgColor = QColor(GetRValue(fgColour), GetGValue(fgColour), GetBValue(fgColour));
	updateColours();
}

void ClipboardHistoryPanel::updateColours()
{
	// upstream WM_CTLCOLORLISTBOX / drawItem: the editor theme's default colours
	QString qss;
	if (_lbBgColor.isValid())
		qss += QStringLiteral("background: %1; ").arg(_lbBgColor.name());
	if (_lbFgColor.isValid())
		qss += QStringLiteral("color: %1; ").arg(_lbFgColor.name());
	_listBox->setStyleSheet(qss.isEmpty() ? QString() : QStringLiteral("QListView { %1}").arg(qss));
}
