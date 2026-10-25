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

#include "KeyMap.h"

namespace nplx
{
int virtualKeyToQtKey(unsigned char vk)
{
	if (vk >= VK_A && vk <= VK_Z)
		return Qt::Key_A + (vk - VK_A);
	if (vk >= VK_0 && vk <= VK_9)
		return Qt::Key_0 + (vk - VK_0);
	if (vk >= VK_F1 && vk <= VK_F24)
		return Qt::Key_F1 + (vk - VK_F1);
	if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
		return (Qt::Key_0 + (vk - VK_NUMPAD0)) | static_cast<int>(Qt::KeypadModifier);

	switch (vk)
	{
		case VK_BACK:      return Qt::Key_Backspace;
		case VK_TAB:       return Qt::Key_Tab;
		case VK_RETURN:    return Qt::Key_Return;
		case VK_ESCAPE:    return Qt::Key_Escape;
		case VK_SPACE:     return Qt::Key_Space;
		case VK_PRIOR:     return Qt::Key_PageUp;
		case VK_NEXT:      return Qt::Key_PageDown;
		case VK_END:       return Qt::Key_End;
		case VK_HOME:      return Qt::Key_Home;
		case VK_LEFT:      return Qt::Key_Left;
		case VK_UP:        return Qt::Key_Up;
		case VK_RIGHT:     return Qt::Key_Right;
		case VK_DOWN:      return Qt::Key_Down;
		case VK_INSERT:    return Qt::Key_Insert;
		case VK_DELETE:    return Qt::Key_Delete;
		case VK_MULTIPLY:  return Qt::Key_Asterisk | static_cast<int>(Qt::KeypadModifier);
		case VK_ADD:       return Qt::Key_Plus | static_cast<int>(Qt::KeypadModifier);
		case VK_SUBTRACT:  return Qt::Key_Minus | static_cast<int>(Qt::KeypadModifier);
		case VK_DECIMAL:   return Qt::Key_Period | static_cast<int>(Qt::KeypadModifier);
		case VK_DIVIDE:    return Qt::Key_Slash | static_cast<int>(Qt::KeypadModifier);
		// US layout positions of the OEM keys (what Windows reports for them)
		case VK_OEM_1:      return Qt::Key_Semicolon;
		case VK_OEM_PLUS:   return Qt::Key_Equal;
		case VK_OEM_COMMA:  return Qt::Key_Comma;
		case VK_OEM_MINUS:  return Qt::Key_Minus;
		case VK_OEM_PERIOD: return Qt::Key_Period;
		case VK_OEM_2:      return Qt::Key_Slash;
		case VK_OEM_3:      return Qt::Key_QuoteLeft;
		case VK_OEM_4:      return Qt::Key_BracketLeft;
		case VK_OEM_5:      return Qt::Key_Backslash;
		case VK_OEM_6:      return Qt::Key_BracketRight;
		case VK_OEM_7:      return Qt::Key_Apostrophe;
		case VK_OEM_102:    return Qt::Key_Less;
		default:            return 0;
	}
}

QKeySequence keyComboToKeySequence(const KeyCombo& kc)
{
	const int key = virtualKeyToQtKey(kc._key);
	if (key == 0)
		return QKeySequence();

	Qt::KeyboardModifiers mods = Qt::KeyboardModifiers::fromInt(key & static_cast<int>(Qt::KeyboardModifierMask));
	if (kc._isCtrl)
		mods |= Qt::ControlModifier;
	if (kc._isAlt)
		mods |= Qt::AltModifier;
	if (kc._isShift)
		mods |= Qt::ShiftModifier;
	return QKeySequence(QKeyCombination(mods, static_cast<Qt::Key>(key & ~static_cast<int>(Qt::KeyboardModifierMask))));
}
}
