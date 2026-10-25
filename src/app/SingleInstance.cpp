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

#include "SingleInstance.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

#include <unistd.h>

namespace
{
	QJsonObject toJson(const std::vector<std::wstring>& files, const CmdLineParamsDTO& p)
	{
		QJsonArray fileArray;
		for (const std::wstring& f : files)
			fileArray.append(QString::fromStdWString(f));
		QJsonObject o;
		o["files"] = fileArray;
		o["readOnly"] = p._isReadOnly;
		o["noSession"] = p._isNoSession;
		o["sessionFile"] = p._isSessionFile;
		o["recursive"] = p._isRecursive;
		o["foldersAsWorkspace"] = p._openFoldersAsWorkspace;
		o["monitor"] = p._monitorFiles;
		o["line"] = static_cast<qint64>(p._line2go);
		o["column"] = static_cast<qint64>(p._column2go);
		o["pos"] = static_cast<qint64>(p._pos2go);
		o["lang"] = static_cast<int>(p._langType);
		o["udl"] = QString::fromWCharArray(p._udlName);
		o["pluginMessage"] = QString::fromWCharArray(p._pluginMessage);
		return o;
	}

	void fromJson(const QJsonObject& o, std::vector<std::wstring>& files, CmdLineParamsDTO& p)
	{
		for (const QJsonValue& v : o["files"].toArray())
			files.push_back(v.toString().toStdWString());
		p._isReadOnly = o["readOnly"].toBool();
		p._isNoSession = o["noSession"].toBool();
		p._isSessionFile = o["sessionFile"].toBool();
		p._isRecursive = o["recursive"].toBool();
		p._openFoldersAsWorkspace = o["foldersAsWorkspace"].toBool();
		p._monitorFiles = o["monitor"].toBool();
		p._line2go = static_cast<intptr_t>(o["line"].toInteger(-1));
		p._column2go = static_cast<intptr_t>(o["column"].toInteger(-1));
		p._pos2go = static_cast<intptr_t>(o["pos"].toInteger(-1));
		p._langType = static_cast<LangType>(o["lang"].toInt(L_EXTERNAL));
		wcsncpy(p._udlName, o["udl"].toString().toStdWString().c_str(), MAX_PATH - 1);
		wcsncpy(p._pluginMessage, o["pluginMessage"].toString().toStdWString().c_str(), 2047);
	}
}

SingleInstance::SingleInstance(const QString& key, QObject* parent)
	: QObject(parent), _key(key)
{
}

QString SingleInstance::defaultKey(const std::wstring& userPath)
{
	// One instance per user and settings directory (like upstream: one per Notepad++ installation)
	const QByteArray hash = QCryptographicHash::hash(QString::fromStdWString(userPath).toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
	return QStringLiteral("notepadlx-%1-%2").arg(::getuid()).arg(QString::fromLatin1(hash));
}

bool SingleInstance::sendToRunningInstance(const std::vector<std::wstring>& files, const CmdLineParamsDTO& params)
{
	QLocalSocket socket;
	socket.connectToServer(_key);
	if (!socket.waitForConnected(500))
		return false;

	const QByteArray payload = QJsonDocument(toJson(files, params)).toJson(QJsonDocument::Compact) + '\n';
	socket.write(payload);
	if (!socket.waitForBytesWritten(2000))
		return false;
	socket.waitForReadyRead(2000); // the primary instance answers "ok" once handled
	socket.disconnectFromServer();
	return true;
}

bool SingleInstance::listen(Handler handler)
{
	_handler = std::move(handler);
	_server = new QLocalServer(this);
	_server->setSocketOptions(QLocalServer::UserAccessOption);
	if (!_server->listen(_key))
	{
		// a stale socket from a crashed instance: nobody answered in sendToRunningInstance
		QLocalServer::removeServer(_key);
		if (!_server->listen(_key))
			return false;
	}

	connect(_server, &QLocalServer::newConnection, this, [this]() {
		while (QLocalSocket* socket = _server->nextPendingConnection())
		{
			connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
			connect(socket, &QLocalSocket::readyRead, socket, [this, socket]() {
				if (!socket->canReadLine())
					return;
				const QJsonDocument doc = QJsonDocument::fromJson(socket->readLine());
				std::vector<std::wstring> files;
				CmdLineParamsDTO params;
				fromJson(doc.object(), files, params);
				socket->write("ok\n");
				socket->flush();
				if (_handler)
					_handler(files, params);
			});
		}
	});
	return true;
}
