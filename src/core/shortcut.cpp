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

// NPLX: data part of upstream WinControls/shortcut/shortcut.cpp (see shortcut.h)

#include "shortcut.h"

#include "Parameters.h"
#include "resource.h"
#include "FindReplaceDlg_rc.h"

using std::string;
using std::wstring;
using std::vector;

std::function<std::wstring(unsigned long)> g_menuCommandNameResolver;

struct KeyIDNAME {
	const char * name = nullptr;
	UCHAR id = 0;
};

KeyIDNAME namedKeyArray[] = {
{"None", VK_NULL},

{"Backspace", VK_BACK},
{"Tab", VK_TAB},
{"Enter", VK_RETURN},
{"Esc", VK_ESCAPE},
{"Spacebar", VK_SPACE},

{"Page up", VK_PRIOR},
{"Page down", VK_NEXT},
{"End", VK_END},
{"Home", VK_HOME},
{"Left", VK_LEFT},
{"Up", VK_UP},
{"Right", VK_RIGHT},
{"Down", VK_DOWN},

{"INS", VK_INSERT},
{"DEL", VK_DELETE},

{"0", VK_0},
{"1", VK_1},
{"2", VK_2},
{"3", VK_3},
{"4", VK_4},
{"5", VK_5},
{"6", VK_6},
{"7", VK_7},
{"8", VK_8},
{"9", VK_9},
{"A", VK_A},
{"B", VK_B},
{"C", VK_C},
{"D", VK_D},
{"E", VK_E},
{"F", VK_F},
{"G", VK_G},
{"H", VK_H},
{"I", VK_I},
{"J", VK_J},
{"K", VK_K},
{"L", VK_L},
{"M", VK_M},
{"N", VK_N},
{"O", VK_O},
{"P", VK_P},
{"Q", VK_Q},
{"R", VK_R},
{"S", VK_S},
{"T", VK_T},
{"U", VK_U},
{"V", VK_V},
{"W", VK_W},
{"X", VK_X},
{"Y", VK_Y},
{"Z", VK_Z},

{"Numpad 0", VK_NUMPAD0},
{"Numpad 1", VK_NUMPAD1},
{"Numpad 2", VK_NUMPAD2},
{"Numpad 3", VK_NUMPAD3},
{"Numpad 4", VK_NUMPAD4},
{"Numpad 5", VK_NUMPAD5},
{"Numpad 6", VK_NUMPAD6},
{"Numpad 7", VK_NUMPAD7},
{"Numpad 8", VK_NUMPAD8},
{"Numpad 9", VK_NUMPAD9},
{"Num *", VK_MULTIPLY},
{"Num +", VK_ADD},
// {"Num Enter"), VK_SEPARATOR},	//this one doesn't seem to work: see below
{"Num -", VK_SUBTRACT},
{"Num .", VK_DECIMAL},
{"Num /", VK_DIVIDE},
{"F1", VK_F1},
{"F2", VK_F2},
{"F3", VK_F3},
{"F4", VK_F4},
{"F5", VK_F5},
{"F6", VK_F6},
{"F7", VK_F7},
{"F8", VK_F8},
{"F9", VK_F9},
{"F10", VK_F10},
{"F11", VK_F11},
{"F12", VK_F12},

{"F13", VK_F13},
{"F14", VK_F14},
{"F15", VK_F15},
{"F16", VK_F16},
{"F17", VK_F17},
{"F18", VK_F18},
{"F19", VK_F19},
{"F20", VK_F20},
{"F21", VK_F21},
{"F22", VK_F22},
{"F23", VK_F23},
{"F24", VK_F24},

{"~", VK_OEM_3},
{"-", VK_OEM_MINUS},
{"=", VK_OEM_PLUS},
{"[", VK_OEM_4},
{"]", VK_OEM_6},
{";", VK_OEM_1},
{"'", VK_OEM_7},
{"\\", VK_OEM_5},
{",", VK_OEM_COMMA},
{".", VK_OEM_PERIOD},
{"/", VK_OEM_2},

{"<>", VK_OEM_102},

// add placeholders for the VirtualKeys that aren't on the standard en-US keyboard
{"", VK_OEM_8},	// VK_OEM_8 not on US keyboards, despite being named
{"", VK_SEPARATOR}, // used for the ',' in "1,234,567.89", but it's not on most keyboards, so keep it in the custom section
{"", 0x88},		// RESERVED
{"", 0x89},		// RESERVED
{"", 0x8A},		// RESERVED
{"", 0x8B},		// RESERVED
{"", 0x8C},		// RESERVED
{"", 0x8D},		// RESERVED
{"", 0x8E},		// RESERVED
{"", 0x8F},		// RESERVED
{"", 0x92},		// OEM SPECIFIC
{"", 0x93},		// OEM SPECIFIC
{"", 0x94},		// OEM SPECIFIC
{"", 0x95},		// OEM SPECIFIC
{"", 0x96},		// OEM SPECIFIC
{"", 0x97},		// UNASSIGNED
{"", 0x98},		// UNASSIGNED
{"", 0x99},		// UNASSIGNED
{"", 0x9A},		// UNASSIGNED
{"", 0x9B},		// UNASSIGNED
{"", 0x9C},		// UNASSIGNED
{"", 0x9D},		// UNASSIGNED
{"", 0x9E},		// UNASSIGNED
{"", 0x9F},		// UNASSIGNED
{"", 0xB8},		// RESERVED
{"", 0xB9},		// RESERVED
{"", 0xC1},		// RESERVED: ABNT_C1
{"", 0xC2},		// RESERVED: ABNT_C2
{"", 0xC3},		// RESERVED
{"", 0xC4},		// RESERVED
{"", 0xC5},		// RESERVED
{"", 0xC6},		// RESERVED
{"", 0xC7},		// RESERVED
{"", 0xC8},		// RESERVED
{"", 0xC9},		// RESERVED
{"", 0xCA},		// RESERVED
{"", 0xCB},		// RESERVED
{"", 0xCC},		// RESERVED
{"", 0xCD},		// RESERVED
{"", 0xCE},		// RESERVED
{"", 0xCF},		// RESERVED
{"", 0xD0},		// RESERVED
{"", 0xD1},		// RESERVED
{"", 0xD2},		// RESERVED
{"", 0xD3},		// RESERVED
{"", 0xD4},		// RESERVED
{"", 0xD5},		// RESERVED
{"", 0xD6},		// RESERVED
{"", 0xD7},		// RESERVED
{"", 0xD8},		// RESERVED
{"", 0xD9},		// RESERVED
{"", 0xDA},		// RESERVED
{"", 0xE0},		// RESERVED
{"", 0xE1},		// OEM SPECIFIC
{"", 0xE3},		// OEM SPECIFIC
{"", 0xE4},		// OEM SPECIFIC
{"", 0xE6},		// OEM SPECIFIC
{"", 0xE8},		// UNASSIGNED
{"", 0xE9},		// OEM SPECIFIC
{"", 0xEA},		// OEM SPECIFIC
{"", 0xEB},		// OEM SPECIFIC
{"", 0xEC},		// OEM SPECIFIC
{"", 0xED},		// OEM SPECIFIC
{"", 0xEE},		// OEM SPECIFIC
{"", 0xEF},		// OEM SPECIFIC
{"", 0xF0},		// OEM SPECIFIC
{"", 0xF1},		// OEM SPECIFIC
{"", 0xF2},		// OEM SPECIFIC
{"", 0xF3},		// OEM SPECIFIC
{"", 0xF4},		// OEM SPECIFIC
{"", 0xF5},		// OEM SPECIFIC
};

#define nbKeys sizeof(namedKeyArray)/sizeof(KeyIDNAME)

size_t namedKeyCount() { return nbKeys; }
const char* namedKeyName(size_t i) { return namedKeyArray[i].name; }
UCHAR namedKeyId(size_t i) { return namedKeyArray[i].id; }

void getKeyStrFromVal(UCHAR keyVal, string & str)
{
	str = "";
	bool found = false;
	size_t i;
	for (i = 0; i < nbKeys; ++i)
	{
		if (keyVal == namedKeyArray[i].id)
		{
			found = true;
			break;
		}
	}
	if (found) 
	{
		// by default, assume it will just use the original name
		str = namedKeyArray[i].name;
		
		// NPLX_TODO(Phase 7): upstream maps OEM keys to the active keyboard layout's names (mapOemVirtualKeys)
	}
	else 
		str = "Unlisted";
}

void getNameStrFromCmd(DWORD cmd, wstring & str)
{
	if ((cmd >= ID_MACRO) && (cmd < ID_MACRO_LIMIT))
	{
		const vector<MacroShortcut> & theMacros = (NppParameters::getInstance()).getMacroList();
		int i = cmd - ID_MACRO;
		str = string2wstring(theMacros[i].getName(), CP_UTF8);
	}
	else if ((cmd >= ID_USER_CMD) && (cmd < ID_USER_CMD_LIMIT))
	{
		const vector<UserCommand> & userCommands = (NppParameters::getInstance()).getUserCommandList();
		int i = cmd - ID_USER_CMD;
		str = string2wstring(userCommands[i].getName(), CP_UTF8);
	}
	else if ((cmd >= ID_PLUGINS_CMD) && (cmd < ID_PLUGINS_CMD_LIMIT))
	{
		const vector<PluginCmdShortcut> & pluginCmds = (NppParameters::getInstance()).getPluginCommandList();
		size_t i = 0;
		for (size_t j = 0, len = pluginCmds.size(); j < len ; ++j)
		{
			if (pluginCmds[j].getID() == cmd)
			{
				i = j;
				break;
			}
		}
		str = string2wstring(pluginCmds[i].getName(), CP_UTF8);
	}
	else
	{
		// NPLX: upstream reads the label from the Win32 menu (GetMenuString); the Qt menu provides it
		if (g_menuCommandNameResolver)
			str = purgeMenuItemString(g_menuCommandNameResolver(cmd).c_str());
	}
	return;
}

string Shortcut::toString() const
{
	string sc;
	if (!isEnabled())
		return sc;

	if (_keyCombo._isCtrl)
		sc += "Ctrl+";
	if (_keyCombo._isAlt)
		sc += "Alt+";
	if (_keyCombo._isShift)
		sc += "Shift+";

	string keyString;
	getKeyStrFromVal(_keyCombo._key, keyString);
	sc += keyString;
	return sc;
}

void Shortcut::setName(const char* menuName, const char* shortcutName)
{
	strcpy_s(_menuName, menuItemStrLenMax, menuName); // NPLX: was lstrcpynA (truncates the same way)
	char const * name = shortcutName ? shortcutName : menuName;
	size_t i = 0, j = 0;
	while (name[j] != 0 && i < (menuItemStrLenMax - 1))
	{
		if (name[j] != '&')
		{
			_name[i] = name[j];
			++i;
		}
		else //check if this ampersand is being escaped
		{
			if (name[j+1] == '&') //escaped ampersand
			{
				_name[i] = name[j];
				++i;
				++j;	//skip escaped ampersand
			}
		}
		++j;
	}
	_name[i] = 0;
}

string ScintillaKeyMap::toString() const
{
	string sc;
	size_t nbCombos = getSize();
	for (size_t combo = 0; combo < nbCombos; ++combo)
	{
		sc += toString(combo);
		if (combo < nbCombos - 1)
			sc += " / ";
	}
	return sc;
}

string ScintillaKeyMap::toString(size_t index) const
{
	string sc = "";
	if (!isEnabled())
		return sc;

	KeyCombo kc = _keyCombos[index];
	if (kc._isCtrl)
		sc += "Ctrl+";
	if (kc._isAlt)
		sc += "Alt+";
	if (kc._isShift)
		sc += "Shift+";

	string keyString;
	getKeyStrFromVal(kc._key, keyString);
	sc += keyString;
	return sc;
}

KeyCombo ScintillaKeyMap::getKeyComboByIndex(size_t index) const
{
	return _keyCombos[index];
}

void ScintillaKeyMap::setKeyComboByIndex(int index, KeyCombo combo)
{
	if (combo._key == 0 && (_size > 1))
	{	//remove the item if possible
		_keyCombos.erase(_keyCombos.begin() + index);
	}
	_keyCombos[index] = combo;
}

void ScintillaKeyMap::removeKeyComboByIndex(size_t index)
{
	if (_size > 1 && index < _size)
	{
		_keyCombos.erase(_keyCombos.begin() + index);
		_size--;
	}
}

int ScintillaKeyMap::addKeyCombo(KeyCombo combo)
{	//returns index where key is added, or -1 when invalid
	if (combo._key == 0)	//do not allow to add disabled keycombos
		return -1;
	if (!isEnabled())
	{	//disabled, override current combo with new enabled one
		_keyCombos[0] = combo;
		return 0;
	}

	for (size_t i = 0; i < _size; ++i)
	{	//if already in the list do not add it
		const KeyCombo& kc = _keyCombos[i];
		if (combo._key == kc._key && combo._isCtrl == kc._isCtrl && combo._isAlt == kc._isAlt && combo._isShift == kc._isShift)
			return static_cast<int32_t>(i);	//already in the list
	}
	_keyCombos.push_back(combo);
	++_size;
	return static_cast<int32_t>(_size - 1);
}

bool ScintillaKeyMap::isEnabled() const
{
	return (_keyCombos[0]._key != 0);
}

size_t ScintillaKeyMap::getSize() const
{
	return _size;
}

CommandShortcut& CommandShortcut::operator = (const Shortcut& sct)
{
	if (this != &sct)
	{
		strcpy(this->_name, sct.getName());
		this->_keyCombo = sct.getKeyCombo();
	}
	return *this;
}

recordedMacroStep::recordedMacroStep(int iMessage, uptr_t wParam, uptr_t lParam)
	: _message(iMessage), _wParameter(wParam), _lParameter(lParam), _macroType(mtUseLParameter)
{ 
	if (_lParameter)
	{
		switch (_message)
		{
			case SCI_SETTEXT :
			case SCI_REPLACESEL :
			case SCI_REPLACETARGET :
			case SCI_REPLACETARGETRE :
			case SCI_SEARCHINTARGET :
			case SCI_ADDTEXT :
			case SCI_ADDSTYLEDTEXT :
			case SCI_INSERTTEXT :
			case SCI_APPENDTEXT :
			case SCI_SETWORDCHARS :
			case SCI_SETWHITESPACECHARS :
			case SCI_SETSTYLINGEX :
			case SCI_TEXTWIDTH :
			case SCI_STYLESETFONT :
			case SCI_SEARCHNEXT :
			case SCI_SEARCHPREV :
			case IDFINDWHAT:
			case IDREPLACEWITH:
			case IDD_FINDINFILES_DIR_COMBO:
			case IDD_FINDINFILES_FILTERS_COMBO:
			{
				char *ch = reinterpret_cast<char *>(_lParameter);
				_sParameter = ch;
				_macroType = mtUseSParameter;
				_lParameter = 0;
			}
			break;

				
			default : // for all other messages, use _lParameter "as is"
				break;
		}
	}
}

bool recordedMacroStep::isMacroable() const
{
	// Enumerates all macroable messages
	switch (_message)
	{
		case SCI_REPLACESEL: // (<unused>, const char *text)
		case SCI_ADDTEXT:    // (int length, const char *s)
		case SCI_INSERTTEXT: // (int pos, const char *text)
		case SCI_APPENDTEXT: // (int length, const char *s)
		case SCI_SEARCHNEXT: // (int searchFlags, const char *text)
		case SCI_SEARCHPREV: // (int searchFlags, const char *text)
		{
			if (_macroType == mtUseSParameter)
				return true;
			else
				return false;
		}

		case SCI_GOTOLINE:   // (int line)
		case SCI_GOTOPOS:    // (int position)
		case SCI_SETSELECTIONMODE:  // (int mode)
		case SCI_CUT:
		case SCI_COPY:
		case SCI_PASTE:
		case SCI_CLEAR:
		case SCI_CLEARALL:
		case SCI_SELECTALL:
		case SCI_SEARCHANCHOR:
		case SCI_LINEDOWN:
		case SCI_LINEDOWNEXTEND:
		case SCI_PARADOWN:
		case SCI_PARADOWNEXTEND:
		case SCI_LINEUP:
		case SCI_LINEUPEXTEND:
		case SCI_PARAUP:
		case SCI_PARAUPEXTEND:
		case SCI_CHARLEFT:
		case SCI_CHARLEFTEXTEND:
		case SCI_CHARRIGHT:
		case SCI_CHARRIGHTEXTEND:
		case SCI_WORDLEFT:
		case SCI_WORDLEFTEXTEND:
		case SCI_WORDRIGHT:
		case SCI_WORDRIGHTEXTEND:
		case SCI_WORDPARTLEFT:
		case SCI_WORDPARTLEFTEXTEND:
		case SCI_WORDPARTRIGHT:
		case SCI_WORDPARTRIGHTEXTEND:
		case SCI_WORDLEFTEND:
		case SCI_WORDLEFTENDEXTEND:
		case SCI_WORDRIGHTEND:
		case SCI_WORDRIGHTENDEXTEND:
		case SCI_HOME:
		case SCI_HOMEEXTEND:
		case SCI_LINEEND:
		case SCI_LINEENDEXTEND:
		case SCI_HOMEWRAP:
		case SCI_HOMEWRAPEXTEND:
		case SCI_LINEENDWRAP:
		case SCI_LINEENDWRAPEXTEND:
		case SCI_DOCUMENTSTART:
		case SCI_DOCUMENTSTARTEXTEND:
		case SCI_DOCUMENTEND:
		case SCI_DOCUMENTENDEXTEND:
		case SCI_STUTTEREDPAGEUP:
		case SCI_STUTTEREDPAGEUPEXTEND:
		case SCI_STUTTEREDPAGEDOWN:
		case SCI_STUTTEREDPAGEDOWNEXTEND:
		case SCI_PAGEUP:
		case SCI_PAGEUPEXTEND:
		case SCI_PAGEDOWN:
		case SCI_PAGEDOWNEXTEND:
		case SCI_EDITTOGGLEOVERTYPE:
		case SCI_CANCEL:
		case SCI_DELETEBACK:
		case SCI_TAB:
		case SCI_BACKTAB:
		case SCI_FORMFEED:
		case SCI_VCHOME:
		case SCI_VCHOMEEXTEND:
		case SCI_VCHOMEWRAP:
		case SCI_VCHOMEWRAPEXTEND:
		case SCI_VCHOMEDISPLAY:
		case SCI_VCHOMEDISPLAYEXTEND:
		case SCI_DELWORDLEFT:
		case SCI_DELWORDRIGHT:
		case SCI_DELWORDRIGHTEND:
		case SCI_DELLINELEFT:
		case SCI_DELLINERIGHT:
		case SCI_LINECOPY:
		case SCI_LINECUT:
		case SCI_LINEDELETE:
		case SCI_LINETRANSPOSE:
		case SCI_LINEDUPLICATE:
		case SCI_LOWERCASE:
		case SCI_UPPERCASE:
		case SCI_LINESCROLLDOWN:
		case SCI_LINESCROLLUP:
		case SCI_DELETEBACKNOTLINE:
		case SCI_HOMEDISPLAY:
		case SCI_HOMEDISPLAYEXTEND:
		case SCI_LINEENDDISPLAY:
		case SCI_LINEENDDISPLAYEXTEND:
		case SCI_LINEDOWNRECTEXTEND:
		case SCI_LINEUPRECTEXTEND:
		case SCI_CHARLEFTRECTEXTEND:
		case SCI_CHARRIGHTRECTEXTEND:
		case SCI_HOMERECTEXTEND:
		case SCI_VCHOMERECTEXTEND:
		case SCI_LINEENDRECTEXTEND:
		case SCI_PAGEUPRECTEXTEND:
		case SCI_PAGEDOWNRECTEXTEND:
		case SCI_SELECTIONDUPLICATE:
		case SCI_COPYALLOWLINE:
		case SCI_VERTICALCENTRECARET:
		case SCI_MOVESELECTEDLINESUP:
		case SCI_MOVESELECTEDLINESDOWN:
		case SCI_SCROLLTOSTART:
		case SCI_SCROLLTOEND:
		case SCI_SETVIRTUALSPACEOPTIONS:
		case SCI_SETCARETLINEBACKALPHA:
		case SCI_NEWLINE:
		{
			if (_macroType == mtUseLParameter)
				return true;
			else
				return false;
		}

		// Filter out all others like display changes.
		default:
			return false;
	}
}
