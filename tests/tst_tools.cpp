// This file is part of NotePadLX, a Linux port of Notepad++.
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

// Phase 7: the Tools menu (MD5 / SHA-1 / SHA-256 / SHA-512) and the Edit tools. NPLX_SCREENSHOT_DIR=<dir> saves
// screenshots.

#include <QtTest>
#include <QAction>
#include <QClipboard>
#include <QCryptographicHash>
#include <QComboBox>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QToolButton>
#include <QMenuBar>
#include <QTemporaryDir>

#include <cstdlib>

#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "AboutDlg.h"
#include "WordStyleDlg.h"
#include "columnEditor.h"
#include "md5Dlgs.h"
#include "md5Dlgs_rc.h"
#include "columnEditor_rc.h"
#include "FindCharsInRange.h"
#include "findCharsInRange_rc.h"
#include "menuCmdID.h"
#include "resource.h"

namespace
{
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring& message, const std::wstring&, unsigned int type) override
		{
			_lastMessage = message;
			++_nbMessages;
			return (type & 0x0F) == MB_YESNO ? IDNO : IDOK;
		}
		std::wstring _lastMessage;
		int _nbMessages = 0;
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override { return IDNO; }
		std::vector<std::wstring> getOpenFileNames(const std::wstring&, const std::wstring&, const std::wstring&) override { return _filesAnswer; }
		std::vector<std::wstring> _filesAnswer;
		std::wstring getOpenFileName(const std::wstring&, const std::wstring&, const std::wstring&) override { return _fileAnswer; }
		std::wstring getSaveFileNameEx(const std::wstring&, const std::wstring& filter, const std::wstring&,
			const std::wstring& defExt, const std::wstring& checkboxLabel, bool* checkboxState) override
		{
			_lastFilter = filter;
			_lastDefExt = defExt;
			_lastCheckboxLabel = checkboxLabel;
			if (checkboxState)
				*checkboxState = false;
			return _fileAnswer;
		}
		std::wstring _fileAnswer;
		std::wstring _lastFilter;
		std::wstring _lastDefExt;
		std::wstring _lastCheckboxLabel;
	};

	QString reference(QCryptographicHash::Algorithm algo, const QByteArray& data)
	{
		return QString::fromLatin1(QCryptographicHash::hash(data, algo).toHex());
	}

	struct HashCase { int _textCmd; int _filesCmd; int _clipCmd; hashType _ht; QCryptographicHash::Algorithm _algo; };
	const HashCase hashCases[] = {
		{ IDM_TOOL_MD5_GENERATE, IDM_TOOL_MD5_GENERATEFROMFILE, IDM_TOOL_MD5_GENERATEINTOCLIPBOARD, hash_md5, QCryptographicHash::Md5 },
		{ IDM_TOOL_SHA1_GENERATE, IDM_TOOL_SHA1_GENERATEFROMFILE, IDM_TOOL_SHA1_GENERATEINTOCLIPBOARD, hash_sha1, QCryptographicHash::Sha1 },
		{ IDM_TOOL_SHA256_GENERATE, IDM_TOOL_SHA256_GENERATEFROMFILE, IDM_TOOL_SHA256_GENERATEINTOCLIPBOARD, hash_sha256, QCryptographicHash::Sha256 },
		{ IDM_TOOL_SHA512_GENERATE, IDM_TOOL_SHA512_GENERATEFROMFILE, IDM_TOOL_SHA512_GENERATEINTOCLIPBOARD, hash_sha512, QCryptographicHash::Sha512 },
	};
}

class TestTools : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	ScintillaEditView* view() { return _npp->getActiveEditView(); }

	void screenshot(QWidget* w, const QString& name)
	{
		const QByteArray dir = qgetenv("NPLX_SCREENSHOT_DIR");
		if (!dir.isEmpty())
		{
			QCoreApplication::processEvents();
			w->grab().save(QString::fromUtf8(dir) + "/" + name);
		}
	}

	QAction* action(int id)
	{
		for (QAction* a : _npp->menuBar()->findChildren<QAction*>())
			if (a->data().toInt() == id)
				return a;
		return nullptr;
	}

	ColumnEditorDlg* openColumnEditor()
	{
		_npp->command(IDM_EDIT_COLUMNMODE);
		ColumnEditorDlg* dlg = _npp->getColumnEditorDlg();
		return (dlg && dlg->isVisible()) ? dlg : nullptr;
	}

	QString text() { return QString::fromStdString(view()->getText()); }

private slots:
	void initTestCase()
	{
		QVERIFY(_tmp.isValid());
		::setenv("XDG_CONFIG_HOME", (_tmp.path() + "/config").toUtf8().constData(), 1);
		::setenv("XDG_DATA_HOME", (_tmp.path() + "/share").toUtf8().constData(), 1);
		::setenv("NPLX_DATA_DIR", NPLX_TEST_SOURCE_DATA_DIR, 1);

		nplx::setUiServices(&_ui);
		NppParameters& nppParam = NppParameters::getInstance();
		nppParam.load();
		_nativeLangSpeaker.init(nppParam.getNativeLang());
		nppParam.setNativeLangSpeaker(&_nativeLangSpeaker);

		_npp = std::make_unique<Notepad_plus>();
		_npp->init();
		_npp->resize(1100, 700);
		_npp->show();
		QVERIFY(QTest::qWaitForWindowExposed(_npp.get()));
	}

	void cleanupTestCase()
	{
		_npp.reset();
		nplx::setUiServices(nullptr);
	}

	void hashAlgorithms()
	{
		// the upstream portable implementations (SHA-512: the NotePadLX replacement of the CryptoAPI call)
		const QByteArray inputs[] = { QByteArray(), "abc", QByteArray(1000, 'a'), QByteArray(111, 'x'), QByteArray(112, 'x'),
			QByteArray(128, 'x'), QStringLiteral("Grüße, 世界").toUtf8() };
		for (const HashCase& c : hashCases)
			for (const QByteArray& in : inputs)
				QCOMPARE(QString::fromStdString(hashToHex(c._ht, in.constData(), static_cast<size_t>(in.size()))), reference(c._algo, in));
	}

	void hashMenuItemsArePorted()
	{
		for (const HashCase& c : hashCases)
			for (int id : { c._textCmd, c._filesCmd, c._clipCmd })
			{
				QVERIFY2(Notepad_plus::isCommandPorted(id), qPrintable(QString::number(id)));
				QVERIFY(action(id) && action(id)->isEnabled());
			}
	}

	void hashFromText()
	{
		for (const HashCase& c : hashCases)
		{
			_npp->command(c._textCmd);
			auto* dlg = qobject_cast<HashFromTextDlg*>(_npp->getHashDlg(c._textCmd));
			QVERIFY(dlg && dlg->isVisible());

			// the digest of the UTF-8 text, updated while typing (EN_CHANGE)
			const QString text = QStringLiteral("Grüße\nabc");
			dlg->setItemText(IDC_HASH_TEXT_EDIT, text.toStdWString());
			dlg->command(IDC_HASH_TEXT_EDIT, RcNotifyEditChange);
			QCOMPARE(QString::fromStdWString(dlg->getItemText(IDC_HASH_RESULT_FOMTEXT_EDIT)), reference(c._algo, text.toUtf8()));

			// "Treat each line as a separate string"; an empty line stays empty
			dlg->setItemText(IDC_HASH_TEXT_EDIT, L"abc\n\nxyz");
			dlg->setChecked(IDC_HASH_EACHLINE_CHECK, true);
			dlg->command(IDC_HASH_EACHLINE_CHECK);
			QCOMPARE(QString::fromStdWString(dlg->getItemText(IDC_HASH_RESULT_FOMTEXT_EDIT)),
				reference(c._algo, "abc") + "\n\n" + reference(c._algo, "xyz") + "\n");

			dlg->command(IDC_HASH_FROMTEXT_TOCLIPBOARD_BUTTON);
			QCOMPARE(QGuiApplication::clipboard()->text(), QString::fromStdWString(dlg->getItemText(IDC_HASH_RESULT_FOMTEXT_EDIT)));
			if (c._ht == hash_sha256)
				screenshot(dlg, "hash-from-text.png");
			dlg->setChecked(IDC_HASH_EACHLINE_CHECK, false);
			dlg->command(IDCANCEL);
			QVERIFY(!dlg->isVisible());
		}
		QCOMPARE(_npp->getHashDlg(IDM_TOOL_SHA512_GENERATE)->windowTitle(), QStringLiteral("Generate SHA-512 digest"));
	}

	void hashFromFiles()
	{
		const QString a = _tmp.path() + "/a.bin";
		const QString b = _tmp.path() + "/b c.txt";
		QByteArray binary;
		for (int i = 0; i < 300; ++i)
			binary.append(static_cast<char>(i % 256)); // NUL bytes included
		for (const auto& [path, content] : { std::pair{ a, binary }, std::pair{ b, QByteArray("hello\n") } })
		{
			QFile f(path);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write(content);
		}
		_ui._filesAnswer = { a.toStdWString(), b.toStdWString() };

		for (const HashCase& c : hashCases)
		{
			_npp->command(c._filesCmd);
			auto* dlg = qobject_cast<HashFromFilesDlg*>(_npp->getHashDlg(c._filesCmd));
			QVERIFY(dlg && dlg->isVisible());
			dlg->command(IDC_HASH_FILEBROWSER_BUTTON);
			QCOMPARE(QString::fromStdWString(dlg->getItemText(IDC_HASH_PATH_EDIT)), a + "\n" + b + "\n");
			QCOMPARE(QString::fromStdWString(dlg->getItemText(IDC_HASH_RESULT_EDIT)),
				reference(c._algo, binary) + "  a.bin\n" + reference(c._algo, "hello\n") + "  b c.txt\n");
			if (c._ht == hash_sha512)
			{
				QCOMPARE(dlg->windowTitle(), QStringLiteral("Generate SHA-512 digest from files"));
				screenshot(dlg, "hash-from-files.png");
			}
			dlg->command(IDCANCEL);
		}
	}

	void hashSelectionIntoClipboard()
	{
		_npp->command(IDM_FILE_NEW);
		view()->execute(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("first line\nselect me\n"));
		view()->execute(SCI_SETSEL, 11, 20);
		for (const HashCase& c : hashCases)
		{
			QGuiApplication::clipboard()->clear();
			_npp->command(c._clipCmd);
			QCOMPARE(QGuiApplication::clipboard()->text(), reference(c._algo, "select me"));
		}

		// nothing selected: the clipboard is left alone
		view()->execute(SCI_SETEMPTYSELECTION, 3);
		QGuiApplication::clipboard()->setText(QStringLiteral("unchanged"));
		_npp->command(IDM_TOOL_MD5_GENERATEINTOCLIPBOARD);
		QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("unchanged"));
	}

	void columnEditorText()
	{
		QVERIFY(Notepad_plus::isCommandPorted(IDM_EDIT_COLUMNMODE));
		_npp->command(IDM_FILE_NEW);
		view()->execute(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("abcd\nx\nefgh"));
		view()->execute(SCI_GOTOPOS, 2); // line 0, column 2

		ColumnEditorDlg* dlg = openColumnEditor();
		QVERIFY(dlg);
		dlg->setChecked(IDC_COL_TEXT_RADIO, true);
		dlg->command(IDC_COL_TEXT_RADIO);
		QVERIFY(dlg->dlgItem(IDC_COL_TEXT_EDIT)->isEnabled());
		QVERIFY(!dlg->dlgItem(IDC_COL_INITNUM_EDIT)->isEnabled());

		dlg->setItemText(IDC_COL_TEXT_EDIT, L"|");
		dlg->command(IDC_COL_TEXT_EDIT, RcNotifyEditChange);
		QVERIFY(dlg->dlgItem(IDOK)->isEnabled());
		screenshot(dlg, "column-editor.png");
		dlg->command(IDOK);
		QVERIFY(!dlg->isVisible());
		// from the caret's column down to the last line; a short line is padded with spaces
		QCOMPARE(text(), QStringLiteral("ab|cd\nx |\nef|gh"));

		_npp->command(IDM_EDIT_UNDO); // one undo action
		QCOMPARE(text(), QStringLiteral("abcd\nx\nefgh"));
	}

	void columnEditorNumbers()
	{
		_npp->command(IDM_FILE_NEW);
		view()->execute(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("a\nb\nc\nd"));
		// a zero-width rectangular selection at column 1 of the 4 lines
		view()->execute(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE);
		view()->execute(SCI_SETRECTANGULARSELECTIONANCHOR, 1);
		view()->execute(SCI_SETRECTANGULARSELECTIONCARET, view()->execute(SCI_POSITIONFROMLINE, 3) + 1);

		ColumnEditorDlg* dlg = openColumnEditor();
		QVERIFY(dlg);
		dlg->setChecked(IDC_COL_NUM_RADIO, true);
		dlg->command(IDC_COL_NUM_RADIO);
		dlg->setChecked(IDC_COL_DEC_RADIO, true);
		dlg->command(IDC_COL_DEC_RADIO);
		dlg->comboBox(IDC_COL_LEADING_COMBO)->setCurrentIndex(1); // Zeros
		for (auto [id, value] : { std::pair{ IDC_COL_INITNUM_EDIT, L"9" }, std::pair{ IDC_COL_INCREASENUM_EDIT, L"1" }, std::pair{ IDC_COL_REPEATNUM_EDIT, L"1" } })
		{
			dlg->setItemText(id, value);
			dlg->command(id, RcNotifyEditChange);
		}
		dlg->command(IDOK);
		QCOMPARE(text(), QStringLiteral("a09\nb10\nc11\nd12"));

		// hexadecimal, upper case, repeat 2, on the lines below the caret (no selection)
		view()->execute(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("\n\n\n\n"));
		view()->execute(SCI_GOTOPOS, 0);
		dlg = openColumnEditor();
		dlg->setChecked(IDC_COL_HEX_RADIO, true);
		dlg->command(IDC_COL_HEX_RADIO);
		QVERIFY(dlg->dlgItem(IDC_COL_HEXUC_COMBO)->isEnabled());
		dlg->comboBox(IDC_COL_HEXUC_COMBO)->setCurrentIndex(1); // A-F
		dlg->comboBox(IDC_COL_LEADING_COMBO)->setCurrentIndex(0); // None
		for (auto [id, value] : { std::pair{ IDC_COL_INITNUM_EDIT, L"A" }, std::pair{ IDC_COL_INCREASENUM_EDIT, L"3" }, std::pair{ IDC_COL_REPEATNUM_EDIT, L"2" } })
		{
			dlg->setItemText(id, value);
			dlg->command(id, RcNotifyEditChange);
		}
		dlg->command(IDOK);
		QCOMPARE(text(), QStringLiteral("A \nA \nD \nD \n10")); // "None": left-aligned, padded to the widest number

		// an invalid digit for the base is refused: the field keeps its last valid value
		dlg = openColumnEditor();
		dlg->setItemText(IDC_COL_INITNUM_EDIT, L"XYZ");
		dlg->command(IDC_COL_INITNUM_EDIT, RcNotifyEditChange);
		QCOMPARE(QString::fromStdWString(dlg->getItemText(IDC_COL_INITNUM_EDIT)), QStringLiteral("A"));
		QCOMPARE(NppParameters::getInstance()._columnEditParam._initialNum, 10);
		dlg->command(IDCANCEL); // closes the tooltip first, if any
		dlg->command(IDCANCEL);
		QVERIFY(!dlg->isVisible());

		// the settings are kept for the next session (config.xml)
		const ColumnEditorParam& p = NppParameters::getInstance()._columnEditParam;
		QCOMPARE(p._mainChoice, activeNumeric);
		QVERIFY(p._formatChoice == NumBase::BASE_16_UPPERCASE);
		QCOMPARE(p._repeatNum, 2);
	}

	void findCharsInRange()
	{
		QVERIFY(Notepad_plus::isCommandPorted(IDM_SEARCH_FINDCHARINRANGE));
		_npp->command(IDM_FILE_NEW);
		// "é" is 2 bytes in UTF-8 (0xC3 0xA9), "\x01" is ASCII
		view()->execute(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("ab\xC3\xA9" "cd\x01" "e"));
		view()->execute(SCI_GOTOPOS, 0);

		_npp->command(IDM_SEARCH_FINDCHARINRANGE);
		FindCharsInRangeDlg* dlg = _npp->getFindCharsInRangeDlg();
		QVERIFY(dlg && dlg->isVisible());
		// upstream defaults: non-ASCII, down, the custom range disabled
		QVERIFY(dlg->isCheckedOrNot(IDC_NONASCCI_RADIO));
		QVERIFY(dlg->isCheckedOrNot(ID_FINDCHAR_DIRDOWN));
		QVERIFY(!dlg->dlgItem(IDC_RANGESTART_EDIT)->isEnabled());
		screenshot(dlg, "find-chars-in-range.png");

		auto sel = [this]() { return std::make_pair(view()->execute(SCI_GETSELECTIONSTART), view()->execute(SCI_GETSELECTIONEND)); };

		// non-ASCII: each byte of "é" in turn (one byte is selected, as upstream)
		dlg->command(ID_FINDCHAR_NEXT);
		QCOMPARE(sel(), std::make_pair(sptr_t(2), sptr_t(3)));
		dlg->command(ID_FINDCHAR_NEXT);
		QCOMPARE(sel(), std::make_pair(sptr_t(3), sptr_t(4)));
		// no wrap: nothing more after the caret
		dlg->setChecked(ID_FINDCHAR_WRAP, false);
		dlg->command(ID_FINDCHAR_NEXT);
		QCOMPARE(sel(), std::make_pair(sptr_t(3), sptr_t(4)));
		// wrap: back to the first one
		dlg->setChecked(ID_FINDCHAR_WRAP, true);
		dlg->command(ID_FINDCHAR_NEXT);
		QCOMPARE(sel(), std::make_pair(sptr_t(2), sptr_t(3)));

		// custom range 0-31, up with wrap from position 0: the control character at 6
		dlg->setChecked(IDC_MYRANGE_RADIO, true);
		dlg->command(IDC_MYRANGE_RADIO);
		QVERIFY(dlg->dlgItem(IDC_RANGESTART_EDIT)->isEnabled());
		dlg->setItemText(IDC_RANGEEND_EDIT, L"31");
		dlg->setChecked(ID_FINDCHAR_DIRUP, true);
		view()->execute(SCI_GOTOPOS, 0);
		dlg->command(ID_FINDCHAR_NEXT);
		QCOMPARE(view()->execute(SCI_GETCURRENTPOS), sptr_t(6));
		QCOMPARE(view()->execute(SCI_GETANCHOR), sptr_t(7));

		// an invalid range is reported and the selection is kept
		const int nb = _ui._nbMessages;
		dlg->setItemText(IDC_RANGESTART_EDIT, L"200");
		dlg->setItemText(IDC_RANGEEND_EDIT, L"100");
		dlg->command(ID_FINDCHAR_NEXT);
		QCOMPARE(_ui._nbMessages, nb + 1);
		QCOMPARE(_ui._lastMessage, std::wstring(L"You should type between 0 and 255."));
		QCOMPARE(view()->execute(SCI_GETCURRENTPOS), sptr_t(6));

		dlg->command(IDCANCEL);
		QVERIFY(!dlg->isVisible());
	}

	void importStyleThemes()
	{
		QVERIFY(Notepad_plus::isCommandPorted(IDM_SETTING_IMPORTSTYLETHEMES));
		ThemeSwitcher& themeSwitcher = NppParameters::getInstance().getThemeSwitcher();
		const size_t nbThemes = themeSwitcher.size();

		// the theme name comes from the file name
		const QString src = _tmp.path() + "/My Imported Theme.xml";
		QVERIFY(QFile::copy(QStringLiteral(NPLX_TEST_SOURCE_DATA_DIR) + "/themes/Zenburn.xml", src));

		_npp->command(IDM_LANGSTYLE_CONFIG_DLG); // the Style Configurator lists it when it is already created
		WordStyleDlg* styleDlg = _npp->getConfigStyleDlg();
		QVERIFY(styleDlg->isCreated());
		QComboBox* themeCombo = nullptr; // "Select theme"
		for (QComboBox* combo : styleDlg->findChildren<QComboBox*>())
			if (combo->findText(QStringLiteral("Zenburn")) >= 0)
				themeCombo = combo;
		QVERIFY(themeCombo);
		const int nbComboItems = themeCombo->count();

		_ui._filesAnswer = { src.toStdWString() };
		_npp->command(IDM_SETTING_IMPORTSTYLETHEMES);
		_ui._filesAnswer.clear();

		// copied into the user's themes folder (scanned at start-up) and added once
		const QString dest = QString::fromStdWString(NppParameters::getInstance().getUserPath()) + "/themes/My Imported Theme.xml";
		QVERIFY(QFile::exists(dest));
		QCOMPARE(themeSwitcher.size(), nbThemes + 1);
		QVERIFY(themeSwitcher.themeNameExists(L"My Imported Theme"));
		QCOMPARE(themeSwitcher.getElementFromIndex(themeSwitcher.size() - 1).second, dest.toStdWString());
		QCOMPARE(themeCombo->count(), nbComboItems + 1);
		QCOMPARE(themeCombo->itemText(nbComboItems), QStringLiteral("My Imported Theme"));

		// importing it again overwrites the file and does not add a second entry
		_ui._filesAnswer = { src.toStdWString() };
		_npp->command(IDM_SETTING_IMPORTSTYLETHEMES);
		_ui._filesAnswer.clear();
		QCOMPARE(themeSwitcher.size(), nbThemes + 1);
		styleDlg->hide();
	}

	void saveAndLoadSession()
	{
		// File > Save Session... then Load Session... (upstream fileSaveSession / fileLoadSession)
		QVERIFY(Notepad_plus::isCommandPorted(IDM_FILE_SAVESESSION));
		QVERIFY(Notepad_plus::isCommandPorted(IDM_FILE_LOADSESSION));
		const QString a = _tmp.filePath(QStringLiteral("session-a.txt"));
		const QString b = _tmp.filePath(QStringLiteral("session-b.txt"));
		for (const QString& fn : { a, b })
		{
			QFile f(fn);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("text\n");
		}
		const BufferID idA = _npp->doOpen(a.toStdWString());
		const BufferID idB = _npp->doOpen(b.toStdWString());
		QVERIFY(idA != BUFFER_INVALID && idB != BUFFER_INVALID);

		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		nppGUI._definedSessionExt = L"npp-session";
		const QString sessionFile = _tmp.filePath(QStringLiteral("work.npp-session"));
		_ui._fileAnswer = sessionFile.toStdWString();
		_npp->command(IDM_FILE_SAVESESSION);
		// the session extension is the default one and the first filter
		QCOMPARE(_ui._lastDefExt, std::wstring(L"npp-session"));
		QVERIFY(_ui._lastFilter.rfind(L"Session file (*.npp-session)", 0) == 0);
		QVERIFY(QFile::exists(sessionFile));
		QFile saved(sessionFile);
		QVERIFY(saved.open(QIODevice::ReadOnly));
		const QByteArray xml = saved.readAll();
		QVERIFY(xml.contains("session-a.txt"));
		QVERIFY(xml.contains("session-b.txt"));

		_npp->fileCloseAll(false);
		QCOMPARE(MainFileManager.getBufferFromName(a.toStdWString().c_str()), BUFFER_INVALID);

		_npp->command(IDM_FILE_LOADSESSION);
		_ui._fileAnswer.clear();
		QVERIFY(MainFileManager.getBufferFromName(a.toStdWString().c_str()) != BUFFER_INVALID);
		QVERIFY(MainFileManager.getBufferFromName(b.toStdWString().c_str()) != BUFFER_INVALID);
		nppGUI._definedSessionExt.clear();
		_npp->fileCloseAll(false);
	}

	void editPopupContextMenu()
	{
		QVERIFY(Notepad_plus::isCommandPorted(IDM_SETTING_EDITCONTEXTMENU));
		const int nb = _ui._nbMessages;
		_npp->command(IDM_SETTING_EDITCONTEXTMENU);
		// the restart warning, then contextMenu.xml of the user's settings is the current document
		QCOMPARE(_ui._nbMessages, nb + 1);
		QVERIFY(_ui._lastMessage.find(L"contextMenu.xml") != std::wstring::npos);
		const std::wstring& path = NppParameters::getInstance().getContextMenuPath();
		QCOMPARE(std::wstring(_npp->getCurrentBuffer()->getFullPathName()), path);
		QVERIFY(text().contains(QStringLiteral("<ScintillaContextMenu>")));
		_npp->command(IDM_FILE_CLOSE);
	}

	void aboutBoxShowsAllItsText()
	{
		_npp->command(IDM_ABOUT);
		AboutDlg* about = _npp->getAboutDlg();
		QVERIFY(about && about->isVisible());
		QCoreApplication::processEvents();
		// every label fits (the old QMessageBox::about cut the lines on the right)
		for (QLabel* label : about->findChildren<QLabel*>())
		{
			if (label->text().isEmpty() || label->wordWrap())
				continue;
			QVERIFY2(label->width() >= label->sizeHint().width(), qPrintable(label->text()));
		}
		bool hasVersion = false;
		for (QLabel* label : about->findChildren<QLabel*>())
			hasVersion |= label->text().contains(QStringLiteral("NotePadLX " NPLX_VERSION));
		QVERIFY(hasVersion);
		auto* licence = about->findChild<QPlainTextEdit*>(QStringLiteral("licenceEdit"));
		QVERIFY(licence && licence->toPlainText().contains(QStringLiteral("GNU General Public License")));
		QVERIFY(about->findChild<QPushButton*>(QStringLiteral("checkForUpdates")));
		screenshot(about, "about.png");

		// Debug Info from the About box
		about->findChild<QPushButton*>(QStringLiteral("debugInfo"))->click();
		DebugInfoDlg* debug = _npp->getDebugInfoDlg();
		QVERIFY(debug && debug->isVisible());
		const QString info = QString::fromStdWString(debug->debugInfo());
		for (const char* field : { "NotePadLX v", "Build time:", "Qt:", "Scintilla/Lexilla included:", "Settings:",
			"OS:", "Platform: offscreen", "Dark Mode:", "Multi-instance Mode:" })
			QVERIFY2(info.contains(QString::fromLatin1(field)), field);
		debug->hide();
		about->hide();

		_npp->command(IDM_CMDLINEARGUMENTS);
		QVERIFY(_npp->getCmdLineArgsDlg() && _npp->getCmdLineArgsDlg()->isVisible());
		_npp->getCmdLineArgsDlg()->hide();
	}

	void checkForUpdates()
	{
		QVERIFY(Notepad_plus::isCommandPorted(IDM_UPDATE_NPP));
		QVERIFY(action(IDM_UPDATE_NPP));
		QCOMPARE(action(IDM_UPDATE_NPP)->text(), QStringLiteral("Check for &Updates..."));
		QVERIFY(UpdateChecker::isNewer("v0.10", "0.2"));
		QVERIFY(UpdateChecker::isNewer("0.2.1", "0.2"));
		QVERIFY(!UpdateChecker::isNewer("v0.2", "0.2"));
		QVERIFY(!UpdateChecker::isNewer("0.1", "0.2"));
		QVERIFY(!UpdateChecker::isNewer("garbage", "0.2"));

		// the GitHub "latest release" answer, from a file
		auto latest = [this](const char* tag) {
			const QString path = _tmp.path() + "/latest.json";
			QFile f(path);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write(QByteArray("{\"tag_name\": \"") + tag + "\", \"html_url\": \"https://github.com/ClaudiuJitea/NotePadLX/releases/tag/" + tag + "\"}");
			f.close();
			::setenv("NPLX_UPDATE_URL", QUrl::fromLocalFile(path).toString().toUtf8().constData(), 1);
		};

		latest("v99.0");
		int nb = _ui._nbMessages;
		_npp->command(IDM_UPDATE_NPP);
		QTRY_COMPARE(_ui._nbMessages, nb + 1);
		QVERIFY(QString::fromStdWString(_ui._lastMessage).contains(QStringLiteral("NotePadLX 99.0 is available")));

		latest("v" NPLX_VERSION);
		nb = _ui._nbMessages;
		_npp->command(IDM_UPDATE_NPP);
		QTRY_COMPARE(_ui._nbMessages, nb + 1);
		QVERIFY(QString::fromStdWString(_ui._lastMessage).contains(QStringLiteral("is the latest version")));

		::setenv("NPLX_UPDATE_URL", QUrl::fromLocalFile(_tmp.path() + "/missing.json").toString().toUtf8().constData(), 1);
		nb = _ui._nbMessages;
		_npp->command(IDM_UPDATE_NPP);
		QTRY_COMPARE(_ui._nbMessages, nb + 1);
		QVERIFY(QString::fromStdWString(_ui._lastMessage).contains(QStringLiteral("could not be checked")));
		::unsetenv("NPLX_UPDATE_URL");
	}

	void editOnSelectionAndPasteSpecial()
	{
		_npp->command(IDM_FILE_NEW);

		// Redact Selection: every character but the line ends
		view()->setText("secret\nkey");
		view()->execute(SCI_SETSEL, 0, 10);
		QVERIFY(Notepad_plus::isCommandPorted(IDM_EDIT_REDACT_SELECTION));
		_npp->command(IDM_EDIT_REDACT_SELECTION);
		QCOMPARE(text(), QStringLiteral("██████\n███"));

		// Copy / Paste Binary Content keeps the NUL bytes
		const char bin[] = { 'a', '\0', 'b' };
		view()->execute(SCI_CLEARALL);
		view()->execute(SCI_ADDTEXT, 3, reinterpret_cast<sptr_t>(bin));
		view()->execute(SCI_SELECTALL);
		_npp->command(IDM_EDIT_COPY_BINARY);
		view()->execute(SCI_DOCUMENTEND);
		_npp->command(IDM_EDIT_PASTE_BINARY);
		QCOMPARE(view()->execute(SCI_GETLENGTH), 6);
		QCOMPARE(static_cast<char>(view()->execute(SCI_GETCHARAT, 4)), '\0');

		// Paste HTML Content: the HTML source
		auto* mime = new QMimeData;
		mime->setHtml(QStringLiteral("<b>bold</b>"));
		QGuiApplication::clipboard()->setMimeData(mime);
		view()->execute(SCI_CLEARALL);
		_npp->command(IDM_EDIT_PASTE_AS_HTML);
		QVERIFY(text().contains(QStringLiteral("<b>bold</b>")));

		// Open File: the file name under the caret, relative to the document's folder
		const QString dir = _tmp.path() + "/open";
		QDir().mkpath(dir);
		const QString target = dir + "/target.txt";
		const QString source = dir + "/source.txt";
		for (const QString& f : { target, source })
		{
			QFile file(f);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write(f == source ? "see target.txt here\n" : "the target\n");
		}
		_npp->switchToFile(_npp->doOpen(source.toStdWString()));
		view()->execute(SCI_GOTOPOS, 6); // in "target.txt"
		_npp->command(IDM_EDIT_OPENSELECTEDFILETOEDIT);
		QCOMPARE(QString::fromStdWString(view()->getCurrentBuffer()->getFullPathName()), target);
	}

	void fullScreenPostItDistractionFree()
	{
		_npp->command(IDM_FILE_NEW);
		for (int id : { IDM_VIEW_FULLSCREENTOGGLE, IDM_VIEW_POSTIT, IDM_VIEW_DISTRACTIONFREE })
			QVERIFY(Notepad_plus::isCommandPorted(id));

		_npp->command(IDM_VIEW_FULLSCREENTOGGLE);
		QVERIFY(_npp->specialView()._isFullScreen);
		QVERIFY(_npp->isFullScreen());
		QVERIFY(!_npp->menuBar()->isVisible());
		QVERIFY(_npp->restoreButton() && _npp->restoreButton()->isVisible());
		_npp->restoreButton()->click(); // the restore button leaves the mode
		QVERIFY(!_npp->specialView()._isFullScreen);
		QVERIFY(_npp->menuBar()->isVisible());
		QVERIFY(!_npp->restoreButton()->isVisible());

		_npp->command(IDM_VIEW_POSTIT);
		QVERIFY(_npp->specialView()._isPostIt);
		QVERIFY(_npp->windowFlags() & Qt::FramelessWindowHint);
		QVERIFY(!_npp->statusBar()->isVisible());
		QVERIFY(!_npp->menuBar()->isVisible());
		_npp->command(IDM_VIEW_POSTIT);
		QVERIFY(!(_npp->windowFlags() & Qt::FramelessWindowHint));
		QVERIFY(_npp->statusBar()->isVisible());
		QVERIFY(_npp->menuBar()->isVisible());

		// Distraction Free: full screen + post-it + side padding; F11 / F12 do nothing meanwhile
		_npp->command(IDM_VIEW_DISTRACTIONFREE);
		QVERIFY(_npp->specialView()._isDistractionFree && _npp->specialView()._isFullScreen && _npp->specialView()._isPostIt);
		QVERIFY(view()->execute(SCI_GETMARGINLEFT) > 0);
		_npp->command(IDM_VIEW_FULLSCREENTOGGLE);
		QVERIFY(_npp->specialView()._isFullScreen);
		_npp->command(IDM_VIEW_DISTRACTIONFREE);
		QVERIFY(!_npp->specialView()._isDistractionFree && !_npp->specialView()._isFullScreen && !_npp->specialView()._isPostIt);
		QCOMPARE(view()->execute(SCI_GETMARGINLEFT), static_cast<intptr_t>(NppParameters::getInstance().getSVP()._paddingLeft));
		QVERIFY(_npp->menuBar()->isVisible());
	}
};

QTEST_MAIN(TestTools)
#include "tst_tools.moc"
