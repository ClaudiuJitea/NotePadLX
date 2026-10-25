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

// Phase 0 acceptance tests: vendored Scintilla (Qt) + Lexilla + Boost.Regex work together.

#include <QtTest>

#include <memory>

#include "ScintillaEditView.h"
#include "SciLexer.h"
#include "ILexer.h"
#include "Lexilla.h"

class TestPhase0 : public QObject
{
	Q_OBJECT

private:
	std::unique_ptr<ScintillaEditView> _view;

	int styleAt(intptr_t pos) const { return static_cast<int>(_view->execute(SCI_GETSTYLEAT, pos)); }

private slots:
	void init() { _view = std::make_unique<ScintillaEditView>(); }
	void cleanup() { _view.reset(); }

	void lexillaHasNppCustomLexers()
	{
		QVERIFY(GetLexerCount() >= 128);
		for (const char* name : { "cpp", "python", "xml", "user", "searchResult", "objc" })
		{
			Scintilla::ILexer5* lexer = CreateLexer(name);
			QVERIFY2(lexer, name);
			lexer->Release();
		}
	}

	void cppHighlighting()
	{
		const std::string code = "int main() { return 42; } // done\n";
		_view->setText(code);
		QVERIFY(_view->setLexerByName("cpp"));
		// Keyword lists come from langs.xml in the app (ScintillaEditView::defineDocType); set them directly here
		_view->execute(SCI_SETKEYWORDS, 0, reinterpret_cast<LPARAM>("return"));
		_view->execute(SCI_SETKEYWORDS, 1, reinterpret_cast<LPARAM>("int"));
		_view->execute(SCI_COLOURISE, 0, -1);
		QCOMPARE(QString::fromStdString(_view->lexerName()), QStringLiteral("cpp"));

		QCOMPARE(styleAt(code.find("int")), SCE_C_WORD2);       // type1 keyword
		QCOMPARE(styleAt(code.find("return")), SCE_C_WORD);     // instre1 keyword
		QCOMPARE(styleAt(code.find("42")), SCE_C_NUMBER);
		QCOMPARE(styleAt(code.find("{")), SCE_C_OPERATOR);
		QCOMPARE(styleAt(code.find("// done")), SCE_C_COMMENTLINE);
	}

	void cppFileStyles()
	{
		QFile file(QStringLiteral(NPLX_TEST_DATA_DIR "/sample.cpp"));
		QVERIFY(file.open(QIODevice::ReadOnly));
		_view->setText(file.readAll().toStdString());
		QVERIFY(_view->setLexerByName("cpp"));
		const std::string text = _view->getText();
		QCOMPARE(styleAt(text.find("#include")), SCE_C_PREPROCESSOR);
		QCOMPARE(styleAt(text.find("/* block")), SCE_C_COMMENT);
		QCOMPARE(styleAt(text.find("\"hello\"")), SCE_C_STRING);
	}

	void boostRegexIsTheEngine()
	{
		// Lookbehind is not supported by Scintilla's built-in regex engine, only by Boost.
		_view->setText("xbar foobar");
		intptr_t end = 0;
		QCOMPARE(_view->findRegex("(?<=foo)bar", 0, &end), intptr_t(8));
		QCOMPARE(end, intptr_t(11));
	}

	void boostRegexFeatures()
	{
		_view->setText("int x = 0x1F;\nthe the cat\nVALUE value\n");
		intptr_t end = 0;

		QCOMPARE(_view->findRegex("\\b\\w+\\b", 0, &end), intptr_t(0));          // word boundaries
		QCOMPARE(end, intptr_t(3));

		QCOMPARE(_view->findRegex("0x[[:xdigit:]]{2}", 0, &end), intptr_t(8));  // POSIX class + quantifier
		QCOMPARE(_view->findRegex("\\b(\\w+) \\1\\b", 0, &end), intptr_t(14));   // backreference "the the"
		QCOMPARE(end, intptr_t(21));

		QCOMPARE(_view->findRegex("value", 0, &end, true), intptr_t(32));         // match case
		QCOMPARE(_view->findRegex("value", 0, &end, false), intptr_t(26));        // ignore case
		QCOMPARE(_view->findRegex("^the", 0, &end), intptr_t(14));                // line anchor
		QCOMPARE(_view->findRegex("nothere", 0, &end), intptr_t(-1));
	}

	void boostRegexUtf8()
	{
		_view->setText("Z\xC3\xBCrich caf\xC3\xA9");   // "Zürich café"
		intptr_t end = 0;
		QCOMPARE(_view->findRegex("\xC3\xBC", 0, &end), intptr_t(1));  // ü is 2 bytes
		QCOMPARE(end, intptr_t(3));
		QCOMPARE(_view->findRegex("caf.$", 0, &end), intptr_t(8));     // '.' matches one code point
		QCOMPARE(end, intptr_t(13));
	}

	void boostRegexInvalidPatternReportsError()
	{
		_view->setText("abc");
		QCOMPARE(_view->findRegex("(abc", 0), intptr_t(-2));
		QVERIFY(!_view->regexErrorMessage().empty());
		qInfo("Boost error message: %s", _view->regexErrorMessage().c_str());

		// A valid search clears the message
		QCOMPARE(_view->findRegex("b", 0), intptr_t(1));
		QVERIFY(_view->regexErrorMessage().empty());
	}
};

QTEST_MAIN(TestPhase0)
#include "tst_phase0.moc"
