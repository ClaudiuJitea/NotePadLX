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

// NPLX: what upstream FileManager / Buffer call on Notepad_plus. The Qt port of Notepad_plus
// implements it (Phase 2); tests use a minimal host. Names follow the upstream members.

#pragma once

#include "Buffer.h"

class ScintillaEditView;

class NppBufferHost
{
public:
	virtual ~NppBufferHost() = default;

	virtual Buffer* getCurrentBuffer() = 0;
	virtual bool viewVisible(int whichOne) = 0;
	virtual int otherView() = 0;
	virtual ScintillaEditView* getInactiveScintView() = 0;
	virtual ScintillaEditView* getActiveEditView() = 0;  // upstream: Notepad_plus::_pEditView
	virtual void notifyBufferChanged(Buffer* buffer, int mask) = 0;
	virtual void command(int id) = 0;
	virtual void loadBufferIntoView(BufferID id, int whichOne) = 0;
	virtual void saveCurrentSession() = 0;
};
