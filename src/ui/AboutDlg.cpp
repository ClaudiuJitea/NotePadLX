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

#include "AboutDlg.h"

#include <unistd.h>

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSysInfo>
#include <QVBoxLayout>
#include <QVersionNumber>

#include <boost/version.hpp>
#include "json.hpp"
#include "pugixml.hpp"

#include "Parameters.h"

namespace
{
	std::string g_commandLineHelpText;
	std::wstring g_commandLine;

	// "566" (Scintilla's version.txt) -> "5.6.6"
	std::wstring dottedVersion(const char* digits)
	{
		std::wstring v;
		for (const char* p = digits; *p; ++p)
		{
			if (!v.empty())
				v += L'.';
			v += static_cast<wchar_t>(*p);
		}
		return v;
	}

	QString nplxVersion()
	{
		return QStringLiteral(NPLX_VERSION);
	}

	QString bitness()
	{
		// upstream: nppParam.archType() -> (32-bit) / (64-bit) / (ARM 64-bit)
		const QString arch = QSysInfo::buildCpuArchitecture();
		if (arch == QStringLiteral("arm64"))
			return QStringLiteral("(ARM 64-bit)");
		return QSysInfo::WordSize == 64 ? QStringLiteral("(64-bit)") : QStringLiteral("(32-bit)");
	}

	QString buildTime()
	{
		return QStringLiteral(__DATE__ " - " __TIME__);
	}
}

void nplx::setCommandLineHelpText(const std::string& text)
{
	g_commandLineHelpText = text;
}

const std::string& nplx::commandLineHelpText()
{
	return g_commandLineHelpText;
}

void nplx::setCommandLine(const std::wstring& cmdLine)
{
	g_commandLine = cmdLine;
}

// ---- About

AboutDlg::AboutDlg(std::function<void()> checkForUpdates, std::function<void()> debugInfo, QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("About NotePadLX"));
	setObjectName(QStringLiteral("aboutDlg"));

	auto* logo = new QLabel(this);
	logo->setPixmap(QIcon(QStringLiteral(":/icons/notepadlx-256.png")).pixmap(96, 96));
	logo->setAlignment(Qt::AlignTop);

	auto* title = new QLabel(QStringLiteral("<span style='font-size:18pt; font-weight:600'>NotePadLX %1</span>"
		"&nbsp;&nbsp;<span style='color:palette(placeholder-text)'>%2</span>").arg(nplxVersion(), bitness()), this);
	auto* subtitle = new QLabel(tr("A Linux port of <a href=\"https://notepad-plus-plus.org/\">Notepad++</a>"), this);
	subtitle->setOpenExternalLinks(true);
	auto* home = new QLabel(QStringLiteral("%1 <a href=\"%2\">%2</a>").arg(tr("Home:"), QStringLiteral(NPLX_HOMEPAGE_URL)), this);
	home->setOpenExternalLinks(true);
	home->setTextInteractionFlags(Qt::TextBrowserInteraction);
	auto* build = new QLabel(QStringLiteral("%1 %2<br>Qt %3 · Scintilla %4 · Lexilla %5").arg(tr("Build time:"), buildTime(),
		QString::fromLatin1(qVersion()), QString::fromStdWString(dottedVersion(NPLX_SCINTILLA_VERSION)),
		QString::fromStdWString(dottedVersion(NPLX_LEXILLA_VERSION))), this);
	build->setTextInteractionFlags(Qt::TextSelectableByMouse);
	auto* credits = new QLabel(tr("Notepad++ is © Don HO and the Notepad++ contributors."), this);
	for (QLabel* l : { subtitle, home, build, credits })
		l->setWordWrap(true);

	auto* header = new QVBoxLayout;
	header->setSpacing(6);
	header->addWidget(title);
	header->addWidget(subtitle);
	header->addWidget(home);
	header->addWidget(build);
	header->addWidget(credits);
	header->addStretch();

	auto* top = new QHBoxLayout;
	top->setSpacing(18);
	top->addWidget(logo, 0, Qt::AlignTop);
	top->addLayout(header, 1);

	// the GPL notice (upstream IDC_LICENCE_EDIT in the "GNU General Public Licence" group)
	auto* licenceGroup = new QGroupBox(tr("GNU General Public Licence"), this);
	auto* licence = new QPlainTextEdit(licenceGroup);
	licence->setObjectName(QStringLiteral("licenceEdit"));
	licence->setReadOnly(true);
	licence->setPlainText(QString::fromWCharArray(LICENCE_TXT));
	licence->setFrameShape(QFrame::NoFrame);
	licence->setMinimumHeight(130);
	auto* licenceLayout = new QVBoxLayout(licenceGroup);
	licenceLayout->addWidget(licence);

	// the actions on the left, OK on the right
	auto* updates = new QPushButton(tr("Check for &Updates..."), this);
	updates->setObjectName(QStringLiteral("checkForUpdates"));
	updates->setAutoDefault(false);
	auto* debug = new QPushButton(tr("&Debug Info..."), this);
	debug->setObjectName(QStringLiteral("debugInfo"));
	debug->setAutoDefault(false);
	auto* ok = new QPushButton(tr("OK"), this);
	ok->setDefault(true);
	connect(updates, &QPushButton::clicked, this, [checkForUpdates]() { if (checkForUpdates) checkForUpdates(); });
	connect(debug, &QPushButton::clicked, this, [debugInfo]() { if (debugInfo) debugInfo(); });
	connect(ok, &QPushButton::clicked, this, &QDialog::accept);
	auto* buttons = new QHBoxLayout;
	buttons->addWidget(updates);
	buttons->addWidget(debug);
	buttons->addStretch();
	buttons->addWidget(ok);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(22, 22, 22, 16);
	layout->setSpacing(16);
	layout->addLayout(top);
	layout->addWidget(licenceGroup, 1);
	layout->addLayout(buttons);

	setMinimumWidth(520);
}

void AboutDlg::doDialog()
{
	show();
	raise();
	activateWindow();
}

// ---- Debug Info

DebugInfoDlg::DebugInfoDlg(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Debug Info"));
	_edit = new QPlainTextEdit(this);
	_edit->setReadOnly(true);
	_edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	_edit->setMinimumSize(560, 360);

	auto* buttons = new QDialogButtonBox(this);
	QPushButton* copy = buttons->addButton(tr("&Copy debug info to clipboard"), QDialogButtonBox::ActionRole);
	QPushButton* ok = buttons->addButton(QDialogButtonBox::Ok);
	ok->setDefault(true);
	connect(copy, &QPushButton::clicked, this, [this]() {
		// upstream IDC_DEBUGINFO_COPYLINK: the text, as a fenced block for the issue tracker
		refreshDebugInfo();
		QGuiApplication::clipboard()->setText(QString::fromStdWString(_debugInfoStr));
	});
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(_edit, 1);
	layout->addWidget(buttons);
}

void DebugInfoDlg::refreshDebugInfo()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGui = nppParam.getNppGUI();
	auto onOff = [](bool b) { return b ? std::wstring(L"ON") : std::wstring(L"OFF"); };
	auto q = [](const QString& s) { return s.toStdWString(); };

	// NotePadLX version
	_debugInfoStr = L"NotePadLX v" + q(nplxVersion()) + L"   " + q(bitness()) + L"\n";

	// Build time
	_debugInfoStr += L"Build time: " + q(buildTime()) + L"\n";

#if defined(__clang__)
	_debugInfoStr += L"Built with: Clang " + q(QStringLiteral(__clang_version__)) + L"\n";
#elif defined(__GNUC__)
	_debugInfoStr += L"Built with: GCC " + q(QStringLiteral(__VERSION__)) + L"\n";
#else
	_debugInfoStr += L"Built with: (unknown)\n";
#endif

	// Qt, Scintilla/Lexilla, Boost Regex, pugixml, JSON
	_debugInfoStr += L"Qt: " + q(QString::fromLatin1(qVersion())) + L" (built with " + q(QStringLiteral(QT_VERSION_STR)) + L")\n";
	_debugInfoStr += L"Scintilla/Lexilla included: " + dottedVersion(NPLX_SCINTILLA_VERSION) + L"/" + dottedVersion(NPLX_LEXILLA_VERSION) + L"\n";
	_debugInfoStr += L"Boost Regex included: " + std::to_wstring(BOOST_VERSION / 100000) + L"." + std::to_wstring(BOOST_VERSION / 100 % 1000) + L"\n";
#if defined(PUGIXML_VERSION)
	_debugInfoStr += L"pugixml included: " + std::to_wstring(PUGIXML_VERSION / 1000) + L"." + std::to_wstring((PUGIXML_VERSION % 1000) / 10) + L"\n";
#endif
	_debugInfoStr += L"nlohmann JSON included: " + std::to_wstring(NLOHMANN_JSON_VERSION_MAJOR) + L"." + std::to_wstring(NLOHMANN_JSON_VERSION_MINOR) + L"." + std::to_wstring(NLOHMANN_JSON_VERSION_PATCH) + L"\n";

	// Binary path, data and settings
	_debugInfoStr += L"Path: " + q(QCoreApplication::applicationFilePath()) + L"\n";
	_debugInfoStr += L"Data: " + nppParam.getNppPath() + L"\n";
	_debugInfoStr += L"Settings: " + nppParam.getUserPath() + L"\n";

	// Command line as specified for program launch
	_debugInfoStr += L"Command Line: " + g_commandLine + L"\n";

	// NPLX: root (upstream "Admin mode"), local conf, cloud
	_debugInfoStr += L"Root: " + onOff(::geteuid() == 0) + L"\n";
	_debugInfoStr += L"Local Conf mode: " + onOff(nppParam.isLocal()) + L"\n";
	const std::wstring& cloudPath = nppGui._cloudPath;
	_debugInfoStr += L"Cloud Config: " + (cloudPath.empty() ? std::wstring(L"OFF") : cloudPath) + L"\n";

	// Periodic Backup, placeholders
	_debugInfoStr += L"Periodic Backup: " + onOff(nppGui.isSnapshotMode()) + L"\n";
	_debugInfoStr += L"Placeholders: " + onOff(nppGui._keepSessionAbsentFileEntries) + L"\n";

	// Multi-instance
	_debugInfoStr += L"Multi-instance Mode: ";
	switch (nppGui._multiInstSetting)
	{
		case monoInst:
			_debugInfoStr += L"monoInst";
			break;
		case multiInstOnSession:
			_debugInfoStr += L"multiInstOnSession";
			break;
		case multiInst:
			_debugInfoStr += L"multiInst";
			break;
		default:
			_debugInfoStr += L"unknown(" + std::to_wstring(nppGui._multiInstSetting) + L")";
	}
	_debugInfoStr += L"\n";

	// asNotepad
	_debugInfoStr += L"asNotepad: " + onOff(nppParam.isAsNotepadStyle()) + L"\n";

	// File Status Auto-Detection
	_debugInfoStr += L"File Status Auto-Detection: ";
	if (nppGui._fileAutoDetection == cdDisabled)
	{
		_debugInfoStr += L"cdDisabled";
	}
	else
	{
		if (nppGui._fileAutoDetection & cdEnabled4AllFiles)
			_debugInfoStr += L"cdEnabled4AllFiles";
		else if (nppGui._fileAutoDetection & cdEnabled4CurrentFile)
			_debugInfoStr += L"cdEnabled4CurrentFile";
		else
			_debugInfoStr += L"cdUnknown (?!)";

		if (nppGui._fileAutoDetection & cdAutoUpdate)
			_debugInfoStr += L" + cdAutoUpdate";
		if (nppGui._fileAutoDetection & cdGo2end)
			_debugInfoStr += L" + cdGo2end";
	}
	_debugInfoStr += L"\n";

	// Dark Mode
	_debugInfoStr += L"Dark Mode: " + onOff(nppGui._darkmode._isEnabled) + L"\n";

	// NPLX: the screens (upstream: the primary monitor, the monitors count and the display adapters)
	_debugInfoStr += L"Display Info:";
	const QList<QScreen*> screens = QGuiApplication::screens();
	for (const QScreen* screen : screens)
	{
		_debugInfoStr += L"\n    " + q(screen->name()) + L": " + std::to_wstring(screen->geometry().width()) + L"x" +
			std::to_wstring(screen->geometry().height()) + L", scaling " + std::to_wstring(qRound(screen->devicePixelRatio() * 100)) + L"%";
	}
	_debugInfoStr += L"\n    visible monitors count: " + std::to_wstring(screens.size()) + L"\n";

	// NPLX: the system (upstream: the Windows version from the registry)
	_debugInfoStr += L"OS: " + q(QSysInfo::prettyProductName()) + L"\n";
	_debugInfoStr += L"Kernel: " + q(QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion()) + L" " + q(QSysInfo::currentCpuArchitecture()) + L"\n";
	_debugInfoStr += L"Platform: " + q(QGuiApplication::platformName()) + L"\n";
	_debugInfoStr += L"Desktop: " + q(qEnvironmentVariable("XDG_CURRENT_DESKTOP", QStringLiteral("(unknown)"))) +
		L" (" + q(qEnvironmentVariable("XDG_SESSION_TYPE", QStringLiteral("unknown session"))) + L")\n";
	_debugInfoStr += L"Locale: " + q(QLocale::system().name()) + L"\n";

	// Plugins
	_debugInfoStr += L"Plugins: none\n"; // NPLX_TODO(Phase 9): the loaded plugins

	_edit->setPlainText(QString::fromStdWString(_debugInfoStr));
}

void DebugInfoDlg::doDialog()
{
	refreshDebugInfo();
	show();
	raise();
	activateWindow();
}

// ---- Command Line Arguments

CmdLineArgsDlg::CmdLineArgsDlg(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Command Line Arguments"));
	auto* edit = new QPlainTextEdit(this);
	edit->setObjectName(QStringLiteral("cmdLineArgsEdit"));
	edit->setReadOnly(true);
	edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); // upstream setFont: a fixed font
	edit->setPlainText(QString::fromStdString(nplx::commandLineHelpText()));
	edit->setMinimumSize(620, 420);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(edit, 1);
	layout->addWidget(buttons);
}

void CmdLineArgsDlg::doDialog()
{
	show();
	raise();
	activateWindow();
}

// ---- Update checker

UpdateChecker::UpdateChecker(QObject* parent)
	: QObject(parent), _network(new QNetworkAccessManager(this))
{
}

bool UpdateChecker::isNewer(const QString& candidate, const QString& current)
{
	auto parse = [](QString v) {
		v = v.trimmed();
		if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
			v.remove(0, 1);
		return QVersionNumber::fromString(v);
	};
	const QVersionNumber c = parse(candidate);
	return !c.isNull() && QVersionNumber::compare(c, parse(current)) > 0;
}

void UpdateChecker::check(std::function<void(Result, const QString&, const QString&, const QString&)> done)
{
	const QString url = qEnvironmentVariable("NPLX_UPDATE_URL",
		QStringLiteral("https://api.github.com/repos/ClaudiuJitea/NotePadLX/releases/latest"));
	QNetworkRequest request{ QUrl(url) };
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NotePadLX/%1").arg(nplxVersion()));
	request.setTransferTimeout(15000);

	QNetworkReply* reply = _network->get(request);
	connect(reply, &QNetworkReply::finished, this, [reply, done]() {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError)
		{
			done(Result::Error, QString(), QString(), reply->errorString());
			return;
		}
		const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
		const QString tag = release.value(QStringLiteral("tag_name")).toString();
		const QString page = release.value(QStringLiteral("html_url")).toString(QStringLiteral(NPLX_HOMEPAGE_URL "/releases"));
		if (tag.isEmpty())
		{
			done(Result::Error, QString(), QString(), QStringLiteral("no release found"));
			return;
		}
		const QString latest = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
		done(isNewer(latest, nplxVersion()) ? Result::NewVersion : Result::UpToDate, latest, page, QString());
	});
}
