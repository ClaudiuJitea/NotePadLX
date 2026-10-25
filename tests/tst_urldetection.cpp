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

// Port of upstream PowerEditor/Test/UrlDetection (verifyUrlDetection.lua): each "u <text> u" line is followed by
// "m <mask> m", where the mask has '1' for every character that must be detected as part of a URL.
// The segmentation loop is the one of Notepad_plus::addHotSpot.

#include <QtTest>
#include <QFile>

#include "UrlDetection.h"

namespace
{
	// Same loop as Notepad_plus::addHotSpot, on characters instead of encoded bytes
	QString detectionMask(const std::wstring& line)
	{
		std::wstring text = line;
		QString mask(static_cast<qsizetype>(text.size()), QLatin1Char('0'));
		const int textLen = static_cast<int>(text.size());
		int start = 0;
		while (true)
		{
			int len = 0;
			const bool r = isUrl(text.data(), textLen, start, &len);
			if (len <= 0)
				break;
			if (r)
			{
				for (int i = start; i < start + len && i < textLen; ++i)
					mask[i] = QLatin1Char('1');
			}
			start += len;
			if (start >= textLen)
				break;
		}
		return mask;
	}
}

class TestUrlDetection : public QObject
{
	Q_OBJECT

	QHash<QString, QString> _knownFailures;

private slots:
	void upstreamTestFiles_data()
	{
		QTest::addColumn<QString>("line");
		QTest::addColumn<QString>("expected");

		for (const char* name : { "verifyUrlDetection_1a", "verifyUrlDetection_1b" })
		{
			QFile f(QStringLiteral(NPLX_TEST_DATA_DIR "/UrlDetection/") + QLatin1String(name));
			QVERIFY(f.open(QIODevice::ReadOnly));
			QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
			if (!lines.isEmpty() && lines[0].startsWith(QChar(0xFEFF)))
				lines[0].remove(0, 1);
			int n = 0;
			for (int i = 0; i + 1 < lines.size(); ++i)
			{
				const QString& u = lines[i];
				const QString& m = lines[i + 1];
				if (u.size() >= 4 && u.startsWith(QLatin1String("u ")) && u.endsWith(QLatin1String(" u"))
					&& m.startsWith(QLatin1String("m ")) && m.endsWith(QLatin1String(" m")))
				{
					QTest::newRow(qPrintable(QStringLiteral("%1:%2").arg(QLatin1String(name)).arg(i + 1)))
						<< u.mid(2, u.size() - 4) << m.mid(2, m.size() - 4);
					++n;
				}
			}
			QVERIFY(n > 0);

			// upstream's .expected.result lists the known failures ("KO") with the mask upstream produces ("ko")
			QFile r(QStringLiteral(NPLX_TEST_DATA_DIR "/UrlDetection/") + QLatin1String(name) + QStringLiteral(".expected.result"));
			QVERIFY(r.open(QIODevice::ReadOnly));
			const QStringList results = QString::fromUtf8(r.readAll()).split(QLatin1Char('\n'));
			for (int i = 0; i + 2 < results.size(); ++i)
			{
				if (results[i].startsWith(QLatin1String("KO\tu ")) && results[i + 2].startsWith(QLatin1String("ko\tm ")))
				{
					const QString u = results[i].mid(3);
					const QString m = results[i + 2].mid(3);
					_knownFailures.insert(u.mid(2, u.size() - 4), m.mid(2, m.size() - 4));
				}
			}
		}
	}

	void upstreamTestFiles()
	{
		QFETCH(QString, line);
		QFETCH(QString, expected);
		if (_knownFailures.contains(line))
			expected = _knownFailures.value(line); // same result as upstream

		// the editor line, as upstream's test sees it: "u " + text + " u"
		const std::wstring editorLine = (QStringLiteral("u ") + line + QStringLiteral(" u")).toStdWString();
		const QString mask = detectionMask(editorLine);
		// compare per character (upstream moves with CharRight); a non-BMP character is one wchar_t here
		const QString textMask = mask.mid(2, mask.size() - 4);
		QString expectedPerChar;
		int k = 0;
		for (int i = 0; i < line.size(); ++i)
		{
			if (line[i].isLowSurrogate())
				continue;
			expectedPerChar += (k < expected.size()) ? expected[k] : QLatin1Char('?');
			++k;
		}
		QCOMPARE(textMask, expectedPerChar);
	}
};

QTEST_MAIN(TestUrlDetection)
#include "tst_urldetection.moc"
