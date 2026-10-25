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
#include <QMenuBar>
#include <QTemporaryDir>

#include <cstdlib>

#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "columnEditor.h"
#include "md5Dlgs.h"
#include "md5Dlgs_rc.h"
#include "columnEditor_rc.h"
#include "menuCmdID.h"
#include "resource.h"

namespace
{
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring&, const std::wstring&, unsigned int) override { return IDOK; }
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override { return IDNO; }
		std::vector<std::wstring> getOpenFileNames(const std::wstring&, const std::wstring&, const std::wstring&) override { return _filesAnswer; }
		std::vector<std::wstring> _filesAnswer;
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
};

QTEST_MAIN(TestTools)
#include "tst_tools.moc"
