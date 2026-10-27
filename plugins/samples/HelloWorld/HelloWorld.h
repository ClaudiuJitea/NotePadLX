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

// Hello World sample plugin: what another plugin can ask it with NPPM_MSGTOPLUGIN (destination "HelloWorld.so").

#pragma once

#include <cstdint>

enum HelloWorldCommand
{
	HelloWorldSayHello,
	HelloWorldInsertPath,
	HelloWorldSeparator,
	HelloWorldShowCounters,
	HelloWorldNbFunc
};

// CommunicationInfo::internalMsg; CommunicationInfo::info is a HelloWorldCounters* the plugin fills
constexpr long HelloWorldMsgGetCounters = 1;

struct HelloWorldCounters
{
	int _notifications = 0;
	int _filesOpened = 0;
	int _filesSaved = 0;
	int _buffersActivated = 0;
	int _langChanged = 0;
	int _textInserted = 0;
	int _helloCount = 0;
	bool _ready = false;
	bool _shutdown = false;
	uintptr_t _lastActivatedBuffer = 0;
	void* _lastScintilla = nullptr;
};
