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

// Phase 1 acceptance tests: the toolkit-free core (encodings, Utf8_16, Common, NppParameters).

#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <string>

#include "Common.h"
#include "Encoding.h"
#include "EncodingMapper.h"
#include "Parameters.h"
#include "Utf8_16.h"
#include "localization.h"

namespace
{
	std::string readBytes(const std::wstring& path)
	{
		std::ifstream f(nplx::toNativePath(path), std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}

	void writeBytes(const std::wstring& path, const std::string& bytes)
	{
		std::ofstream f(nplx::toNativePath(path), std::ios::binary);
		f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}

	// Reads raw file bytes the way upstream FileManager::loadFileData does: Utf8_16_Read -> UTF-8
	std::string decodeLikeLoader(const std::string& raw, UniMode& detected)
	{
		Utf8_16_Read reader;
		std::string buf = raw;
		const size_t newLen = reader.convert(buf.data(), buf.size());
		detected = reader.getEncoding();
		return std::string(reader.getNewBuf(), newLen);
	}

	// Writes UTF-8 text the way upstream FileManager::saveBuffer does: Utf8_16_Write
	std::string encodeLikeSaver(const std::string& utf8, UniMode mode, const std::wstring& path)
	{
		Utf8_16_Write writer;
		writer.setEncoding(mode);
		if (!writer.openFile(path.c_str()))
			return {};
		writer.writeFile(utf8.data(), utf8.size());
		writer.closeFile();
		return readBytes(path);
	}
}

class TestCore : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	std::wstring _tmpDir;

	std::wstring tmpFile(const wchar_t* name) const
	{
		std::wstring p = _tmpDir;
		pathAppend(p, name);
		return p;
	}

private slots:
	void initTestCase()
	{
		QVERIFY(_tmp.isValid());
		_tmpDir = nplx::fromNativePath(_tmp.path().toStdString());

		// Isolated settings: NppParameters reads/writes ~/.config/notepadlx -> <tmp>/config/notepadlx
		::setenv("XDG_CONFIG_HOME", (_tmp.path() + "/config").toUtf8().constData(), 1);
		::setenv("XDG_DATA_HOME", (_tmp.path() + "/share").toUtf8().constData(), 1);
		::setenv("NPLX_DATA_DIR", NPLX_TEST_SOURCE_DATA_DIR, 1);
	}

	// ------------------------------------------------------------------ encodings (iconv)

	void encodingConversions()
	{
		using namespace nplx::encoding;

		// UTF-8 <-> wchar_t (UTF-32 on Linux)
		const std::string utf8 = "Z\xC3\xBCrich \xE2\x82\xAC \xF0\x9F\x98\x80"; // "Zürich € 😀"
		const std::wstring w = toWide(utf8.data(), utf8.size(), CP_UTF8);
		QCOMPARE(w.size(), size_t(10));
		QCOMPARE(int(w[1]), 0xFC);
		QCOMPARE(int(w[9]), 0x1F600); // one wchar_t, no surrogate pair
		QCOMPARE(fromWide(w.data(), w.size(), CP_UTF8), utf8);

		// Windows code pages used by upstream EncodingMapper
		std::string out;
		QVERIFY(convert(CP_UTF8, 1252, "caf\xC3\xA9 \xE2\x82\xAC", 9, out));
		QCOMPARE(out, std::string("caf\xE9 \x80"));
		QVERIFY(convert(1251, CP_UTF8, "\xCF\xF0\xE8\xE2\xE5\xF2", 6, out)); // "Привет"
		QCOMPARE(out, std::string("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"));
		QVERIFY(convert(932, CP_UTF8, "\x93\xFA\x96\x7B", 4, out)); // "日本" in Shift_JIS
		QCOMPARE(out, std::string("\xE6\x97\xA5\xE6\x9C\xAC"));
		QVERIFY(convert(CP_UTF8, 1200, "A\xC3\xA9", 3, out)); // UTF-16LE
		QCOMPARE(out, std::string("A\0\xE9\0", 4));

		// Invalid input is replaced (U+FFFD), not dropped
		QVERIFY(convert(CP_UTF8, CP_UTF8, "a\xFF" "b", 3, out));
		QCOMPARE(out, std::string("a\xEF\xBF\xBD" "b"));

		// Chunked decoding: an incomplete trailing sequence is reported, not converted
		int notProcessed = 0;
		const std::wstring part = toWide("ab\xE2\x82", 4, CP_UTF8, &notProcessed);
		QCOMPARE(part, std::wstring(L"ab"));
		QCOMPARE(notProcessed, 2);

		// CP720 (Arabic DOS) is not in glibc iconv: built-in table
		QVERIFY(convert(720, CP_UTF8, "a\xA0\xE9", 3, out));
		QCOMPARE(out, std::string("a\xD8\xA8\xD9\x84")); // Python codecs.decode(b"a\xA0\xE9", "cp720")
		std::string back;
		QVERIFY(convert(CP_UTF8, 720, out.data(), out.size(), back));
		QCOMPARE(back, std::string("a\xA0\xE9"));

		QVERIFY(isCodepageSupported(1252));
		QVERIFY(isCodepageSupported(936));
		QVERIFY(!isCodepageSupported(12345));
	}

	void encodingMapper()
	{
		const EncodingMapper& em = EncodingMapper::getInstance();
		QCOMPARE(em.getEncodingFromString("windows-1252"), 1252);
		QCOMPARE(em.getEncodingFromString("Shift_JIS"), 932);
		QCOMPARE(em.getEncodingFromString("UTF-8"), int(SC_CP_UTF8));
		QCOMPARE(em.getEncodingFromString("koi8_r"), 20866);
		QCOMPARE(em.getEncodingFromIndex(em.getIndexFromEncoding(28605)), 28605);

		// Every code page of the Encoding menu must be convertible on Linux
		for (int i = 0; ; ++i)
		{
			const int cp = em.getEncodingFromIndex(i);
			if (cp == -1 && em.getEncodingFromIndex(i + 1) == -1 && em.getEncodingFromIndex(i + 2) == -1)
				break;
			if (cp == -1)
				continue; // unused menu slots (ISO-8859-10, -11, -16 in upstream)
			QVERIFY2(nplx::encoding::isCodepageSupported(cp), qPrintable(QString::number(cp)));
		}
	}

	// ------------------------------------------------------------------ Utf8_16 (file encodings)

	void utf8_16_detectAndRoundTrip_data()
	{
		QTest::addColumn<int>("mode");
		QTest::addColumn<QByteArray>("fileBytes");
		QTest::addColumn<QByteArray>("utf8");

		const QByteArray text = QByteArray("h\xC3\xA9llo\r\nw\xE2\x82\xACrld\n");

		QTest::newRow("UTF-8 BOM") << int(uniUTF8) << (QByteArray("\xEF\xBB\xBF") + text) << text;
		QTest::newRow("UTF-8 no BOM") << int(uniUTF8_NoBOM) << text << text;
		QTest::newRow("UTF-16LE BOM") << int(uni16LE)
			<< QByteArray("\xFF\xFEh\0\xE9\0l\0l\0o\0\r\0\n\0w\0\xAC\x20r\0l\0d\0\n\0", 28) << text;
		QTest::newRow("UTF-16BE BOM") << int(uni16BE)
			<< QByteArray("\xFE\xFF\0h\0\xE9\0l\0l\0o\0\r\0\n\0w\x20\xAC\0r\0l\0d\0\n", 28) << text;
		QTest::newRow("ASCII") << int(uni7Bit) << QByteArray("plain ascii\n") << QByteArray("plain ascii\n");
	}

	void utf8_16_detectAndRoundTrip()
	{
		QFETCH(int, mode);
		QFETCH(QByteArray, fileBytes);
		QFETCH(QByteArray, utf8);

		UniMode detected = uni8Bit;
		const std::string decoded = decodeLikeLoader(fileBytes.toStdString(), detected);
		QCOMPARE(int(detected), mode);
		QCOMPARE(QByteArray::fromStdString(decoded), utf8);

		if (mode == uni7Bit)
			return; // ASCII is saved as-is (8-bit path), covered by "UTF-8 no BOM"

		// Saving with the detected mode must reproduce the original bytes exactly
		const std::wstring path = tmpFile(L"roundtrip.txt");
		const std::string saved = encodeLikeSaver(decoded, detected, path);
		QCOMPARE(QByteArray::fromStdString(saved), fileBytes);
	}

	void utf8_16_detectsUtf16WithoutBom()
	{
		// Upstream uses IsTextUnicode(IS_TEXT_UNICODE_STATISTICS); NPLX uses Wine's heuristic
		const std::string le("T\0e\0x\0t\0 \0f\0i\0l\0e\0\n\0", 20);
		UniMode detected = uni8Bit;
		QCOMPARE(decodeLikeLoader(le, detected), std::string("Text file\n"));
		QCOMPARE(int(detected), int(uni16LE_NoBOM));

		// Binary-looking data (NUL units) is not taken for UTF-16
		const std::string binary("A\0\0\0B\0\0\0", 8);
		decodeLikeLoader(binary, detected);
		QVERIFY(detected != uni16LE_NoBOM);
	}

	// ------------------------------------------------------------------ Common

	void commonPaths()
	{
		std::wstring p = L"/home/user";
		pathAppend(p, L"notes.txt");
		QCOMPARE(p, std::wstring(L"/home/user/notes.txt"));
		pathAppend(p = L"/usr/share/notepadlx/", L"localization\\english.xml"); // upstream-style sub-path
		QCOMPARE(p, std::wstring(L"/usr/share/notepadlx/localization/english.xml"));

		std::wstring f = L"/a/b/c.txt";
		QCOMPARE(pathRemoveFileSpec(f), std::wstring(L"/a/b"));
		std::wstring root = L"/c.txt";
		QCOMPARE(pathRemoveFileSpec(root), std::wstring(L"/"));

		QCOMPARE(std::wstring(PathFindFileName(L"/x/y/z.tar.gz")), std::wstring(L"z.tar.gz"));
		QCOMPARE(std::wstring(PathFindExtension(L"/x/y/z.tar.gz")), std::wstring(L".gz"));

		const std::vector<std::wstring> patterns = { L"*.cpp", L"*.h", L"!moc_*" };
		QVERIFY(matchInList(L"/src/Main.CPP", patterns));
		QVERIFY(matchInList(L"widget.h", patterns));
		QVERIFY(!matchInList(L"moc_widget.cpp", patterns));
		QVERIFY(!matchInList(L"readme.md", patterns));

		::setenv("NPLX_TEST_VAR", "expanded", 1);
		std::wstring env = L"$NPLX_TEST_VAR/${NPLX_TEST_VAR}/%NPLX_TEST_VAR%/$UNDEFINED_NPLX_VAR";
		expandEnv(env);
		QCOMPARE(env, std::wstring(L"expanded/expanded/expanded/$UNDEFINED_NPLX_VAR"));
	}

	void commonMisc()
	{
		std::tm t{};
		t.tm_year = 2026 - 1900; t.tm_mon = 9; t.tm_mday = 4; t.tm_hour = 15; t.tm_min = 7; t.tm_sec = 9;
		QCOMPARE(getDateTimeStrFrom(L"yyyy-MM-dd HH:mm:ss", t), std::wstring(L"2026-10-04 15:07:09"));
		QCOMPARE(getDateTimeStrFrom(L"d/M/yy h:mm tt", t), std::wstring(L"4/10/26 3:07 PM"));

		QVERIFY(Version(L"8.7.1") > Version(L"8.7"));
		QVERIFY(Version(L"8.7.1").isCompatibleTo(Version(L"8.0"), Version(L"")));
		QVERIFY(Version(L"1.x").empty());

		// shlwapi ColorRGBToHLS / ColorHLSToRGB: same scale (0..240) and round trip
		WORD h = 0, l = 0, s = 0;
		ColorRGBToHLS(RGB(255, 0, 0), &h, &l, &s);
		QCOMPARE(int(h), 0); QCOMPARE(int(l), 120); QCOMPARE(int(s), 240);
		for (COLORREF c : { RGB(0x12, 0x34, 0x56), RGB(200, 100, 50), RGB(0, 0, 0), RGB(255, 255, 255) })
		{
			ColorRGBToHLS(c, &h, &l, &s);
			const COLORREF back = ColorHLSToRGB(h, l, s);
			QVERIFY(std::abs(GetRValue(back) - GetRValue(c)) <= 2);
			QVERIFY(std::abs(GetGValue(back) - GetGValue(c)) <= 2);
			QVERIFY(std::abs(GetBValue(back) - GetBValue(c)) <= 2);
		}

		// FILETIME as stored in session.xml
		const FILETIME ft = unixTimeToFileTime(1700000000);
		QCOMPARE(fileTimeToUnixTime(ft), int64_t(1700000000));
		QCOMPARE(((uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime), uint64_t(133444736000000000ULL));
	}

	// ------------------------------------------------------------------ NppParameters

	void parametersLoad()
	{
		NppParameters& nppParam = NppParameters::getInstance();
		QCOMPARE(QString::fromStdWString(nppParam.getNppPath()), QStringLiteral(NPLX_TEST_SOURCE_DATA_DIR));

		// Optional files (nativeLang.xml, toolbarButtonsConf.xml) are absent: load() reports it,
		// like upstream, but everything else must be loaded.
		nppParam.load();

		const std::wstring userPath = nppParam.getUserPath();
		QCOMPARE(QString::fromStdWString(userPath), _tmp.path() + "/config/notepadlx");

		// Model files were copied / generated into the user settings dir
		for (const wchar_t* f : { L"langs.xml", L"stylers.xml", L"shortcuts.xml", L"contextMenu.xml" })
		{
			std::wstring p = userPath;
			pathAppend(p, f);
			QVERIFY2(doesFileExist(p.c_str()), qPrintable(QString::fromStdWString(p)));
		}

		// langs.xml
		QVERIFY(nppParam.getNbLang() > 80);
		QCOMPARE(int(nppParam.getLangFromExt(L"cpp")), int(L_CPP));
		QCOMPARE(int(nppParam.getLangFromExt(L"py")), int(L_PYTHON));
		QCOMPARE(int(nppParam.getLangFromExt(L"rs")), int(L_RUST));
		QCOMPARE(int(nppParam.getLangIDFromStr(L"javascript.js")), int(L_JAVASCRIPT));

		// NotePadLX default (data/config.model.xml): dark mode with themes/Solarized.xml
		QVERIFY(nppParam.getNppGUI()._darkmode._isEnabled);
		QVERIFY(QString::fromStdWString(nppParam.getNppGUI()._themeName).endsWith(QStringLiteral("themes/Solarized.xml")));
		QVERIFY(!nppParam.getNppGUI().isSnapshotMode()); // closing with unsaved changes asks to save
		LexerStyler* cpp = nppParam.getLStylerArray().getLexerStylerByName(L"cpp");
		QVERIFY(cpp);
		Style* instr = cpp->findByName(L"INSTRUCTION WORD");
		QVERIFY(instr);
		QCOMPARE(instr->_fgColor, RGB(0x85, 0x99, 0x00));
		QCOMPARE(instr->_styleID, 5);
		Style* defaultStyle = nppParam.getMiscStylerArray().findByName(L"Default Style");
		QVERIFY(defaultStyle);

		// shortcuts.xml (generated from upstream SHORTCUT_XML_CONTENT): the sample macro is there
		QVERIFY(!nppParam.getMacroList().empty());
		QVERIFY(!nppParam.getMacroList()[0].getMacro().empty());

		// User Defined Languages from data/userDefineLangs are not loaded from the data dir (they are
		// per-user in upstream too); the user dir has none yet.
		QVERIFY(nppParam.getNbUserLang() >= 0);
	}

	void parametersSaveConfig()
	{
		NppParameters& nppParam = NppParameters::getInstance();
		nppParam.createXmlTreeFromGUIParams(); // as upstream does at exit, before saveConfig_xml()
		nppParam.saveConfig_xml();

		std::wstring configPath = nppParam.getUserPath();
		pathAppend(configPath, L"config.xml");
		const std::string xml = readBytes(configPath);
		QVERIFY(xml.find("<NotepadPlus>") != std::string::npos);
		QVERIFY(xml.find("<GUIConfig name=\"ToolBar\"") != std::string::npos);
		QVERIFY(xml.find("<GUIConfig name=\"DockingManager\"") != std::string::npos);
	}

	void sessionRoundTrip()
	{
		NppParameters& nppParam = NppParameters::getInstance();

		Session session;
		Position pos;
		pos._firstVisibleLine = 3;
		pos._startPos = 10;
		pos._endPos = 15;
		MapPosition mapPos;
		const FILETIME ts = unixTimeToFileTime(1700000000);

		session._mainViewFiles.emplace_back(L"/home/user/projects/main.cpp", L"C++", 65001, false, true, false, pos, nullptr, ts, mapPos);
		session._mainViewFiles.back()._marks = { 1, 5, 42 };
		session._subViewFiles.emplace_back(L"/home/user/notes/r\u00E9sum\u00E9.txt", L"Normal text", 1252, true, false, false, Position{}, nullptr, FILETIME{}, mapPos);
		session._activeView = 1;

		const std::wstring sessionFile = tmpFile(L"session-test.xml");
		nppParam.writeSession(session, sessionFile.c_str());

		Session loaded;
		QVERIFY(nppParam.loadSession(loaded, sessionFile.c_str(), true));
		QCOMPARE(loaded.nbMainFiles(), size_t(1));
		QCOMPARE(loaded.nbSubFiles(), size_t(1));
		QCOMPARE(loaded._activeView, size_t(1));

		const sessionFileInfo& m = loaded._mainViewFiles[0];
		QCOMPARE(m._fileName, std::wstring(L"/home/user/projects/main.cpp"));
		QCOMPARE(m._langName, std::wstring(L"C++"));
		QCOMPARE(m._encoding, 65001);
		QVERIFY(m._isPinned);
		QCOMPARE(m._startPos, intptr_t(10));
		QCOMPARE(m._firstVisibleLine, intptr_t(3));
		QCOMPARE(m._marks, (std::vector<size_t>{ 1, 5, 42 }));
		QCOMPARE(fileTimeToUnixTime(m._originalFileLastModifTimestamp), int64_t(1700000000));

		const sessionFileInfo& s = loaded._subViewFiles[0];
		QCOMPARE(s._fileName, std::wstring(L"/home/user/notes/r\u00E9sum\u00E9.txt"));
		QCOMPARE(s._encoding, 1252);
		QVERIFY(s._isUserReadOnly);
	}

	void sessionFromWindowsKeepsBackupConfined()
	{
		// A session.xml written by Windows Notepad++: the backup path is rebased into <userPath>/backup/
		NppParameters& nppParam = NppParameters::getInstance();
		const std::wstring sessionFile = tmpFile(L"session-win.xml");
		writeBytes(sessionFile,
			"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
			"<NotepadPlus><Session activeView=\"0\"><mainView activeIndex=\"0\">"
			"<File firstVisibleLine=\"0\" xOffset=\"0\" scrollWidth=\"1\" startPos=\"0\" endPos=\"0\" selMode=\"0\" offset=\"0\" wrapCount=\"1\" "
			"lang=\"Normal text\" encoding=\"-1\" userReadOnly=\"no\" filename=\"new 1\" "
			"backupFilePath=\"C:\\Users\\me\\AppData\\Roaming\\Notepad++\\backup\\new 1@2026-01-01_120000\" "
			"originalFileLastModifTimestamp=\"0\" originalFileLastModifTimestampHigh=\"0\" tabPinned=\"no\" />"
			"</mainView><subView activeIndex=\"0\" /></Session></NotepadPlus>\n");

		Session loaded;
		QVERIFY(nppParam.loadSession(loaded, sessionFile.c_str(), true));
		QCOMPARE(loaded.nbMainFiles(), size_t(1));
		const std::wstring expected = nplx::fullPathName(nppParam.getUserPath() + L"/backup") + L"/new 1@2026-01-01_120000";
		QCOMPARE(QString::fromStdWString(loaded._mainViewFiles[0]._backupFilePath), QString::fromStdWString(expected));
	}

	void localizationStrings()
	{
		// nativeLang lookups on the real french.xml shipped in data/localization
		std::wstring frPath = NppParameters::getInstance().getNppPath();
		pathAppend(frPath, L"localization/french.xml");
		NppXml::NewDocument doc;
		QVERIFY(NppXml::loadFileNativeLang(&doc, frPath.c_str()));

		NativeLangSpeaker speaker;
		speaker.init(&doc);
		QVERIFY(!speaker.isRTL());
		const std::wstring fileMenu = speaker.getNativeLangMenuString(IDM_FILE_NEW, L"", true);
		QCOMPARE(QString::fromStdWString(fileMenu), QStringLiteral("Nouveau"));
		const std::wstring untitled = speaker.getLocalizedStrFromID("tab-untitled-string", L"new ");
		QCOMPARE(QString::fromStdWString(untitled), QStringLiteral("nouveau "));
	}

	void fileAttributesWithTimeout()
	{
		// upstream getFileAttributesExWithTimeout: the result of the worker, no timeout on a local disk
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const std::wstring file = (dir.path() + "/a.txt").toStdWString();
		{
			QFile f(QString::fromStdWString(file));
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("12345");
		}
		WIN32_FILE_ATTRIBUTE_DATA attr{};
		bool isTimeoutReached = true;
		DWORD error = 1;
		QVERIFY(getFileAttributesExWithTimeout(file.c_str(), &attr, 0, &isTimeoutReached, &error));
		QVERIFY(!isTimeoutReached);
		QCOMPARE(error, DWORD(NO_ERROR));
		QCOMPARE(attr.nFileSizeLow, DWORD(5));
		QVERIFY(!(attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));

		const std::wstring missing = (dir.path() + "/missing").toStdWString();
		QVERIFY(!getFileAttributesExWithTimeout(missing.c_str(), &attr, 0, &isTimeoutReached, &error));
		QVERIFY(!isTimeoutReached);
		QCOMPARE(error, DWORD(ENOENT));
		QCOMPARE(attr.dwFileAttributes, DWORD(INVALID_FILE_ATTRIBUTES));

		QVERIFY(doesFileExist(file.c_str()));
		QVERIFY(!doesDirectoryExist(file.c_str()));
		QVERIFY(doesDirectoryExist(dir.path().toStdWString().c_str()));
		QVERIFY(doesPathExist(dir.path().toStdWString().c_str()));
		QVERIFY(!doesPathExist(missing.c_str()));

		ULARGE_INTEGER freeBytes{};
		QVERIFY(getDiskFreeSpaceWithTimeout(dir.path().toStdWString().c_str(), &freeBytes, 0, &isTimeoutReached));
		QVERIFY(!isTimeoutReached);
		QVERIFY(freeBytes.QuadPart > 0);
	}

	void stringConversionStopsAtNull()
	{
		// upstream string2wstring / wstring2string convert up to the first null character
		std::string buf("/us");
		buf.resize(16, '\0');
		QCOMPARE(string2wstring(buf, CP_UTF8), std::wstring(L"/us"));
		std::wstring wbuf(L"ab");
		wbuf.resize(8, L'\0');
		QCOMPARE(wstring2string(wbuf, CP_UTF8), std::string("ab"));
	}
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
