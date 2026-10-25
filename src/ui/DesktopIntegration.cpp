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

#include "DesktopIntegration.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

namespace nplx::desktop
{
void showFileInFileManager(const QString& filePath)
{
	QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
		QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
	call << QStringList{ QUrl::fromLocalFile(filePath).toString() } << QString();
	const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 2000);
	if (reply.type() == QDBusMessage::ErrorMessage)
		QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
}

void openFolder(const QString& folderPath)
{
	QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
}

bool openTerminal(const QString& folderPath)
{
	struct Terminal { const char* _program; const char* _dirOption; };
	// the working directory is also set on the process: enough for the terminals without an option
	static constexpr Terminal terminals[] = {
		{ "x-terminal-emulator", nullptr }, { "kgx", nullptr }, { "gnome-terminal", "--working-directory=" },
		{ "konsole", "--workdir=" }, { "xfce4-terminal", "--working-directory=" }, { "mate-terminal", "--working-directory=" },
		{ "tilix", "--working-directory=" }, { "kitty", "--directory=" }, { "alacritty", "--working-directory=" },
		{ "wezterm", nullptr }, { "foot", nullptr }, { "lxterminal", "--working-directory=" }, { "xterm", nullptr },
	};

	std::vector<std::pair<QString, QStringList>> candidates;
	const QString userTerminal = qEnvironmentVariable("TERMINAL");
	if (!userTerminal.isEmpty())
		candidates.emplace_back(userTerminal, QStringList());
	for (const Terminal& t : terminals)
	{
		QStringList args;
		if (t._dirOption)
			args << QString::fromLatin1(t._dirOption) + folderPath;
		candidates.emplace_back(QString::fromLatin1(t._program), args);
	}

	for (const auto& [program, args] : candidates)
	{
		const QString path = QStandardPaths::findExecutable(program);
		if (path.isEmpty())
			continue;
		if (QProcess::startDetached(path, args, folderPath))
			return true;
	}
	return false;
}

bool openWithSystem(const QString& filePath)
{
	return QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}
}
