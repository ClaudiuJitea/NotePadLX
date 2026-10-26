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

#include "Printer.h"

#include <algorithm>
#include <string>

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QLocale>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QLineEdit>

#include "Parameters.h"
#include "RunDlg.h"
#include "ScintillaEditView.h"
#include "localization.h"

using namespace std;

namespace
{
	void replaceStr(wstring & str, const wstring& str2BeReplaced, const wstring& replacement)
	{
		size_t pos = str.find(str2BeReplaced);
		if (pos != str.npos)
			str.replace(pos, str2BeReplaced.length(), replacement);
	}

	// upstream ::CreateFont(MulDiv(fontSize, dpiY, 72), ..., weight, italic, ..., fontFace)
	QFont headerFooterFont(const wstring& fontName, int fontSize, int fontStyle)
	{
		QFont font(QString::fromStdWString(fontName.empty() ? L"Arial" : fontName));
		font.setPointSize(fontSize ? fontSize : 9);
		font.setBold(fontStyle & FONTSTYLE_BOLD);
		font.setItalic(fontStyle & FONTSTYLE_ITALIC);
		return font;
	}
}

void Printer::init(QWidget* parent, const ParentMessenger& sendToNpp, ScintillaEditView *pSEView, bool showDialog, size_t startPos, size_t endPos, bool isRTL)
{
	_parent = parent;
	_sendToNpp = sendToNpp;
	_pSEView = pSEView;
	_startPos = startPos;
	_endPos = endPos;
	_showDialog = showDialog;
	_isRTL = isRTL;
}

void Printer::polishPrintDialog(QPrintDialog& dlg, const Buffer* buffer)
{
	// NPLX: Qt's own (Unix) print dialog, as a Windows user knows PrintDlg: the page range, selection and copies are
	// shown, not hidden behind "Options >>", and print to file suggests a file name. Qt gives no API for either, so
	// both are looked up in the dialog and left alone when they are not there (another Qt version or platform).
	const QFileInfo file(QString::fromWCharArray(buffer->getFullPathName()));
	dlg.setWindowTitle(QPrintDialog::tr("Print") + QStringLiteral(" \u2014 ") + file.fileName());

	for (QDialogButtonBox* box : dlg.findChildren<QDialogButtonBox*>())
		for (QAbstractButton* button : box->buttons())
		{
			const QDialogButtonBox::ButtonRole role = box->buttonRole(button);
			if (role != QDialogButtonBox::AcceptRole && role != QDialogButtonBox::RejectRole)
				button->click(); // "Options >>"
		}

	// Qt names the PDF file only on X11; elsewhere it is a folder: suggest <document folder>/<name>.pdf
	if (QLineEdit* outputFile = dlg.findChild<QLineEdit*>(QStringLiteral("filename")))
	{
		if (outputFile->text().isEmpty() || outputFile->text().endsWith(QLatin1Char('/')))
		{
			const QString dir = buffer->isUntitled() ? QDir::homePath() : file.absolutePath();
			outputFile->setText(QDir(dir).filePath(file.completeBaseName() + QStringLiteral(".pdf")));
		}
	}
}

size_t Printer::doPrint()
{
	// upstream PRINTDLG: PD_ALLPAGES, PD_SELECTION when a range is selected, and PD_RETURNDEFAULT without the dialog
	QPrinter printer(QPrinter::HighResolution);
	printer.setDocName(QString::fromWCharArray(_pSEView->getCurrentBuffer()->getFullPathName()));
	if (!_showDialog)
	{
		// Don't display dialog box, just use the default printer and options
		const QPrinterInfo defaultPrinter = QPrinterInfo::defaultPrinter();
		if (!defaultPrinter.isNull())
			printer.setPrinterName(defaultPrinter.printerName());
		return doPrint(printer, true);
	}

	QPrintDialog dlg(&printer, _parent);
	dlg.setOption(QAbstractPrintDialog::PrintSelection, _startPos != _endPos);
	dlg.setOption(QAbstractPrintDialog::PrintPageRange, true);
	dlg.setOption(QAbstractPrintDialog::PrintToFile, true);
	dlg.setMinMax(1, 0xffff); // We do not know how many pages in the document until the printer is selected and the paper size is known.
	if (_startPos != _endPos)
		printer.setPrintRange(QPrinter::Selection);
	polishPrintDialog(dlg, _pSEView->getCurrentBuffer());
	if (dlg.exec() != QDialog::Accepted)
		return 0;

	const bool printSelection = (printer.printRange() == QPrinter::Selection);
	const bool printPageRange = (printer.printRange() == QPrinter::PageRange);
	return doPrint(printer, true, printSelection, printPageRange ? printer.fromPage() : 0, printPageRange ? printer.toPage() : 0);
}

size_t Printer::doPrint(QPagedPaintDevice& device, bool justDoIt, bool printSelection, int fromPage, int toPage)
{
	const NppGUI & nppGUI = (NppParameters::getInstance()).getNppGUI();

	// Get printer resolution
	const int dpiX = device.logicalDpiX(); // dpi in X direction
	const int dpiY = device.logicalDpiY(); // dpi in Y direction

	// Start by getting the physical page size (in device units).
	const QPageLayout layout = device.pageLayout();
	const QRect fullRect = layout.fullRectPixels(dpiY);
	const QRect paintRect = layout.paintRectPixels(dpiY);
	const QPoint ptPage(fullRect.width(), fullRect.height());

	// Get the dimensions of the unprintable part of the page (in device units): the painter's origin is the top left
	// of the printable part (upstream PHYSICALOFFSETX / Y, and the rest of the paper)
	struct { int left = 0, top = 0, right = 0, bottom = 0; } rectPhysMargins, rectMargins;
	rectPhysMargins.left = paintRect.left() - fullRect.left();
	rectPhysMargins.top = paintRect.top() - fullRect.top();
	rectPhysMargins.right = ptPage.x() - paintRect.width() - rectPhysMargins.left;
	rectPhysMargins.bottom = ptPage.y() - paintRect.height() - rectPhysMargins.top;

	if (nppGUI._printSettings.isUserMargePresent())
	{
		// the user margins are in mm
		const int left = nppGUI._printSettings._marge.left * 100 * dpiX / 2540;
		const int top = nppGUI._printSettings._marge.top * 100 * dpiY / 2540;
		const int right = nppGUI._printSettings._marge.right * 100 * dpiX / 2540;
		const int bottom = nppGUI._printSettings._marge.bottom * 100 * dpiY / 2540;

		rectMargins.left = std::max(rectPhysMargins.left, left);
		rectMargins.top = std::max(rectPhysMargins.top, top);
		rectMargins.right = std::max(rectPhysMargins.right, right);
		rectMargins.bottom = std::max(rectPhysMargins.bottom, bottom);
	}
	else
	{
		rectMargins = rectPhysMargins;
	}

	QPainter painter;
	if (!painter.begin(&device))
	{
		NativeLangSpeaker* pNativeSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
		pNativeSpeaker->messageBox("PrintError", L"Cannot start printer document.", L"", MB_OK);
		return 0;
	}

	const QFont fontHeader = headerFooterFont(nppGUI._printSettings._headerFontName, nppGUI._printSettings._headerFontSize, nppGUI._printSettings._headerFontStyle);
	const QFontMetrics fmHeader(fontHeader, &device);
	const int headerLineHeight = fmHeader.height() + fmHeader.leading(); // tmHeight + tmExternalLeading

	const QFont fontFooter = headerFooterFont(nppGUI._printSettings._footerFontName, nppGUI._printSettings._footerFontSize, nppGUI._printSettings._footerFontStyle);
	const QFontMetrics fmFooter(fontFooter, &device);
	const int footerLineHeight = fmFooter.height() + fmFooter.leading();

	int printMarge = footerLineHeight;
	printMarge = printMarge + printMarge / 2;

	// By default, we will print all the document
	size_t lengthPrinted = 0;
	size_t lengthDoc = _pSEView->getCurrentDocLen();
	size_t lengthDocMax = lengthDoc;

	// In the case that the print dialog was launched and that there's a range of selection
	// We print the range of selection
	if (printSelection)
	{
		if (_startPos > _endPos)
		{
			lengthPrinted = _endPos;
			lengthDoc = _startPos;
		}
		else
		{
			lengthPrinted = _startPos;
			lengthDoc = _endPos;
		}

		if (lengthDoc > lengthDocMax)
			lengthDoc = lengthDocMax;
	}

	Sci_RangeToFormatFull frPrint{};
	frPrint.hdc = static_cast<QPaintDevice*>(&device); // PlatQt: a SurfaceID is a QPaintDevice
	frPrint.hdcTarget = static_cast<QPaintDevice*>(&device);
	frPrint.rc.left = rectMargins.left - rectPhysMargins.left;
	frPrint.rc.top = rectMargins.top - rectPhysMargins.top;
	frPrint.rc.right = ptPage.x() - rectMargins.right - rectPhysMargins.left;
	frPrint.rc.bottom = ptPage.y() - rectMargins.bottom - rectPhysMargins.top;
	frPrint.rcPage.left = 0;
	frPrint.rcPage.top = 0;
	frPrint.rcPage.right = ptPage.x() - rectPhysMargins.left - rectPhysMargins.right - 1;
	frPrint.rcPage.bottom = ptPage.y() - rectPhysMargins.top - rectPhysMargins.bottom - 1;

	frPrint.rc.top += printMarge;
	frPrint.rc.bottom -= printMarge;
	frPrint.rc.left += printMarge;
	frPrint.rc.right -= printMarge;

	wstring headerL, headerM, headerR, footerL, footerM, footerR;

	const wchar_t shortDateVar[] = L"$(SHORT_DATE)";
	const wchar_t longDateVar[] = L"$(LONG_DATE)";
	const wchar_t timeVar[] = L"$(TIME)";

	// upstream GetDateFormat (DATE_SHORTDATE, DATE_LONGDATE) and GetTimeFormat (TIME_NOSECONDS) of the user's locale
	const QDateTime now = QDateTime::currentDateTime();
	const QLocale locale = QLocale::system();
	const wstring shortDate = locale.toString(now.date(), QLocale::ShortFormat).toStdWString();
	const wstring longDate = locale.toString(now.date(), QLocale::LongFormat).toStdWString();
	const wstring time = locale.toString(now.time(), QLocale::ShortFormat).toStdWString();

	auto expandPart = [&](const wstring& part) {
		if (part.empty())
			return wstring();
		wstring str = part;
		replaceStr(str, shortDateVar, shortDate);
		replaceStr(str, longDateVar, longDate);
		replaceStr(str, timeVar, time);
		return expandNppEnvironmentStrs(str, _sendToNpp);
	};

	if (nppGUI._printSettings.isHeaderPresent())
	{
		frPrint.rc.top += headerLineHeight + headerLineHeight / 2;
		headerL = expandPart(nppGUI._printSettings._headerLeft);
		headerM = expandPart(nppGUI._printSettings._headerMiddle);
		headerR = expandPart(nppGUI._printSettings._headerRight);
	}

	if (nppGUI._printSettings.isFooterPresent())
	{
		frPrint.rc.bottom -= footerLineHeight + footerLineHeight / 2;
		footerL = expandPart(nppGUI._printSettings._footerLeft);
		footerM = expandPart(nppGUI._printSettings._footerMiddle);
		footerR = expandPart(nppGUI._printSettings._footerRight);
	}

	bool isShown = _pSEView->hasMarginShown(ScintillaEditView::_SC_MARGE_LINENUMBER);
	if (!nppGUI._printSettings._printLineNumber)
		_pSEView->showMargin(ScintillaEditView::_SC_MARGE_LINENUMBER, false);

	int pageNum = 1;
	bool isFirstPrintedPage = true;
	const wstring pageVar = L"$(CURRENT_PRINTING_PAGE)";

	_pSEView->execute(SCI_SETPRINTCOLOURMODE, nppGUI._printSettings._printOption); // setting mode once is enough

	// the 3 parts of a header or footer line: left, middle (centred) and right, in black
	auto drawLine = [&](const QFont& font, const QFontMetrics& fm, const wstring& left, const wstring& middle, const wstring& right, const QRect& rcw, const QString& pageString) {
		painter.setFont(font);
		painter.setPen(Qt::black);
		auto withPage = [&pageVar, &pageString](wstring str) {
			size_t pos = str.find(pageVar);
			if (pos != str.npos)
				str.replace(pos, pageVar.length(), pageString.toStdWString());
			return QString::fromStdWString(str);
		};
		const Qt::LayoutDirection direction = _isRTL ? Qt::RightToLeft : Qt::LeftToRight;
		painter.setLayoutDirection(direction);
		if (!left.empty())
			painter.drawText(rcw.adjusted(5, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, withPage(left));
		if (!middle.empty())
			painter.drawText(rcw, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine, withPage(middle));
		if (!right.empty())
			painter.drawText(rcw, Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, withPage(right));
		(void)fm;
	};

	while (lengthPrinted < lengthDoc)
	{
		bool printPage = (fromPage <= 0 || pageNum >= fromPage) && (toPage <= 0 || pageNum <= toPage);

		if (!justDoIt)
			printPage = false;

		const QString pageString = QString::number(pageNum);

		if (printPage)
		{
			// upstream StartPage
			if (!isFirstPrintedPage)
				device.newPage();
			isFirstPrintedPage = false;

			if (nppGUI._printSettings.isHeaderPresent())
			{
				painter.save();
				const QRect rcw(QPoint(frPrint.rc.left, frPrint.rc.top - headerLineHeight - headerLineHeight / 2),
					QSize(frPrint.rc.right - frPrint.rc.left, headerLineHeight));
				drawLine(fontHeader, fmHeader, headerL, headerM, headerR, rcw, pageString);

				painter.setPen(QPen(Qt::black, 0));
				painter.drawLine(frPrint.rc.left, frPrint.rc.top - headerLineHeight / 4, frPrint.rc.right, frPrint.rc.top - headerLineHeight / 4);
				painter.restore();
			}
		}

		frPrint.chrg.cpMin = static_cast<Sci_Position>(lengthPrinted);
		frPrint.chrg.cpMax = static_cast<Sci_Position>(lengthDoc);

		Sci_Position foundFormFeed = -1;
		if (nppGUI._printSettings._printFormFeedPageBreak)
		{
			// look for form feed: if found, stop current page just before the FF (though scintilla will continue through the EOL if one comes immediately after FF)
			Sci_TextToFindFull ttf;
			ttf.chrg = frPrint.chrg;
			ttf.lpstrText = "\f";	// null terminated formfeed
			foundFormFeed = static_cast<Sci_Position>(_pSEView->execute(SCI_FINDTEXTFULL, static_cast<uptr_t>(SCFIND_NONE), reinterpret_cast<sptr_t>(&ttf)));

			// if FF is found, then stop the current page just before the FF by changing cpMax
			if (foundFormFeed > 0)
				frPrint.chrg.cpMax = foundFormFeed - 1;	// stop the current page just before the FF
			else if (foundFormFeed == 0)
				frPrint.chrg.cpMax = 0;	// but if FF was the first character, then just stop at that character (cannot stop before 0)
		}

		// actually send this page to Scintilla's print-page fomatter, whether or not FF is found
		painter.save();
		lengthPrinted = static_cast<size_t>(_pSEView->execute(SCI_FORMATRANGEFULL, printPage, reinterpret_cast<sptr_t>(&frPrint)));
		painter.restore();

		// if FF was found, _after_ we've done the print-formatting for the current page, make sure that the start of the _next_ page is after this FF
		//		(without this increment by 1, it will just print the same FF inifintitely)
		if ((foundFormFeed != -1) && (static_cast<Sci_Position>(lengthPrinted) <= foundFormFeed))
			lengthPrinted = foundFormFeed + 1;

		if (printPage && nppGUI._printSettings.isFooterPresent())
		{
			painter.save();
			const QRect rcw(QPoint(frPrint.rc.left, frPrint.rc.bottom + footerLineHeight / 2),
				QSize(frPrint.rc.right - frPrint.rc.left, footerLineHeight));
			drawLine(fontFooter, fmFooter, footerL, footerM, footerR, rcw, pageString);

			painter.setPen(QPen(Qt::black, 0));
			painter.drawLine(frPrint.rc.left, frPrint.rc.bottom + footerLineHeight / 4, frPrint.rc.right, frPrint.rc.bottom + footerLineHeight / 4);
			painter.restore();
		}

		++pageNum;

		if (toPage > 0 && pageNum > toPage)
			break;
	}

	if (!nppGUI._printSettings._printLineNumber)
		_pSEView->showMargin(ScintillaEditView::_SC_MARGE_LINENUMBER, isShown);

	_pSEView->execute(SCI_FORMATRANGEFULL, FALSE, 0);
	painter.end(); // upstream EndDoc

	return (static_cast<size_t>(pageNum) - 1);
}
