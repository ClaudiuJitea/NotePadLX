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

// NPLX: single instance, in place of upstream's FindWindow + WM_COPYDATA (COPYDATA_PARAMS,
// COPYDATA_FILENAMESW). A QLocalServer per user (and settings dir) receives the files and the
// command line parameters of later launches as JSON.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QObject>

#include "Parameters.h"

class QLocalServer;

class SingleInstance : public QObject
{
	Q_OBJECT

public:
	using Handler = std::function<void(const std::vector<std::wstring>& files, const CmdLineParamsDTO& params)>;

	explicit SingleInstance(const QString& key, QObject* parent = nullptr);

	// true if a running instance received the request (this process should then exit)
	bool sendToRunningInstance(const std::vector<std::wstring>& files, const CmdLineParamsDTO& params);

	// become the primary instance
	bool listen(Handler handler);

	static QString defaultKey(const std::wstring& userPath);

private:
	QString _key;
	QLocalServer* _server = nullptr;
	Handler _handler;
};
