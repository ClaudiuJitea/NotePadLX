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

// Phase 1 acceptance tests: the ported FileManager / Buffer load files in every supported encoding
// and save them back byte-identically (BOM, EOL and encoding preserved).

#include <QtTest>
#include <QTemporaryDir>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#include "Buffer.h"
#include "NppBufferHost.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "localization.h"

namespace
{
	std::string readBytes(const std::wstring& path)
	{
		std::ifstream f(nplx::toNativePath(path), std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}

	void writeBytes(const std::wstring& path, const QByteArray& bytes)
	{
		std::ofstream f(nplx::toNativePath(path), std::ios::binary);
		f.write(bytes.constData(), bytes.size());
	}

	// Minimal Notepad_plus for FileManager: one view, records notifications
	class TestHost : public NppBufferHost
	{
	public:
		explicit TestHost(ScintillaEditView* view) : _view(view) {}

		Buffer* getCurrentBuffer() override { return _current; }
		bool viewVisible(int) override { return false; }
		int otherView() override { return 1; }
		ScintillaEditView* getInactiveScintView() override { return _view; }
		ScintillaEditView* getActiveEditView() override { return _view; }
		void notifyBufferChanged(Buffer*, int mask) override { _lastMask |= mask; }
		void command(int) override {}
		void loadBufferIntoView(BufferID, int) override {}
		void saveCurrentSession() override {}

		Buffer* _current = nullptr;
		int _lastMask = 0;

	private:
		ScintillaEditView* _view = nullptr;
	};
}

class TestFileManager : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	std::unique_ptr<ScintillaEditView> _scratch;
	std::unique_ptr<ScintillaEditView> _view;
	std::unique_ptr<TestHost> _host;
	NativeLangSpeaker _nativeLangSpeaker;
	int _fileCounter = 0;

	std::wstring tmpFile(const QString& name) const
	{
		return (_tmp.path() + "/" + name).toStdWString();
	}

	// Writes bytes to a file, loads it with FileManager (optionally forcing an encoding)
	BufferID loadBytes(const QByteArray& bytes, const QString& ext = QStringLiteral("txt"), int encoding = -1)
	{
		const std::wstring path = tmpFile(QStringLiteral("in%1.%2").arg(++_fileCounter).arg(ext));
		writeBytes(path, bytes);
		return MainFileManager.loadFile(path.c_str(), static_cast<Document>(0), encoding);
	}

	std::string documentText(Buffer* buf)
	{
		_view->execute(SCI_SETDOCPOINTER, 0, buf->getDocument());
		return _view->getText();
	}

private slots:
	void initTestCase()
	{
		QVERIFY(_tmp.isValid());
		::setenv("XDG_CONFIG_HOME", (_tmp.path() + "/config").toUtf8().constData(), 1);
		::setenv("XDG_DATA_HOME", (_tmp.path() + "/share").toUtf8().constData(), 1);
		::setenv("NPLX_DATA_DIR", NPLX_TEST_SOURCE_DATA_DIR, 1);

		// Same start-up order as upstream winmain: parameters, then the native language speaker
		NppParameters& nppParam = NppParameters::getInstance();
		nppParam.load();
		_nativeLangSpeaker.init(nullptr);
		nppParam.setNativeLangSpeaker(&_nativeLangSpeaker);

		_scratch = std::make_unique<ScintillaEditView>();
		_view = std::make_unique<ScintillaEditView>();
		_host = std::make_unique<TestHost>(_view.get());
		MainFileManager.init(_host.get(), _scratch.get());
	}

	void roundTrip_data()
	{
		QTest::addColumn<QByteArray>("bytes");
		QTest::addColumn<int>("forcedEncoding");
		QTest::addColumn<int>("expectedUniMode");
		QTest::addColumn<int>("expectedEncoding");
		QTest::addColumn<int>("expectedEol");
		QTest::addColumn<QByteArray>("expectedUtf8");

		const QByteArray utf8Text("Gr\xC3\xBC\xC3\x9F" "e \xE2\x82\xAC 100\nline 2\n");

		QTest::newRow("UTF-8 BOM, LF") << (QByteArray("\xEF\xBB\xBF") + utf8Text) << -1
			<< int(uniUTF8) << -1 << int(EolType::unix) << utf8Text;
		QTest::newRow("UTF-8, CRLF") << QByteArray("caf\xC3\xA9\r\nna\xC3\xAFve\r\n") << -1
			<< int(uniUTF8_NoBOM) << -1 << int(EolType::windows) << QByteArray("caf\xC3\xA9\r\nna\xC3\xAFve\r\n");
		QTest::newRow("UTF-16LE BOM") << QByteArray("\xFF\xFE" "A\0\xE9\0\n\0", 8) << -1
			<< int(uni16LE) << -1 << int(EolType::unix) << QByteArray("A\xC3\xA9\n");
		QTest::newRow("UTF-16BE BOM, CR") << QByteArray("\xFE\xFF\0A\0\xE9\0\r", 8) << -1
			<< int(uni16BE) << -1 << int(EolType::macos) << QByteArray("A\xC3\xA9\r");
		QTest::newRow("ASCII") << QByteArray("plain\n") << -1
			<< int(uniUTF8_NoBOM) << -1 << int(EolType::unix) << QByteArray("plain\n"); // _openAnsiAsUtf8 default
		QTest::newRow("Windows-1252 forced") << QByteArray("caf\xE9 \x80\r\n") << 1252
			<< int(uniUTF8_NoBOM) << 1252 << int(EolType::windows) << QByteArray("caf\xC3\xA9 \xE2\x82\xAC\r\n");
		QTest::newRow("Shift_JIS forced") << QByteArray("\x93\xFA\x96\x7B\x8C\xEA\n") << 932
			<< int(uniUTF8_NoBOM) << 932 << int(EolType::unix) << QByteArray("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\n");
		QTest::newRow("KOI8-R forced") << QByteArray("\xF0\xD2\xC9\xD7\xC5\xD4\n") << 20866
			<< int(uniUTF8_NoBOM) << 20866 << int(EolType::unix) << QByteArray("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82\n");
	}

	void roundTrip()
	{
		QFETCH(QByteArray, bytes);
		QFETCH(int, forcedEncoding);
		QFETCH(int, expectedUniMode);
		QFETCH(int, expectedEncoding);
		QFETCH(int, expectedEol);
		QFETCH(QByteArray, expectedUtf8);

		const BufferID id = loadBytes(bytes, QStringLiteral("txt"), forcedEncoding);
		QVERIFY(id != BUFFER_INVALID);
		Buffer* buf = MainFileManager.getBufferByID(id);

		QCOMPARE(int(buf->getUnicodeMode()), expectedUniMode);
		QCOMPARE(buf->getEncoding(), expectedEncoding);
		QCOMPARE(int(buf->getEolFormat()), expectedEol);
		QCOMPARE(QByteArray::fromStdString(documentText(buf)), expectedUtf8); // Scintilla holds UTF-8

		const std::wstring outPath = tmpFile(QStringLiteral("out%1.txt").arg(_fileCounter));
		QCOMPARE(int(MainFileManager.saveBuffer(id, outPath.c_str())), int(SavingStatus::SaveOK));
		QCOMPARE(QByteArray::fromStdString(readBytes(outPath)), bytes); // byte-identical
		QVERIFY(!buf->isDirty());
		QCOMPARE(QString::fromStdWString(buf->getFullPathName()), QString::fromStdWString(outPath));

		MainFileManager.closeBuffer(id, nullptr);
	}

	void detectsEncodingWithUchardet()
	{
		// Long enough Windows-1251 Russian text without BOM: uchardet must find it (upstream "detectEncoding")
		const QByteArray cp1251 = QByteArray(
			"\xDD\xF2\xEE \xF2\xE5\xEA\xF1\xF2 \xED\xE0 \xF0\xF3\xF1\xF1\xEA\xEE\xEC \xFF\xE7\xFB\xEA\xE5 "
			"\xE4\xEB\xFF \xEF\xF0\xEE\xE2\xE5\xF0\xEA\xE8 \xEE\xEF\xF0\xE5\xE4\xE5\xEB\xE5\xED\xE8\xFF "
			"\xEA\xEE\xE4\xE8\xF0\xEE\xE2\xEA\xE8. \xCC\xFB \xEF\xE8\xF8\xE5\xEC \xED\xE5\xF1\xEA\xEE\xEB\xFC\xEA\xEE "
			"\xEF\xF0\xE5\xE4\xEB\xEE\xE6\xE5\xED\xE8\xE9, \xF7\xF2\xEE\xE1\xFB \xE0\xED\xE0\xEB\xE8\xE7\xE0\xF2\xEE\xF0 "
			"\xEC\xEE\xE3 \xF3\xE2\xE5\xF0\xE5\xED\xED\xEE \xE2\xFB\xE1\xF0\xE0\xF2\xFC \xEF\xF0\xE0\xE2\xE8\xEB\xFC\xED\xF3\xFE "
			"\xEA\xEE\xE4\xEE\xE2\xF3\xFE \xF1\xF2\xF0\xE0\xED\xE8\xF6\xF3.\n");
		const BufferID id = loadBytes(cp1251);
		QVERIFY(id != BUFFER_INVALID);
		Buffer* buf = MainFileManager.getBufferByID(id);
		QCOMPARE(buf->getEncoding(), 1251);
		QVERIFY(documentText(buf).rfind("\xD0\xAD\xD1\x82\xD0\xBE", 0) == 0); // starts with "Это"

		const std::wstring outPath = tmpFile(QStringLiteral("out-cp1251.txt"));
		QCOMPARE(int(MainFileManager.saveBuffer(id, outPath.c_str())), int(SavingStatus::SaveOK));
		QCOMPARE(QByteArray::fromStdString(readBytes(outPath)), cp1251);
		MainFileManager.closeBuffer(id, nullptr);
	}

	void detectsLanguage()
	{
		struct Case { QByteArray content; QString ext; LangType lang; };
		const Case cases[] = {
			{ "int main() {}\n", "cpp", L_CPP },
			{ "print('x')\n", "py", L_PYTHON },
			{ "fn main() {}\n", "rs", L_RUST },
			{ "#!/bin/bash\necho hi\n", "", L_BASH },               // from the first line
			{ "<?xml version=\"1.0\"?>\n<a/>\n", "", L_XML },
			{ "plain\n", "unknownext", L_TEXT },
		};
		for (const Case& c : cases)
		{
			const BufferID id = loadBytes(c.content, c.ext.isEmpty() ? QStringLiteral("noext") : c.ext);
			QVERIFY(id != BUFFER_INVALID);
			Buffer* buf = MainFileManager.getBufferByID(id);
			if (c.ext.isEmpty())
			{
				// no known extension: the language comes from the content (detectLanguageFromTextBeginning)
				QCOMPARE(int(buf->getLangType()), int(c.lang));
			}
			else
			{
				QCOMPARE(int(buf->getLangType()), int(c.lang));
			}
			MainFileManager.closeBuffer(id, nullptr);
		}

		// File name rules (makefile, CMakeLists.txt) from Buffer::setFileName
		const std::wstring makefile = tmpFile(QStringLiteral("Makefile"));
		writeBytes(makefile, "all:\n\techo\n");
		const BufferID id = MainFileManager.loadFile(makefile.c_str());
		QCOMPARE(int(MainFileManager.getBufferByID(id)->getLangType()), int(L_MAKEFILE));
		MainFileManager.closeBuffer(id, nullptr);
	}

	void savePreservesPermissionsAndInode()
	{
		// Upstream rewrites files in place: an existing file keeps its mode, inode and hard links
		const std::wstring path = tmpFile(QStringLiteral("perm.sh"));
		writeBytes(path, "#!/bin/sh\necho 1\n");
		const std::string native = nplx::toNativePath(path);
		QCOMPARE(::chmod(native.c_str(), 0750), 0);
		struct stat before{};
		::stat(native.c_str(), &before);

		const BufferID id = MainFileManager.loadFile(path.c_str());
		QVERIFY(id != BUFFER_INVALID);
		Buffer* buf = MainFileManager.getBufferByID(id);
		_view->execute(SCI_SETDOCPOINTER, 0, buf->getDocument());
		_view->execute(SCI_APPENDTEXT, 7, reinterpret_cast<sptr_t>("echo 2\n"));
		QCOMPARE(int(MainFileManager.saveBuffer(id, path.c_str())), int(SavingStatus::SaveOK));

		struct stat after{};
		::stat(native.c_str(), &after);
		QCOMPARE(after.st_mode & 07777, mode_t(0750));
		QCOMPARE(after.st_ino, before.st_ino);
		QCOMPARE(readBytes(path), std::string("#!/bin/sh\necho 1\necho 2\n"));
		MainFileManager.closeBuffer(id, nullptr);
	}

	void untitledDocuments()
	{
		const BufferID id1 = MainFileManager.newEmptyDocument();
		QVERIFY(id1 != BUFFER_INVALID);
		Buffer* buf = MainFileManager.getBufferByID(id1);
		QVERIFY(buf->isUntitled());
		QCOMPARE(QString::fromWCharArray(buf->getFileName()), QStringLiteral("new 1"));
		QCOMPARE(int(buf->getEolFormat()), int(EolType::unix)); // NPLX default
		MainFileManager.closeBuffer(id1, nullptr);
	}

	void missingFileIsInvalid()
	{
		const std::wstring path = tmpFile(QStringLiteral("does-not-exist.txt"));
		QCOMPARE(MainFileManager.loadFile(path.c_str()), BUFFER_INVALID);
	}
};

QTEST_MAIN(TestFileManager)
#include "tst_filemanager.moc"
