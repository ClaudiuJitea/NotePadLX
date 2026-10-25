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

#include "LangNameInfo.h"

LanguageNameInfo g_langNameInfoArray[L_EXTERNAL + 1] = {
	//
	// _langName:         _shortName                 _longName                                             _langID            _lexerID
	//
	// link to lang.xml
	// don't change
	//
	{L"normal",           L"Normal text",            L"Normal text file",                                  L_TEXT,            "null"},
	{L"php",              L"PHP",                    L"PHP Hypertext Preprocessor file",                   L_PHP,             "phpscript"},
	{L"c",                L"C",                      L"C source file",                                     L_C,               "cpp"},
	{L"cpp",              L"C++",                    L"C++ source file",                                   L_CPP,             "cpp"},
	{L"cs",               L"C#",                     L"C# source file",                                    L_CS,              "cpp"},
	{L"objc",             L"Objective-C",            L"Objective-C source file",                           L_OBJC,            "objc"},
	{L"java",             L"Java",                   L"Java source file",                                  L_JAVA,            "cpp"},
	{L"rc",               L"RC",                     L"Windows Resource file",                             L_RC,              "cpp"},
	{L"html",             L"HTML",                   L"Hyper Text Markup Language file",                   L_HTML,            "hypertext"},
	{L"xml",              L"XML",                    L"eXtensible Markup Language file",                   L_XML,             "xml"},
	{L"makefile",         L"Makefile",               L"Makefile",                                          L_MAKEFILE,        "makefile"},
	{L"pascal",           L"Pascal",                 L"Pascal source file",                                L_PASCAL,          "pascal"},
	{L"batch",            L"Batch",                  L"Batch file",                                        L_BATCH,           "batch"},
	{L"ini",              L"ini",                    L"MS ini file",                                       L_INI,             "props"},
	{L"nfo",              L"NFO",                    L"MSDOS Style/ASCII Art",                             L_ASCII,           "null"},
	{L"udf",              L"udf",                    L"User Defined language file",                        L_USER,            "user"},
	{L"asp",              L"ASP",                    L"Active Server Pages script file",                   L_ASP,             "hypertext"},
	{L"sql",              L"SQL",                    L"Structured Query Language file",                    L_SQL,             "sql"},
	{L"vb",               L"Visual Basic",           L"Visual Basic file",                                 L_VB,              "vb"},
	{L"javascript",       L"Embedded JS",            L"Embedded JavaScript",                               L_JS_EMBEDDED,     "cpp"},
	{L"css",              L"CSS",                    L"Cascade Style Sheets File",                         L_CSS,             "css"},
	{L"perl",             L"Perl",                   L"Perl source file",                                  L_PERL,            "perl"},
	{L"python",           L"Python",                 L"Python file",                                       L_PYTHON,          "python"},
	{L"lua",              L"Lua",                    L"Lua source File",                                   L_LUA,             "lua"},
	{L"tex",              L"TeX",                    L"TeX file",                                          L_TEX,             "tex"},
	{L"fortran",          L"Fortran free form",      L"Fortran free form source file",                     L_FORTRAN,         "fortran"},
	{L"bash",             L"Shell",                  L"Unix script file",                                  L_BASH,            "bash"},
	{L"actionscript",     L"ActionScript",           L"Flash ActionScript file",                           L_FLASH,           "cpp"},
	{L"nsis",             L"NSIS",                   L"Nullsoft Scriptable Install System script file",    L_NSIS,            "nsis"},
	{L"tcl",              L"TCL",                    L"Tool Command Language file",                        L_TCL,             "tcl"},
	{L"lisp",             L"Lisp",                   L"List Processing language file",                     L_LISP,            "lisp"},
	{L"scheme",           L"Scheme",                 L"Scheme file",                                       L_SCHEME,          "lisp"},
	{L"asm",              L"Assembly",               L"Assembly language source file",                     L_ASM,             "asm"},
	{L"diff",             L"Diff",                   L"Diff file",                                         L_DIFF,            "diff"},
	{L"props",            L"Properties file",        L"Properties file",                                   L_PROPS,           "props"},
	{L"postscript",       L"PostScript",             L"PostScript file",                                   L_PS,              "ps"},
	{L"ruby",             L"Ruby",                   L"Ruby file",                                         L_RUBY,            "ruby"},
	{L"smalltalk",        L"Smalltalk",              L"Smalltalk file",                                    L_SMALLTALK,       "smalltalk"},
	{L"vhdl",             L"VHDL",                   L"VHSIC Hardware Description Language file",          L_VHDL,            "vhdl"},
	{L"kix",              L"KiXtart",                L"KiXtart file",                                      L_KIX,             "kix"},
	{L"autoit",           L"AutoIt",                 L"AutoIt",                                            L_AU3,             "au3"},
	{L"caml",             L"CAML",                   L"Categorical Abstract Machine Language",             L_CAML,            "caml"},
	{L"ada",              L"Ada",                    L"Ada file",                                          L_ADA,             "ada"},
	{L"verilog",          L"Verilog",                L"Verilog file",                                      L_VERILOG,         "verilog"},
	{L"matlab",           L"MATLAB",                 L"MATrix LABoratory",                                 L_MATLAB,          "matlab"},
	{L"haskell",          L"Haskell",                L"Haskell",                                           L_HASKELL,         "haskell"},
	{L"inno",             L"Inno Setup",             L"Inno Setup script",                                 L_INNO,            "inno"},
	{L"searchResult",     L"Internal Search",        L"Internal Search",                                   L_SEARCHRESULT,    "searchResult"},
	{L"cmake",            L"CMake",                  L"CMake file",                                        L_CMAKE,           "cmake"},
	{L"yaml",             L"YAML",                   L"YAML Ain't Markup Language",                        L_YAML,            "yaml"},
	{L"cobol",            L"COBOL",                  L"COmmon Business Oriented Language",                 L_COBOL,           "COBOL"},
	{L"gui4cli",          L"Gui4Cli",                L"Gui4Cli file",                                      L_GUI4CLI,         "gui4cli"},
	{L"d",                L"D",                      L"D programming language",                            L_D,               "d"},
	{L"powershell",       L"PowerShell",             L"Windows PowerShell",                                L_POWERSHELL,      "powershell"},
	{L"r",                L"R",                      L"R programming language",                            L_R,               "r"},
	{L"jsp",              L"JSP",                    L"JavaServer Pages script file",                      L_JSP,             "hypertext"},
	{L"coffeescript",     L"CoffeeScript",           L"CoffeeScript file",                                 L_COFFEESCRIPT,    "coffeescript"},
	{L"json",             L"json",                   L"JSON file",                                         L_JSON,            "json"},
	{L"javascript.js",    L"JavaScript",             L"JavaScript file",                                   L_JAVASCRIPT,      "cpp"},
	{L"fortran77",        L"Fortran fixed form",     L"Fortran fixed form source file",                    L_FORTRAN_77,      "f77"},
	{L"baanc",            L"BaanC",                  L"BaanC File",                                        L_BAANC,           "baan"},
	{L"srec",             L"S-Record",               L"Motorola S-Record binary data",                     L_SREC,            "srec"},
	{L"ihex",             L"Intel HEX",              L"Intel HEX binary data",                             L_IHEX,            "ihex"},
	{L"tehex",            L"Tektronix extended HEX", L"Tektronix extended HEX binary data",                L_TEHEX,           "tehex"},
	{L"swift",            L"Swift",                  L"Swift file",                                        L_SWIFT,           "cpp"},
	{L"asn1",             L"ASN.1",                  L"Abstract Syntax Notation One file",                 L_ASN1,            "asn1"},
	{L"avs",              L"AviSynth",               L"AviSynth scripts files",                            L_AVS,             "avs"},
	{L"blitzbasic",       L"BlitzBasic",             L"BlitzBasic file",                                   L_BLITZBASIC,      "blitzbasic"},
	{L"purebasic",        L"PureBasic",              L"PureBasic file",                                    L_PUREBASIC,       "purebasic"},
	{L"freebasic",        L"FreeBasic",              L"FreeBasic file",                                    L_FREEBASIC,       "freebasic"},
	{L"csound",           L"Csound",                 L"Csound file",                                       L_CSOUND,          "csound"},
	{L"erlang",           L"Erlang",                 L"Erlang file",                                       L_ERLANG,          "erlang"},
	{L"escript",          L"ESCRIPT",                L"ESCRIPT file",                                      L_ESCRIPT,         "escript"},
	{L"forth",            L"Forth",                  L"Forth file",                                        L_FORTH,           "forth"},
	{L"latex",            L"LaTeX",                  L"LaTeX file",                                        L_LATEX,           "latex"},
	{L"mmixal",           L"MMIXAL",                 L"MMIXAL file",                                       L_MMIXAL,          "mmixal"},
	{L"nim",              L"Nim",                    L"Nim file",                                          L_NIM,             "nimrod"},
	{L"nncrontab",        L"Nncrontab",              L"extended crontab file",                             L_NNCRONTAB,       "nncrontab"},
	{L"oscript",          L"OScript",                L"OScript source file",                               L_OSCRIPT,         "oscript"},
	{L"rebol",            L"REBOL",                  L"REBOL file",                                        L_REBOL,           "rebol"},
	{L"registry",         L"registry",               L"registry file",                                     L_REGISTRY,        "registry"},
	{L"rust",             L"Rust",                   L"Rust file",                                         L_RUST,            "rust"},
	{L"spice",            L"Spice",                  L"spice file",                                        L_SPICE,           "spice"},
	{L"txt2tags",         L"txt2tags",               L"txt2tags file",                                     L_TXT2TAGS,        "txt2tags"},
	{L"visualprolog",     L"Visual Prolog",          L"Visual Prolog file",                                L_VISUALPROLOG,    "visualprolog"},
	{L"typescript",       L"TypeScript",             L"TypeScript file",                                   L_TYPESCRIPT,      "cpp"},
	{L"json5",            L"json5",                  L"JSON5 file",                                        L_JSON5,           "json"},
	{L"mssql",            L"mssql",                  L"Microsoft Transact-SQL (SQL Server) file",          L_MSSQL,           "mssql"},
	{L"gdscript",         L"GDScript",               L"GDScript file",                                     L_GDSCRIPT,        "gdscript"},
	{L"hollywood",        L"Hollywood",              L"Hollywood script",                                  L_HOLLYWOOD,       "hollywood"},
	{L"go",               L"Go",                     L"Go source file",                                    L_GOLANG,          "cpp"},
	{L"raku",             L"Raku",                   L"Raku source file",                                  L_RAKU,            "raku"},
	{L"toml",             L"TOML",                   L"Tom's Obvious Minimal Language file",               L_TOML,            "toml"},
	{L"sas",              L"SAS",                    L"SAS file",                                          L_SAS,             "sas"},
	{L"errorlist",        L"ErrorList",              L"ErrorList file",                                    L_ERRORLIST,       "errorlist"},
	{L"escseq",           L"EscapeSequence (ANSI)",  L"Escape Sequence (ANSI) file",                       L_ESCSEQ,          "escseq"},
	{L"fcST",             L"Structured Text",         L"IEC 61131-3 Structured Text file",                  L_STTXT,           "fcST"},
	{L"ext",              L"External",               L"External",                                          L_EXTERNAL,        "null"}
};
