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

// NPLX: port of upstream NppCommands.cpp (Notepad_plus::command). The cases are copied from upstream
// and adapted: WM_UNDO/WM_CUT/WM_COPY/WM_PASTE/WM_CLEAR (understood by the Win32 Scintilla only)
// become SCI_UNDO/SCI_CUT/SCI_COPY/SCI_PASTE/SCI_CLEAR; toolbar, plugin and dialog calls are TODOs.
// Commands that are not ported yet are disabled in the menu (see isCommandPorted in Notepad_plus.cpp).

#include "Notepad_plus.h"
#include "PluginsManager.h"

#include "RunDlg.h"
#include "RunMacroDlg.h"
#include "AboutDlg.h"
#include "ShortcutMapper.h"
#include "columnEditor.h"
#include "FindCharsInRange.h"
#include "md5Dlgs.h"
#include "VerticalFileSwitcher.h"

#include <filesystem>
#include <memory>
#include <mutex>

#include <QSystemTrayIcon>
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QDesktopServices>
#include <QDBusConnection>
#include <QDateTime>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <QUrl>

#include "DesktopIntegration.h"
#include "DocTabView.h"
#include "EncodingMapper.h"
#include "FindReplaceDlg.h"
#include "AnsiCharPanel.h"
#include "DocumentMap.h"
#include "FileBrowser.h"
#include "ProjectPanel.h"
#include "TaskListDlg.h"
#include "WindowsDlg.h"
#include "ClipboardHistoryPanel.h"
#include "FunctionListPanel.h"
#include "GoToLineDlg.h"
#include "preferenceDlg.h"
#include "UserDefineDialog.h"
#include "WordStyleDlg.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "SortLocale.h"

// NPLX: upstream CF_NPPTEXTLEN (the byte length of the CF_TEXT data); the bytes themselves here, NUL bytes included
#define NPLX_MIME_BINARY_TEXT "application/x-notepadlx-binary-text"
#include "UiServices.h"
#include "Sorters.h"
#include "menuCmdID.h"

using std::pair;
using std::wstring;

namespace
{
	// upstream Common.cpp buf2Clipboard. NPLX: LF line ends (upstream CRLF)
	bool buf2Clipboard(const std::vector<Buffer*>& buffers, bool isFullPath)
	{
		const wstring eol = L"\n";
		wstring selection;
		for (auto&& buf : buffers)
		{
			if (buf)
			{
				const wchar_t* fileName = isFullPath ? buf->getFullPathName() : buf->getFileName();
				if (fileName)
					selection += fileName;
			}

			if (!selection.empty() && !selection.ends_with(eol))
				selection += eol;
		}

		if (!selection.empty())
		{
			QGuiApplication::clipboard()->setText(QString::fromStdWString(selection)); // NPLX: was str2Clipboard
			return true;
		}
		return false;
	}

	std::mutex command_mutex;

	using nplx::desktop::showFileInFileManager; // NPLX: replacement for upstream openInExplorerAndSelect
}

void Notepad_plus::command(int id)
{
	switch (id)
	{
		case IDM_FILE_NEW:
		{
			fileNew();
		}
		break;

		case IDM_FILE_OPEN:
		{
			fileOpen();
		}
		break;

		case IDM_FILE_OPEN_FOLDER:
		{
			showFileInFileManager(QString::fromWCharArray(_pEditView->getCurrentBuffer()->getFullPathName()));
			break;
		}

		case IDM_FILE_OPEN_DEFAULT_VIEWER:
		{
			// upstream: ShellExecute "open" (the associated application)
			QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromWCharArray(_pEditView->getCurrentBuffer()->getFullPathName())));
			break;
		}

		case IDM_FILE_RELOAD:
			fileReload();
			break;

		case IDM_FILE_CLOSE:
			if (fileClose())
				checkDocState();
			break;

		case IDM_FILE_CLOSEALL:
		{
			bool isSnapshotMode = NppParameters::getInstance().getNppGUI().isSnapshotMode();
			fileCloseAll(isSnapshotMode, false);
			checkDocState();
			break;
		}

		case IDM_FILE_SAVE:
			fileSave();
			break;

		case IDM_FILE_SAVEALL:
			fileSaveAll();
			break;

		case IDM_FILE_SAVEAS:
			fileSaveAs();
			break;

		case IDM_FILE_SAVECOPYAS:
			fileSaveAs(BUFFER_INVALID, true);
			break;

		case IDM_FILE_DELETE:
		{
			if (fileDelete())
				checkDocState();
			break;
		}

		case IDM_FILE_RENAME:
		{
			fileRename();
			break;
		}

		case IDM_FILE_EXIT:
			close(); // upstream: PostMessage(WM_CLOSE)
			break;

		case IDM_EDIT_UNDO:
		{
			std::lock_guard<std::mutex> lock(command_mutex);
			_pEditView->execute(SCI_UNDO); // NPLX: was WM_UNDO
			break;
		}

		case IDM_EDIT_REDO:
		{
			std::lock_guard<std::mutex> lock(command_mutex);
			_pEditView->execute(SCI_REDO);
			break;
		}

		case IDM_EDIT_CUT:
		{
			if (_pEditView->hasFocus())
			{
				if (_pEditView->hasSelection()) // Cut normally
				{
					_pEditView->execute(SCI_CUT); // NPLX: was WM_CUT
				}
				else if (NppParameters::getInstance().getSVP()._lineCopyCutWithoutSelection) // Cut the entire line with EOL
				{
					_pEditView->execute(SCI_COPYALLOWLINE);
					_pEditView->execute(SCI_LINEDELETE);
				}
			}
			else if (QWidget* w = QApplication::focusWidget())
			{
				QMetaObject::invokeMethod(w, "cut"); // NPLX: was SendMessage(focusedHwnd, WM_CUT)
			}
			break;
		}

		case IDM_EDIT_COPY:
		{
			if (_pEditView->hasFocus())
			{
				if (_pEditView->hasSelection())
				{
					_pEditView->execute(SCI_COPY); // NPLX: was WM_COPY
				}
				else if (NppParameters::getInstance().getSVP()._lineCopyCutWithoutSelection)
				{
					_pEditView->execute(SCI_COPYALLOWLINE); // Copy without selected text, it will copy the whole line with EOL, for pasting before any line where the caret is.
				}
			}
			else if (QWidget* w = QApplication::focusWidget())
			{
				QMetaObject::invokeMethod(w, "copy");
			}
			break;
		}

		case IDM_EDIT_COPY_BINARY:
		case IDM_EDIT_CUT_BINARY:
		{
			size_t textLen = _pEditView->execute(SCI_GETSELTEXT, 0, 0);
			if (!textLen)
				return;

			const size_t strSize = textLen + 1;
			auto pBinText = std::make_unique<char[]>(strSize);
			std::fill_n(pBinText.get(), strSize, '\0');

			_pEditView->getSelectedTextToMultiChar(pBinText.get(), textLen + 1);

			// NPLX: upstream puts the bytes as CF_TEXT and their length as CF_NPPTEXTLEN; here the bytes as
			// text/plain and, untouched (NUL bytes included), as NotePadLX's binary format
			const QByteArray bytes(pBinText.get(), static_cast<qsizetype>(textLen));
			pBinText.reset(nullptr); // free possible big membuf ASAP
			auto* mime = new QMimeData;
			mime->setText(QString::fromUtf8(bytes));
			mime->setData(QStringLiteral(NPLX_MIME_BINARY_TEXT), bytes);
			QGuiApplication::clipboard()->setMimeData(mime);

			if (id == IDM_EDIT_CUT_BINARY)
				_pEditView->execute(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(""));

			break;
		}

		case IDM_EDIT_PASTE_BINARY:
		{
			std::lock_guard<std::mutex> lock(command_mutex);
			const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
			if (!mime || !mime->hasText())
				return;

			if (mime->hasFormat(QStringLiteral(NPLX_MIME_BINARY_TEXT)))
			{
				const QByteArray bytes = mime->data(QStringLiteral(NPLX_MIME_BINARY_TEXT));
				_pEditView->execute(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(""));
				_pEditView->execute(SCI_ADDTEXT, static_cast<uptr_t>(bytes.size()), reinterpret_cast<LPARAM>(bytes.constData()));
			}
			else
			{
				const QByteArray text = mime->text().toUtf8();
				_pEditView->execute(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(text.constData()));
			}
		}
		break;

		case IDM_EDIT_PASTE_AS_RTF:
		case IDM_EDIT_PASTE_AS_HTML:
		{
			// upstream: the "HTML Format" / "Rich Text Format" clipboard data, as text. NPLX: text/html, text/rtf
			std::lock_guard<std::mutex> lock(command_mutex);
			const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
			if (!mime)
				return;

			QByteArray data;
			const QStringList formats = (id == IDM_EDIT_PASTE_AS_HTML)
				? QStringList{ QStringLiteral("text/html") }
				: QStringList{ QStringLiteral("text/rtf"), QStringLiteral("application/rtf"), QStringLiteral("text/richtext") };
			for (const QString& f : formats)
			{
				if (mime->hasFormat(f))
				{
					data = mime->data(f);
					break;
				}
			}
			if (data.isEmpty())
				return;

			data.append('\0');
			_pEditView->execute(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(data.constData()));
		}
		break;

		case IDM_EDIT_REDACT_SELECTION:
		{
			_pEditView->execute(SCI_BEGINUNDOACTION);

			const int selCount = static_cast<int>(_pEditView->execute(SCI_GETSELECTIONS));
			const bool useBullet = QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier; // GetKeyState(VK_SHIFT)

			const int codePage = static_cast<int>(_pEditView->execute(SCI_GETCODEPAGE));
			const bool isUnicode = (codePage == SC_CP_UTF8);

			std::string maskSymbol;
			if (isUnicode)
				maskSymbol = useBullet ? "\xE2\x97\x8F" : "\xE2\x96\x88"; // ● or █ (UTF-8)
			else
				maskSymbol = useBullet ? "." : "#";


			for (int i = 0; i < selCount; ++i)
			{
				WPARAM start = _pEditView->execute(SCI_GETSELECTIONNSTART, i);
				LPARAM end = _pEditView->execute(SCI_GETSELECTIONNEND, i);
				int charCount = static_cast<int>(_pEditView->execute(SCI_COUNTCHARACTERS, start, end));

				if (charCount > 0)
				{

					std::string maskStr;

					for (int j = 0; j < charCount; ++j)
					{
						// Get byte position of the jth character, starting from "start"
						auto bytePos = _pEditView->execute(SCI_POSITIONRELATIVE, start, j);
						char aChar = static_cast<char>(_pEditView->execute(SCI_GETCHARAT, bytePos));

						if (aChar == '\r' || aChar == '\n')
							maskStr += aChar;
						else
							maskStr += maskSymbol;
					}

					_pEditView->execute(SCI_SETTARGETRANGE, start, end);
					_pEditView->execute(SCI_REPLACETARGET, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(maskStr.c_str()));
				}
			}

			_pEditView->execute(SCI_ENDUNDOACTION);
		}
		break;

		case IDM_EDIT_OPENSELECTEDFILEFOLDERINEXPLORER:
		case IDM_EDIT_OPENSELECTEDFILETOEDIT:
		{
			if (_pEditView->execute(SCI_GETSELECTIONS) != 1) // Multi-Selection || Column mode || no selection
				return;

			const int strSize = CURRENTWORD_MAXLENGTH;
			auto currentWord = std::make_unique<wchar_t[]>(strSize);
			std::fill_n(currentWord.get(), strSize, L'\0');

			nppMessage(NPPM_GETFILENAMEATCURSOR, CURRENTWORD_MAXLENGTH, reinterpret_cast<LPARAM>(currentWord.get()));

			// Try to expand environment strings, nevertheless currentWord is copied with or without expansion
			// NPLX: %VAR%, $VAR and ${VAR} (upstream ::ExpandEnvironmentStringsW); ~ is the home folder
			std::wstring fullTargetPath = expandEnvironmentStrings(currentWord.get());
			if (fullTargetPath.size() > 1 && fullTargetPath[0] == L'~' && fullTargetPath[1] == L'/')
				fullTargetPath = QDir::homePath().toStdWString() + fullTargetPath.substr(1);

			if (!doesPathExist(fullTargetPath.c_str()))
			{
				// Concatenate relative path
				auto currentDir = std::make_unique<wchar_t[]>(strSize);
				std::fill_n(currentDir.get(), strSize, L'\0');
				nppMessage(NPPM_GETCURRENTDIRECTORY, CURRENTWORD_MAXLENGTH, reinterpret_cast<LPARAM>(currentDir.get()));

				fullTargetPath = currentDir.get();
				fullTargetPath += L"/"; // NPLX: was L"\\"
				fullTargetPath += currentWord.get();
			}

			if (id == IDM_EDIT_OPENSELECTEDFILEFOLDERINEXPLORER)
			{
				if (!doesPathExist(fullTargetPath.c_str()))
				{
					_nativeLangSpeaker.messageBox("FilePathNotFoundWarning",
						L"The path you're trying to open doesn't exist.",
						L"Open Path",
						MB_OK | MB_APPLMODAL);
					return;
				}

				std::filesystem::path canonicalPath(fullTargetPath);
				canonicalPath = canonicalPath.lexically_normal();

				// openInExplorerAndSelect, or the folder itself
				const QString path = QString::fromStdWString(canonicalPath.wstring());
				if (QFileInfo(path).isDir())
					nplx::desktop::openFolder(path);
				else
					nplx::desktop::showFileInFileManager(path);
			}
			else // IDM_EDIT_OPENSELECTEDFILETOEDIT
			{
				if (!doesFileExist(fullTargetPath.c_str()))
				{
					_nativeLangSpeaker.messageBox("FilePathNotFoundWarning",
						L"The path you're trying to open doesn't exist.",
						L"Open Path",
						MB_OK | MB_APPLMODAL);
					return;
				}

				// NPLX: upstream runs notepad++.exe with the file, which hands it to the running instance; open it here
				const BufferID bufID = doOpen(std::filesystem::path(fullTargetPath).lexically_normal().wstring());
				if (bufID != BUFFER_INVALID)
					switchToFile(bufID);
			}
			break;
		}

		case IDM_EDIT_PASTE:
		{
			std::lock_guard<std::mutex> lock(command_mutex);
			if (_pEditView->hasFocus())
			{
				size_t nbSelections = _pEditView->execute(SCI_GETSELECTIONS);
				Buffer* buf = getCurrentBuffer();
				bool isRO = buf->isReadOnly();
				LRESULT selectionMode = _pEditView->execute(SCI_GETSELECTIONMODE);
				if (nbSelections > 1 && !isRO && selectionMode == SC_SEL_STREAM)
				{
					bool isPasteDone = _pEditView->pasteToMultiSelection();
					if (isPasteDone)
						return;
				}

				_pEditView->execute(SCI_PASTE);
			}
			else if (QWidget* w = QApplication::focusWidget())
			{
				QMetaObject::invokeMethod(w, "paste");
			}
			break;
		}

		case IDM_EDIT_DELETE:
			_pEditView->execute(SCI_CLEAR); // NPLX: was WM_CLEAR
			break;

		case IDM_MACRO_STARTRECORDINGMACRO:
		case IDM_MACRO_STOPRECORDINGMACRO:
		case IDC_EDIT_TOGGLEMACRORECORDING:
		{
			toggleMacroRecording(); // NPLX: upstream body in NppMacro.cpp
			break;
		}

		case IDM_MACRO_PLAYBACKRECORDEDMACRO:
			if (!_recordingMacro) // if we're not currently recording, then playback the recorded keystrokes
			{
				macroPlayback(_macro);
			}
			break;

		case IDM_MACRO_RUNMULTIMACRODLG :
		{
			if (!_recordingMacro) // if we're not currently recording, then playback the recorded keystrokes
			{
				_runMacroDlg->doDialog();
				break;

			}
		}
		break;

		case IDM_MACRO_SAVECURRENTMACRO :
		{
			if (addCurrentMacro())
			{
				_recordingSaved = true;
				_runMacroDlg->initMacroList();
				checkMacroState();
			}
			break;
		}

		case IDM_VIEW_TAB_COLOUR_NONE:
		case IDM_VIEW_TAB_COLOUR_1:
		case IDM_VIEW_TAB_COLOUR_2:
		case IDM_VIEW_TAB_COLOUR_3:
		case IDM_VIEW_TAB_COLOUR_4:
		case IDM_VIEW_TAB_COLOUR_5:
		{
			const int color_id = (id - IDM_VIEW_TAB_COLOUR_NONE) - 1;
			const auto current_index = _pDocTab->getCurrentTabIndex();
			BufferID buffer_id = _pDocTab->getBufferByIndex(current_index);
			_pDocTab->setIndividualTabColour(buffer_id, color_id);
			_pDocTab->update(); // upstream redraw

			if (_pDocumentListPanel != nullptr)
				_pDocumentListPanel->setItemColor(buffer_id);
			break;
		}

		case IDM_VIEW_GOTO_NEW_INSTANCE :
			docOpenInNewInstance(TransferMove);
			break;

		case IDM_VIEW_LOAD_IN_NEW_INSTANCE:
			docOpenInNewInstance(TransferClone);
			break;

		case IDM_FILE_PRINT :
			filePrint(true);
			break;

		case IDM_FILE_PRINTNOW :
			filePrint(false);
			break;

		case IDM_SETTING_PREFERENCE:
		{
			// upstream: _preference.doDialog(isRTL); NPLX: created on first use, with the messages to this window
			const bool isFirstTime = (_preference == nullptr);
			if (isFirstTime)
			{
				_preference = new PreferenceDlg(this);
				_preference->init([this](unsigned int message, uptr_t wParam, sptr_t lParam) { return nppMessage(message, wParam, lParam); });
			}
			_preference->doDialog(_nativeLangSpeaker.isRTL()); // translated when created (changePreferenceDlgLang)
			break;
		}

		case IDM_EDIT_CHANGESEARCHENGINE:
		{
			command(IDM_SETTING_PREFERENCE);
			_preference->showDialogByName(L"SearchEngine");
			break;
		}

		case IDM_SETTING_SHORTCUT_MAPPER :
		case IDM_SETTING_SHORTCUT_MAPPER_MACRO :
		case IDM_SETTING_SHORTCUT_MAPPER_RUN :
		{
			GridState st = id==IDM_SETTING_SHORTCUT_MAPPER_MACRO?STATE_MACRO:id==IDM_SETTING_SHORTCUT_MAPPER_RUN?STATE_USER:STATE_MENU;
			ShortcutMapper shortcutMapper(this);
			ShortcutMapperHost host;
			host._macroMenu = [this]() { return nppMenuOfCommand(IDM_MACRO_STARTRECORDINGMACRO); };
			host._runMenu = [this]() { return nppMenuOfCommand(IDM_EXECUTE); };
			host._updateShortcuts = [this]() { updateShortcuts(); };
			host._updateScintillaKeys = [this]() { updateScintillaKeys(); };
			host._isRemovedCommand = [](int cmdID) { return isRemovedOnLinux(cmdID); };
			host._pluginShortcutModified = [this](int cmdID, const KeyCombo& key) {
				// upstream NPPM_INTERNAL_PLUGINSHORTCUTMODIFIED: NPPN_SHORTCUTREMAPPED with the new ShortcutKey
				ShortcutKey shortcut;
				shortcut._isAlt = key._isAlt;
				shortcut._isCtrl = key._isCtrl;
				shortcut._isShift = key._isShift;
				shortcut._key = key._key;
				notifyPlugins(NPPN_SHORTCUTREMAPPED, &shortcut, static_cast<uptr_t>(cmdID));
			};
			shortcutMapper.init(std::move(host), st);
			_pShortcutMapper = &shortcutMapper;
			shortcutMapper.doDialog();
			_pShortcutMapper = nullptr;
			_runMacroDlg->initMacroList(); // NPLX: upstream relies on RunMacroDlg::doDialog reloading the list
			checkMacroState();
		}
		break;

		case IDM_EDIT_COLUMNMODE :
		{
			if (!_colEditorDlg)
			{
				// upstream Notepad_plus::init: _colEditorDlg.init(_pPublicInterface->getHinst(), hwnd, &_pEditView)
				_colEditorDlg = new ColumnEditorDlg(this);
				_colEditorDlg->init(&_pEditView);
			}
			_colEditorDlg->doDialog();
		}
		break;

		case IDM_TOOL_MD5_GENERATE:
		case IDM_TOOL_SHA1_GENERATE:
		case IDM_TOOL_SHA256_GENERATE:
		case IDM_TOOL_SHA512_GENERATE:
		{
			// upstream: _md5FromTextDlg.doDialog() ... (one case per hash); NPLX: the dialogs are created on first use
			HashFromTextDlg*& dlg = (id == IDM_TOOL_MD5_GENERATE) ? _md5FromTextDlg : (id == IDM_TOOL_SHA1_GENERATE) ? _sha1FromTextDlg
				: (id == IDM_TOOL_SHA256_GENERATE) ? _sha2FromTextDlg : _sha512FromTextDlg;
			if (!dlg)
			{
				dlg = new HashFromTextDlg(this);
				dlg->setHashType((id == IDM_TOOL_MD5_GENERATE) ? hash_md5 : (id == IDM_TOOL_SHA1_GENERATE) ? hash_sha1
					: (id == IDM_TOOL_SHA256_GENERATE) ? hash_sha256 : hash_sha512);
			}
			dlg->doDialog();
		}
		break;

		case IDM_TOOL_MD5_GENERATEFROMFILE:
		case IDM_TOOL_SHA1_GENERATEFROMFILE:
		case IDM_TOOL_SHA256_GENERATEFROMFILE:
		case IDM_TOOL_SHA512_GENERATEFROMFILE:
		{
			HashFromFilesDlg*& dlg = (id == IDM_TOOL_MD5_GENERATEFROMFILE) ? _md5FromFilesDlg : (id == IDM_TOOL_SHA1_GENERATEFROMFILE) ? _sha1FromFilesDlg
				: (id == IDM_TOOL_SHA256_GENERATEFROMFILE) ? _sha2FromFilesDlg : _sha512FromFilesDlg;
			if (!dlg)
			{
				dlg = new HashFromFilesDlg(this);
				dlg->setHashType((id == IDM_TOOL_MD5_GENERATEFROMFILE) ? hash_md5 : (id == IDM_TOOL_SHA1_GENERATEFROMFILE) ? hash_sha1
					: (id == IDM_TOOL_SHA256_GENERATEFROMFILE) ? hash_sha256 : hash_sha512);
			}
			dlg->doDialog();
		}
		break;

		case IDM_TOOL_MD5_GENERATEINTOCLIPBOARD:
		case IDM_TOOL_SHA1_GENERATEINTOCLIPBOARD:
		case IDM_TOOL_SHA256_GENERATEINTOCLIPBOARD:
		case IDM_TOOL_SHA512_GENERATEINTOCLIPBOARD:
		{
			if (_pEditView->execute(SCI_GETSELECTIONS) == 1)
			{
				size_t selectionStart = _pEditView->execute(SCI_GETSELECTIONSTART);
				size_t selectionEnd = _pEditView->execute(SCI_GETSELECTIONEND);

				intptr_t strLen = selectionEnd - selectionStart;
				if (strLen)
				{
					intptr_t strSize = strLen + 1;
					std::string selectedStr(strSize, '\0');
					_pEditView->execute(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(selectedStr.data()));

					const hashType ht = (id == IDM_TOOL_MD5_GENERATEINTOCLIPBOARD) ? hash_md5
						: (id == IDM_TOOL_SHA1_GENERATEINTOCLIPBOARD) ? hash_sha1
						: (id == IDM_TOOL_SHA256_GENERATEINTOCLIPBOARD) ? hash_sha256 : hash_sha512;
					// upstream hashes strlen(selectedStr) bytes
					const std::string hashStr = hashToHex(ht, selectedStr.c_str(), strlen(selectedStr.c_str()));
					QGuiApplication::clipboard()->setText(QString::fromStdString(hashStr)); // str2Clipboard
				}
			}
		}
		break;

		case IDM_EXECUTE:
		{
			_runDlg->doDialog();
			break;
		}

		case IDM_EXECUTE_VALIDATE_SHORTCUTSXML:
		{
			validateShortcutsXml(); // NPLX: upstream body in NppMacro.cpp
			break;
		}

		case IDM_EDIT_SELECTALL:
			_pEditView->execute(SCI_SELECTALL);
			break;

		case IDM_EDIT_FULLPATHTOCLIP:
		case IDM_EDIT_CURRENTDIRTOCLIP:
		case IDM_EDIT_FILENAMETOCLIP:
		{
			Buffer* buf = _pEditView->getCurrentBuffer();
			wstring text;
			if (id == IDM_EDIT_FULLPATHTOCLIP)
			{
				text = buf->getFullPathName();
			}
			else if (id == IDM_EDIT_CURRENTDIRTOCLIP)
			{
				wstring dir(buf->getFullPathName());
				pathRemoveFileSpec(dir);
				text = dir;
			}
			else if (id == IDM_EDIT_FILENAMETOCLIP)
			{
				text = buf->getFileName();
			}
			QGuiApplication::clipboard()->setText(QString::fromStdWString(text)); // NPLX: was str2Clipboard
			break;
		}

		case IDM_EDIT_INS_TAB:
		case IDM_EDIT_RMV_TAB:
		{
			bool forwards = id == IDM_EDIT_INS_TAB;
			size_t selStartPos = _pEditView->execute(SCI_GETSELECTIONSTART);
			size_t lineNumber = _pEditView->execute(SCI_LINEFROMPOSITION, selStartPos);
			size_t nbSelections = _pEditView->execute(SCI_GETSELECTIONS);
			size_t selEndPos = _pEditView->execute(SCI_GETSELECTIONEND);
			size_t selEndLineNumber = _pEditView->execute(SCI_LINEFROMPOSITION, selEndPos);
			if ((nbSelections > 1) || (lineNumber != selEndLineNumber))
			{
				// multiple-selection or multi-line selection; use Scintilla SCI_TAB / SCI_BACKTAB behavior
				_pEditView->execute(forwards ? SCI_TAB : SCI_BACKTAB);
			}
			else
			{
				// zero-length selection (simple single caret) or selected text is all on single line
				// depart from Scintilla behavior and do it our way
				size_t currentIndent = _pEditView->execute(SCI_GETLINEINDENTATION, lineNumber);
				intptr_t indentDelta = _pEditView->execute(SCI_GETTABWIDTH);
				if (!forwards) indentDelta = -indentDelta;
				_pEditView->setLineIndent(lineNumber, static_cast<intptr_t>(currentIndent) + indentDelta);
			}
		}
		break;

		case IDM_EDIT_DUP_LINE:
			_pEditView->execute(SCI_LINEDUPLICATE);
			break;

		case IDM_EDIT_REMOVE_ANY_DUP_LINES:
			_pEditView->execute(SCI_BEGINUNDOACTION);
			_pEditView->removeAnyDuplicateLines();
			_pEditView->execute(SCI_ENDUNDOACTION);
			break;

		case IDM_EDIT_SPLIT_LINES:
		{
			if (_pEditView->execute(SCI_GETSELECTIONS) == 1)
			{
				pair<size_t, size_t> lineRange = _pEditView->getSelectionLinesRange();
				auto anchorPos = _pEditView->execute(SCI_POSITIONFROMLINE, lineRange.first);
				auto caretPos = _pEditView->execute(SCI_GETLINEENDPOSITION, lineRange.second);
				_pEditView->execute(SCI_SETSELECTION, caretPos, anchorPos);
				_pEditView->execute(SCI_TARGETFROMSELECTION);
				size_t edgeMode = _pEditView->execute(SCI_GETEDGEMODE);
				if (edgeMode == EDGE_NONE)
				{
					_pEditView->execute(SCI_LINESSPLIT, 0);
				}
				else
				{
					auto textWidth = _pEditView->execute(SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>("P"));
					auto edgeCol = _pEditView->execute(SCI_GETEDGECOLUMN); // will work for edgeMode == EDGE_BACKGROUND
					if (edgeMode == EDGE_MULTILINE)
					{
						NppParameters& nppParam = NppParameters::getInstance();
						ScintillaViewParams& svp = const_cast<ScintillaViewParams&>(nppParam.getSVP());
						edgeCol = svp._edgeMultiColumnPos.back();  // the LAST edge column specified by the user
					}
					++edgeCol;  // compensate for zero-based column number
					_pEditView->execute(SCI_LINESSPLIT, textWidth * edgeCol);
				}
			}
		}
		break;

		case IDM_EDIT_JOIN_LINES:
		{
			const pair<size_t, size_t> lineRange = _pEditView->getSelectionLinesRange();
			if (lineRange.first != lineRange.second)
			{
				auto anchorPos = _pEditView->execute(SCI_POSITIONFROMLINE, lineRange.first);
				auto caretPos = _pEditView->execute(SCI_GETLINEENDPOSITION, lineRange.second);
				_pEditView->execute(SCI_SETSELECTION, caretPos, anchorPos);
				_pEditView->execute(SCI_TARGETFROMSELECTION);
				_pEditView->execute(SCI_LINESJOIN);
			}
		}
		break;

		case IDM_EDIT_LINE_UP:
			_pEditView->currentLinesUp();
			break;

		case IDM_EDIT_LINE_DOWN:
			_pEditView->currentLinesDown();
			break;

		case IDM_EDIT_BLANKLINEABOVECURRENT:
		{
			_pEditView->insertNewLineAboveCurrentLine();
		}
		break;

		case IDM_EDIT_BLANKLINEBELOWCURRENT:
		{
			_pEditView->insertNewLineBelowCurrentLine();
		}
		break;

		case IDM_EDIT_UPPERCASE:
			_pEditView->convertSelectedTextToUpperCase();
			break;

		case IDM_EDIT_LOWERCASE:
			_pEditView->convertSelectedTextToLowerCase();
			break;

		case IDM_EDIT_PROPERCASE_FORCE:
			_pEditView->convertSelectedTextToNewerCase(PROPERCASE_FORCE);
			break;

		case IDM_EDIT_PROPERCASE_BLEND:
			_pEditView->convertSelectedTextToNewerCase(PROPERCASE_BLEND);
			break;

		case IDM_EDIT_SENTENCECASE_FORCE:
			_pEditView->convertSelectedTextToNewerCase(SENTENCECASE_FORCE);
			break;

		case IDM_EDIT_SENTENCECASE_BLEND:
			_pEditView->convertSelectedTextToNewerCase(SENTENCECASE_BLEND);
			break;

		case IDM_EDIT_INVERTCASE:
			_pEditView->convertSelectedTextToNewerCase(INVERTCASE);
			break;

		case IDM_EDIT_RANDOMCASE:
			_pEditView->convertSelectedTextToNewerCase(RANDOMCASE);
			break;

		case IDM_EDIT_TOGGLEREADONLY:
		{
			Buffer* buf = _pEditView->getCurrentBuffer();
			buf->setUserReadOnly(!buf->getUserReadOnly());
			break;
		}

		case IDM_SEARCH_FIND :
		case IDM_SEARCH_REPLACE :
		case IDM_SEARCH_MARK :
		{
			DIALOG_TYPE dlgID = FIND_DLG;
			if (id == IDM_SEARCH_REPLACE)
				dlgID = REPLACE_DLG;
			else if (id == IDM_SEARCH_MARK)
				dlgID = MARK_DLG;
			_findReplaceDlg->doDialog(dlgID, _nativeLangSpeaker.isRTL());

			_findReplaceDlg->setSearchTextWithSettings();

			setFindReplaceFolderFilter(NULL, NULL);
			// upstream changeFindReplaceDlgLang on first use: done when the dialog is created
			break;
		}

		case IDM_SEARCH_FINDINFILES:
		{
			// upstream NPPM_LAUNCHFINDINFILESDLG(0, 0)
			_findReplaceDlg->doDialog(FINDINFILES_DLG, _nativeLangSpeaker.isRTL());

			_findReplaceDlg->setSearchTextWithSettings();

			setFindReplaceFolderFilter(NULL, NULL);
			break;
		}

		case IDM_SEARCH_FINDINCREMENT :
		{
			// upstream: the accelerator table of the incremental search bar while it has the focus
			if (_incrementFindDlg->isVisibleBar() && _incrementFindDlg->hasFocusWithin())
			{
				_incrementFindDlg->command(IDM_SEARCH_FINDINCREMENT);
				break;
			}

			auto str = _pEditView->getSelectedTextToWChar(false);
			if (!str.empty())         // the selected text is not empty, then use it
				_incrementFindDlg->setSearchText(str.c_str(), _pEditView->getCurrentBuffer()->getUnicodeMode() != uni8Bit);

			_incrementFindDlg->display();
		}
		break;

		case IDM_SEARCH_FINDNEXT :
		case IDM_SEARCH_FINDPREV :
		{
			// upstream: the accelerator table of the incremental search bar while it has the focus
			if (_incrementFindDlg->isVisibleBar() && _incrementFindDlg->hasFocusWithin())
			{
				_incrementFindDlg->command(id);
				break;
			}
			if (_findReplaceDlg->isCreated())
			{
				FindOption op = _findReplaceDlg->getCurrentOptions();
				const NppParameters& nppParams = NppParameters::getInstance();
				if ((id == IDM_SEARCH_FINDPREV) && (op._searchType == FindRegex) && !nppParams.regexBackward4PowerUser())
				{
					// regex upward search is disabled
					// make this a no-action command

					_findReplaceDlg->regexBackwardMsgBox();
				}
				else
				{
					op._whichDirection = (id == IDM_SEARCH_FINDNEXT ? DIR_DOWN : DIR_UP);
					wstring s = _findReplaceDlg->getText2search();
					FindStatus status = FSNoMessage;
					_findReplaceDlg->processFindNext(s.c_str(), &op, &status);
					if (status == FSEndReached)
					{
						wstring msg = _nativeLangSpeaker.getLocalizedStrFromID("find-status-end-reached", FIND_STATUS_END_REACHED_TEXT);
						_findReplaceDlg->setStatusbarMessage(msg, FSEndReached);
					}
					else if (status == FSTopReached)
					{
						wstring msg = _nativeLangSpeaker.getLocalizedStrFromID("find-status-top-reached", FIND_STATUS_TOP_REACHED_TEXT);
						_findReplaceDlg->setStatusbarMessage(msg, FSTopReached);
					}
				}
			}
		}
		break;

		case IDM_SEARCH_SETANDFINDNEXT :
		case IDM_SEARCH_SETANDFINDPREV :
		{
			bool isFirstTime = !_findReplaceDlg->isCreated();
			if (isFirstTime)
				_findReplaceDlg->doDialog(FIND_DLG, _nativeLangSpeaker.isRTL(), false);

			wstring str = _findReplaceDlg->setSearchText();
			if (str.empty()) return;

			_findReplaceDlg->_env->_str2Search = str;

			setFindReplaceFolderFilter(NULL, NULL);

			FindOption op = _findReplaceDlg->getCurrentOptions();
			op._searchType = FindNormal;
			op._whichDirection = (id == IDM_SEARCH_SETANDFINDNEXT?DIR_DOWN:DIR_UP);

			FindStatus status = FSNoMessage;
			_findReplaceDlg->processFindNext(str.c_str(), &op, &status);
			if (status == FSEndReached)
			{
				wstring msg = _nativeLangSpeaker.getLocalizedStrFromID("find-status-end-reached", FIND_STATUS_END_REACHED_TEXT);
				_findReplaceDlg->setStatusbarMessage(msg, FSEndReached);
			}
			else if (status == FSTopReached)
			{
				wstring msg = _nativeLangSpeaker.getLocalizedStrFromID("find-status-top-reached", FIND_STATUS_TOP_REACHED_TEXT);
				_findReplaceDlg->setStatusbarMessage(msg, FSTopReached);
			}
		}
		break;

		case IDM_SEARCH_GOTONEXTFOUND:
		{
			_findReplaceDlg->gotoNextFoundResult();
		}
		break;

		case IDM_SEARCH_GOTOPREVFOUND:
		{
			_findReplaceDlg->gotoNextFoundResult(-1);
		}
		break;

		case IDM_FOCUS_ON_FOUND_RESULTS:
		{
			ScintillaEditView* finderView = _findReplaceDlg->getHFindResults();
			if (finderView && finderView->hasFocus())
				// focus already on found results, switch to current edit view
				switchEditViewTo(currentView());
			else
				_findReplaceDlg->focusOnFinder();
		}
		break;

		case IDM_SEARCH_VOLATILE_FINDNEXT :
		case IDM_SEARCH_VOLATILE_FINDPREV :
		{
			auto str = _pEditView->getSelectedTextToWChar();
			if (str.empty()) return;

			FindOption op;
			op._isMatchCase = false;
			op._isWholeWord = false;
			op._isWrapAround = true;
			op._searchType = FindNormal;
			op._whichDirection = (id == IDM_SEARCH_VOLATILE_FINDNEXT ? DIR_DOWN : DIR_UP);

			FindStatus status = FSNoMessage;
			_findReplaceDlg->processFindNext(str.c_str(), &op, &status);
			if (status == FSEndReached)
			{
				wstring msg = _nativeLangSpeaker.getLocalizedStrFromID("find-status-end-reached", FIND_STATUS_END_REACHED_TEXT);
				_findReplaceDlg->setStatusbarMessage(msg, FSEndReached);
			}
			else if (status == FSTopReached)
			{
				wstring msg = _nativeLangSpeaker.getLocalizedStrFromID("find-status-top-reached", FIND_STATUS_TOP_REACHED_TEXT);
				_findReplaceDlg->setStatusbarMessage(msg, FSTopReached);
			}
		}
		break;

		case IDM_SEARCH_MARKALLEXT1 :
		case IDM_SEARCH_MARKALLEXT2 :
		case IDM_SEARCH_MARKALLEXT3 :
		case IDM_SEARCH_MARKALLEXT4 :
		case IDM_SEARCH_MARKALLEXT5 :
		{
			int styleID;
			if (id == IDM_SEARCH_MARKALLEXT1)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT1;
			else if (id == IDM_SEARCH_MARKALLEXT2)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT2;
			else if (id == IDM_SEARCH_MARKALLEXT3)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT3;
			else if (id == IDM_SEARCH_MARKALLEXT4)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT4;
			else // (id == IDM_SEARCH_MARKALLEXT5)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT5;

			auto selectedText = _pEditView->getSelectedTextToWChar(true);

			if (!selectedText.empty())
			{
				_findReplaceDlg->markAll(selectedText.c_str(), styleID);
			}
		}
		break;

		case IDM_SEARCH_MARKONEEXT1:
		case IDM_SEARCH_MARKONEEXT2:
		case IDM_SEARCH_MARKONEEXT3:
		case IDM_SEARCH_MARKONEEXT4:
		case IDM_SEARCH_MARKONEEXT5:
		{
			int styleID;
			if (id == IDM_SEARCH_MARKONEEXT1)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT1;
			else if (id == IDM_SEARCH_MARKONEEXT2)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT2;
			else if (id == IDM_SEARCH_MARKONEEXT3)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT3;
			else if (id == IDM_SEARCH_MARKONEEXT4)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT4;
			else // (id == IDM_SEARCH_MARKONEEXT5)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT5;

			Sci_CharacterRangeFull range = _pEditView->getSelection();
			if (range.cpMin == range.cpMax)
			{
				auto caretPos = _pEditView->execute(SCI_GETCURRENTPOS, 0, 0);
				range.cpMin = _pEditView->execute(SCI_WORDSTARTPOSITION, caretPos, true);
				range.cpMax = _pEditView->execute(SCI_WORDENDPOSITION, caretPos, true);
			}
			if (range.cpMax > range.cpMin)
			{
				_pEditView->execute(SCI_SETINDICATORCURRENT, styleID);
				_pEditView->execute(SCI_INDICATORFILLRANGE, range.cpMin, range.cpMax - range.cpMin);
			}
		}
		break;

		case IDM_SEARCH_UNMARKALLEXT1 :
		case IDM_SEARCH_UNMARKALLEXT2 :
		case IDM_SEARCH_UNMARKALLEXT3 :
		case IDM_SEARCH_UNMARKALLEXT4 :
		case IDM_SEARCH_UNMARKALLEXT5 :
		{
			int styleID;
			if (id == IDM_SEARCH_UNMARKALLEXT1)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT1;
			else if (id == IDM_SEARCH_UNMARKALLEXT2)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT2;
			else if (id == IDM_SEARCH_UNMARKALLEXT3)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT3;
			else if (id == IDM_SEARCH_UNMARKALLEXT4)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT4;
			else // (id == IDM_SEARCH_UNMARKALLEXT5)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT5;

			_pEditView->clearIndicator(styleID);
		}
		break;

		case IDM_SEARCH_GONEXTMARKER1 :
		case IDM_SEARCH_GONEXTMARKER2 :
		case IDM_SEARCH_GONEXTMARKER3 :
		case IDM_SEARCH_GONEXTMARKER4 :
		case IDM_SEARCH_GONEXTMARKER5 :
		case IDM_SEARCH_GONEXTMARKER_DEF :
		{
			int styleID;
			if (id == IDM_SEARCH_GONEXTMARKER1)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT1;
			else if (id == IDM_SEARCH_GONEXTMARKER2)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT2;
			else if (id == IDM_SEARCH_GONEXTMARKER3)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT3;
			else if (id == IDM_SEARCH_GONEXTMARKER4)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT4;
			else if (id == IDM_SEARCH_GONEXTMARKER5)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT5;
			else // (id == IDM_SEARCH_GONEXTMARKER_DEF)
				styleID = SCE_UNIVERSAL_FOUND_STYLE;

			goToNextIndicator(styleID);
		}
		break;

		case IDM_SEARCH_GOPREVMARKER1 :
		case IDM_SEARCH_GOPREVMARKER2 :
		case IDM_SEARCH_GOPREVMARKER3 :
		case IDM_SEARCH_GOPREVMARKER4 :
		case IDM_SEARCH_GOPREVMARKER5 :
		case IDM_SEARCH_GOPREVMARKER_DEF :
		{
			int styleID;
			if (id == IDM_SEARCH_GOPREVMARKER1)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT1;
			else if (id == IDM_SEARCH_GOPREVMARKER2)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT2;
			else if (id == IDM_SEARCH_GOPREVMARKER3)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT3;
			else if (id == IDM_SEARCH_GOPREVMARKER4)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT4;
			else if (id == IDM_SEARCH_GOPREVMARKER5)
				styleID = SCE_UNIVERSAL_FOUND_STYLE_EXT5;
			else // (id == IDM_SEARCH_GOPREVMARKER_DEF)
				styleID = SCE_UNIVERSAL_FOUND_STYLE;

			goToPreviousIndicator(styleID);
		}
		break;

		case IDM_SEARCH_CLEARALLMARKS :
		{
			_pEditView->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE_EXT1);
			_pEditView->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE_EXT2);
			_pEditView->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE_EXT3);
			_pEditView->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE_EXT4);
			_pEditView->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE_EXT5);
		}
		break;

		case IDM_SEARCH_STYLE1TOCLIP:
		{
			_pEditView->markedTextToClipboard(SCE_UNIVERSAL_FOUND_STYLE_EXT1);
		}
		break;
		case IDM_SEARCH_STYLE2TOCLIP:
		{
			_pEditView->markedTextToClipboard(SCE_UNIVERSAL_FOUND_STYLE_EXT2);
		}
		break;
		case IDM_SEARCH_STYLE3TOCLIP:
		{
			_pEditView->markedTextToClipboard(SCE_UNIVERSAL_FOUND_STYLE_EXT3);
		}
		break;
		case IDM_SEARCH_STYLE4TOCLIP:
		{
			_pEditView->markedTextToClipboard(SCE_UNIVERSAL_FOUND_STYLE_EXT4);
		}
		break;
		case IDM_SEARCH_STYLE5TOCLIP:
		{
			_pEditView->markedTextToClipboard(SCE_UNIVERSAL_FOUND_STYLE_EXT5);
		}
		break;
		case IDM_SEARCH_ALLSTYLESTOCLIP:
		{
			_pEditView->markedTextToClipboard(-1, true);
		}
		break;
		case IDM_SEARCH_MARKEDTOCLIP:
		{
			_pEditView->markedTextToClipboard(SCE_UNIVERSAL_FOUND_STYLE);
		}
		break;

		case IDM_SEARCH_TOGGLE_BOOKMARK:
			bookmarkToggle(-1);
			break;

		case IDM_SEARCH_NEXT_BOOKMARK:
			bookmarkNext(true);
			break;

		case IDM_SEARCH_PREV_BOOKMARK:
			bookmarkNext(false);
			break;

		case IDM_SEARCH_CLEAR_BOOKMARKS:
			bookmarkClearAll();
			break;

		case IDM_SEARCH_GOTOMATCHINGBRACE :
		case IDM_SEARCH_SELECTMATCHINGBRACES :
		{
			intptr_t braceAtCaret = -1;
			intptr_t braceOpposite = -1;
			findMatchingBracePos(braceAtCaret, braceOpposite);

			if (braceOpposite != -1)
			{
				if (id == IDM_SEARCH_GOTOMATCHINGBRACE)
					_pEditView->execute(SCI_GOTOPOS, braceOpposite);
				else
					_pEditView->execute(SCI_SETSEL, std::min<intptr_t>(braceAtCaret, braceOpposite), std::max<intptr_t>(braceAtCaret, braceOpposite) + 1); // + 1 so we always include the ending brace in the selection.

				// Update Scintilla's knowledge about what column the caret is in, so that if user
				// does up/down arrow as first navigation after the brace-match operation,
				// the caret doesn't jump to an unexpected column
				_pEditView->execute(SCI_CHOOSECARETX);
			}
			break;
		}

		case IDM_VIEW_FOLDALL:
		case IDM_VIEW_UNFOLDALL:
		{
			_isFolding = true; // So we can ignore events while folding is taking place
			bool doCollapse = (id == IDM_VIEW_FOLDALL) ? fold_collapse : fold_expand;
			_pEditView->foldAll(doCollapse);
			if (_pDocMap)
			{
				_pDocMap->foldAll(doCollapse);
			}
			_isFolding = false;
		}
		break;

		case IDM_VIEW_FOLD_CURRENT:
		case IDM_VIEW_UNFOLD_CURRENT:
		{
			bool isToggleEnabled = NppParameters::getInstance().getNppGUI()._enableFoldCmdToggable;
			bool mode = id == IDM_VIEW_FOLD_CURRENT ? fold_collapse : fold_expand;

			if (isToggleEnabled)
			{
				bool isFolded = _pEditView->isCurrentLineFolded();
				mode = isFolded ? fold_expand : fold_collapse;
			}

			_pEditView->foldCurrentPos(mode);
		}
		break;

		case IDM_VIEW_WRAP:
		{
			bool isWrapped = !_pEditView->isWrap();
			// ViewMoveAtWrappingDisableFix: Disable wrapping messes up visible lines. Therefore save view position before in IDM_VIEW_WRAP and restore after SCN_PAINTED, as Scintilla-Doc. says
			if (!isWrapped)
			{
				_mainEditView->saveCurrentPos();
				_mainEditView->setWrapRestoreNeeded(true);
				_subEditView->saveCurrentPos();
				_subEditView->setWrapRestoreNeeded(true);
			}
			_mainEditView->wrap(isWrapped);
			_subEditView->wrap(isWrapped);
			checkMenuItem(IDM_VIEW_WRAP, isWrapped);

			ScintillaViewParams& svp1 = (ScintillaViewParams&)(NppParameters::getInstance()).getSVP();
			svp1._doWrap = isWrapped;

			if (_pDocMap)
			{
				_pDocMap->initWrapMap();
				_pDocMap->wrapMap();
			}
			break;
		}

		case IDM_VIEW_WRAP_SYMBOL:
		{
			_mainEditView->showWrapSymbol(!_pEditView->isWrapSymbolVisible());
			_subEditView->showWrapSymbol(!_pEditView->isWrapSymbolVisible());
			checkMenuItem(IDM_VIEW_WRAP_SYMBOL, _pEditView->isWrapSymbolVisible());

			ScintillaViewParams& svp1 = (ScintillaViewParams&)(NppParameters::getInstance()).getSVP();
			svp1._wrapSymbolShow = _pEditView->isWrapSymbolVisible();
			break;
		}

		case IDM_VIEW_TAB_SPACE:
		{
			auto& svp1 = const_cast<ScintillaViewParams&>(NppParameters::getInstance().getSVP());
			const bool isChecked = !svp1._whiteSpaceShow; // NPLX: upstream reads the menu check state
			checkMenuItem(id, isChecked);

			_mainEditView->showWSAndTab(isChecked);
			_subEditView->showWSAndTab(isChecked);

			svp1._whiteSpaceShow = isChecked;

			const bool allChecked = svp1._whiteSpaceShow && svp1._eolShow && svp1._npcShow && svp1._ccUniEolShow;

			checkMenuItem(IDM_VIEW_ALL_CHARACTERS, allChecked);
			break;
		}

		case IDM_VIEW_EOL:
		{
			auto& svp1 = const_cast<ScintillaViewParams&>(NppParameters::getInstance().getSVP());
			const bool isChecked = !svp1._eolShow; // NPLX: upstream reads the menu check state
			checkMenuItem(id, isChecked);

			_mainEditView->showEOL(isChecked);
			_subEditView->showEOL(isChecked);

			svp1._eolShow = isChecked;

			const bool allChecked = svp1._whiteSpaceShow && svp1._eolShow && svp1._npcShow && svp1._ccUniEolShow;

			checkMenuItem(IDM_VIEW_ALL_CHARACTERS, allChecked);
			break;
		}

		case IDM_VIEW_ALL_CHARACTERS:
		{
			auto& svp1 = const_cast<ScintillaViewParams&>(NppParameters::getInstance().getSVP());
			const bool isChecked = !(svp1._whiteSpaceShow && svp1._eolShow && svp1._npcShow && svp1._ccUniEolShow); // NPLX: upstream reads the menu check state
			checkMenuItem(id, isChecked);
			checkMenuItem(IDM_VIEW_TAB_SPACE, isChecked);
			checkMenuItem(IDM_VIEW_EOL, isChecked);
			checkMenuItem(IDM_VIEW_NPC, isChecked);
			checkMenuItem(IDM_VIEW_NPC_CCUNIEOL, isChecked);

			svp1._whiteSpaceShow = isChecked;
			svp1._eolShow = isChecked;
			svp1._npcShow = isChecked;
			svp1._ccUniEolShow = isChecked;

			_mainEditView->showInvisibleChars(isChecked);
			_subEditView->showInvisibleChars(isChecked);

			// inform the relevant lexers about the update
			for (ScintillaEditView* pView : { _mainEditView, _subEditView })
			{
				Buffer* pBuf = pView->getCurrentBuffer();
				// ErrorList and EscapeSequence both need to set visiblity of ANSI control sequences based on View setting
				if (pBuf->getLangType() == L_ERRORLIST)
				{
					pView->execute(SCI_STYLESETVISIBLE, static_cast<WPARAM>(SCE_ERR_ESCSEQ), static_cast<LPARAM>(isChecked));
					pView->execute(SCI_STYLESETVISIBLE, static_cast<WPARAM>(SCE_ERR_ESCSEQ_UNKNOWN), static_cast<LPARAM>(isChecked));
				}
				else if (pBuf->getLangType() == L_ESCSEQ)
				{
					pView->execute(SCI_STYLESETVISIBLE, static_cast<WPARAM>(SCE_ESCSEQ_IDENTIFIER), static_cast<LPARAM>(isChecked));
					pView->execute(SCI_STYLESETVISIBLE, static_cast<WPARAM>(SCE_ESCSEQ_UNKNOWN), static_cast<LPARAM>(isChecked));
				}
			}

			break;
		}

		case IDM_VIEW_INDENT_GUIDE:
		{
			_mainEditView->showIndentGuideLine(!_pEditView->isShownIndentGuide());
			_subEditView->showIndentGuideLine(!_pEditView->isShownIndentGuide());
			checkMenuItem(IDM_VIEW_INDENT_GUIDE, _pEditView->isShownIndentGuide());

			ScintillaViewParams& svp1 = (ScintillaViewParams&)(NppParameters::getInstance()).getSVP();
			svp1._indentGuideLineShow = _pEditView->isShownIndentGuide();
			break;
		}

		case IDM_VIEW_ZOOMIN:
		{
			_pEditView->execute(SCI_ZOOMIN);
			syncZoom();
			break;
		}

		case IDM_VIEW_ZOOMOUT:
			_pEditView->execute(SCI_ZOOMOUT);
			syncZoom();
			break;

		case IDM_VIEW_ZOOMRESTORE:
			//Zoom factor of 0 points means default view
			_pEditView->execute(SCI_SETZOOM, 0);//_zoomOriginalValue);
			syncZoom();
			break;

		case IDM_VIEW_GOTO_ANOTHER_VIEW:
			docGotoAnotherEditView(TransferMove);
			checkSyncState();
			break;

		case IDM_VIEW_CLONE_TO_ANOTHER_VIEW:
			docGotoAnotherEditView(TransferClone);
			checkSyncState();
			break;

		case IDM_VIEW_SWITCHTO_OTHER_VIEW:
		{
			int view_to_focus = otherView();
			if (!viewVisible(view_to_focus)) view_to_focus = _activeView;
			switchEditViewTo(view_to_focus);
			break;
		}

		case IDM_FORMAT_TODOS:
		case IDM_FORMAT_TOUNIX:
		case IDM_FORMAT_TOMAC:
		{
			EolType newFormat = (id == IDM_FORMAT_TODOS)
				? EolType::windows
				: (id == IDM_FORMAT_TOUNIX) ? EolType::unix : EolType::macos;

			Buffer* buf = _pEditView->getCurrentBuffer();

			if (!buf->isReadOnly())
			{
				std::lock_guard<std::mutex> lock(command_mutex);
				buf->setEolFormat(newFormat);
				_pEditView->execute(SCI_CONVERTEOLS, static_cast<int>(buf->getEolFormat()));
			}
			setDisplayFormat(buf->getEolFormat()); // NPLX: refresh the radio check marks

			break;
		}

		case IDM_FORMAT_ANSI:
		case IDM_FORMAT_UTF_8:
		case IDM_FORMAT_UTF_16BE:
		case IDM_FORMAT_UTF_16LE:
		case IDM_FORMAT_AS_UTF_8:
		{
			bool isUTF8System = NppParameters::getInstance().isCurrentSystemCodepageUTF8();

			if (isUTF8System && id == IDM_FORMAT_ANSI)
			{
				return;
			}

			Buffer* buf = _pEditView->getCurrentBuffer();

			UniMode originalUm = buf->getUnicodeMode();
			int originalEncoding = buf->getEncoding();

			UniMode um;
			bool shouldBeDirty = true;
			switch (id)
			{
				case IDM_FORMAT_AS_UTF_8:
					shouldBeDirty = originalUm != uni8Bit;
					um = uniUTF8_NoBOM;
					break;

				case IDM_FORMAT_UTF_8:
					um = uniUTF8;
					break;

				case IDM_FORMAT_UTF_16BE:
					um = uni16BE;
					break;

				case IDM_FORMAT_UTF_16LE:
					um = uni16LE;
					break;

				default: // IDM_FORMAT_ANSI
					shouldBeDirty = originalUm != uniUTF8_NoBOM;
					um = uni8Bit;
			}

			if (originalEncoding != -1)
			{
				if (buf->isDirty())
				{
					int answer = _nativeLangSpeaker.messageBox("SaveCurrentModifWarning",
						L"You should save the current modification.\rAll the saved modifications cannot be undone.\r\rContinue?",
						L"Save Current Modification",
						MB_YESNO);

					if (answer == IDYES)
					{
						fileSave();
						_pEditView->execute(SCI_EMPTYUNDOBUFFER);
					}
					else
					{
						return;
					}
				}

				if (_pEditView->execute(SCI_CANUNDO) == TRUE)
				{
					int answer = _nativeLangSpeaker.messageBox("LoseUndoAbilityWarning",
						L"You should save the current modification.\rAll the saved modifications cannot be undone.\r\rContinue?",
						L"Lose Undo Ability Warning",
						MB_YESNO);
					if (answer == IDYES)
					{
						// Do nothing
					}
					else
					{
						return;
					}
				}

				buf->setEncoding(-1);

				if (um == uni8Bit)
				{
					NppParameters& nppParams = NppParameters::getInstance();
					_pEditView->execute(SCI_SETCODEPAGE, !nppParams.isCurrentSystemCodepageUTF8() ? CP_ACP : SC_CP_UTF8);
				}
				else
					buf->setUnicodeMode(um);

				MainFileManager.disableAutoDetectEncoding4Loading();
				fileReload();
				MainFileManager.enableAutoDetectEncoding4Loading();
			}
			else
			{
				if (originalUm != um)
				{
					buf->setUnicodeMode(um);
					if (shouldBeDirty)
						buf->setDirty(true);
				}
			}

			break;
		}

		case IDM_OPEN_ALL_RECENT_FILE:
		{
			BufferID lastOne = BUFFER_INVALID;
			int size = _lastRecentFileList.getSize();
			for (int i = size - 1; i >= 0; i--)
			{
				BufferID test = doOpen(_lastRecentFileList.getIndex(i));
				if (test != BUFFER_INVALID)
					lastOne = test;
			}
			if (lastOne != BUFFER_INVALID)
			{
				switchToFile(lastOne);
			}
			break;
		}

		case IDM_CLEAN_RECENT_FILE_LIST:
			_lastRecentFileList.clear();
			break;

		case IDM_FILE_RESTORELASTCLOSEDFILE:
		{
			wstring lastOpenedFullPath = _lastRecentFileList.getFirstItem();
			if (!lastOpenedFullPath.empty())
			{
				BufferID lastOpened = doOpen(lastOpenedFullPath);
				if (lastOpened != BUFFER_INVALID)
					switchToFile(lastOpened);
			}
		}
		break;

		case IDM_ABOUT:
		{
			// NPLX: upstream also shows a quote when the selection names an author (easter eggs); the About box only
			if (!_aboutDlg)
				_aboutDlg = new AboutDlg([this]() { command(IDM_UPDATE_NPP); }, [this]() { command(IDM_DEBUGINFO); }, this);
			_aboutDlg->doDialog();
			break;
		}

		case IDM_DEBUGINFO:
		{
			if (!_debugInfoDlg)
			{
				_debugInfoDlg = new DebugInfoDlg(this);
				_debugInfoDlg->init(getLoadedPluginNames());
			}
			_debugInfoDlg->doDialog(); // translated when created
			break;
		}

		case IDM_CMDLINEARGUMENTS:
		{
			if (!_cmdLineArgsDlg)
				_cmdLineArgsDlg = new CmdLineArgsDlg(this);
			_cmdLineArgsDlg->doDialog();
			break;
		}

		case IDM_UPDATE_NPP:
		{
			checkForUpdates(); // NPLX: upstream launches the WinGUp updater
			break;
		}

		// ---- ported from upstream NppCommands.cpp (session 4)
		case IDM_WINDOW_WINDOWS :
		{
			WindowsDlg _windowsDlg(this);
			_windowsDlg.init(_pDocTab, [this](NMWINDLG& nmdlg) { windowsDlgNotify(nmdlg); });
			_windowsDlg.doDialog();
		}
		break;

		case IDM_WINDOW_SORT_FN_ASC :
		case IDM_WINDOW_SORT_FN_DSC :
		case IDM_WINDOW_SORT_FP_ASC :
		case IDM_WINDOW_SORT_FP_DSC :
		case IDM_WINDOW_SORT_FT_ASC :
		case IDM_WINDOW_SORT_FT_DSC :
		case IDM_WINDOW_SORT_FS_ASC :
		case IDM_WINDOW_SORT_FS_DSC :
		case IDM_WINDOW_SORT_FD_ASC :
		case IDM_WINDOW_SORT_FD_DSC :
		{
			WindowsDlg windowsDlg(this);
			windowsDlg.init(_pDocTab, [this](NMWINDLG& nmdlg) { windowsDlgNotify(nmdlg); });
			switch (id)
			{
				case IDM_WINDOW_SORT_FN_ASC: windowsDlg.sortFileNameASC(); break;
				case IDM_WINDOW_SORT_FN_DSC: windowsDlg.sortFileNameDSC(); break;
				case IDM_WINDOW_SORT_FP_ASC: windowsDlg.sortFilePathASC(); break;
				case IDM_WINDOW_SORT_FP_DSC: windowsDlg.sortFilePathDSC(); break;
				case IDM_WINDOW_SORT_FT_ASC: windowsDlg.sortFileTypeASC(); break;
				case IDM_WINDOW_SORT_FT_DSC: windowsDlg.sortFileTypeDSC(); break;
				case IDM_WINDOW_SORT_FS_ASC: windowsDlg.sortFileSizeASC(); break;
				case IDM_WINDOW_SORT_FS_DSC: windowsDlg.sortFileSizeDSC(); break;
				case IDM_WINDOW_SORT_FD_ASC: windowsDlg.sortDateTimeASC(); break;
				default:                     windowsDlg.sortDateTimeDSC(); break;
			}
			windowsDlg.doSort();
		}
		break;

		case IDC_PREV_DOC :
		case IDC_NEXT_DOC :
		{
			size_t nbDoc = viewVisible(MAIN_VIEW) ? _mainDocTab->nbItem() : 0;
			nbDoc += viewVisible(SUB_VIEW)?_subDocTab->nbItem():0;

			bool doTaskList = ((NppParameters::getInstance()).getNppGUI())._doTaskList;
			_isFolding = true;
			if (nbDoc > 1)
			{
				bool direction = (id == IDC_NEXT_DOC)?dirDown:dirUp;
				if (!doTaskList)
				{
					activateNextDoc(direction);
				}
				else
				{
					if (TaskListDlg::_instanceCount == 0)
					{
						TaskListDlg tld(this, direction,
							[this](TaskListInfo& tli) { taskListInfo(&tli, false); }, // upstream WM_GETTASKLISTINFO
							[this](int view, int index) {
								// upstream NPPM_ACTIVATEDOC
								switchEditViewTo(view);
								activateDoc(index);
							});
						tld.doDialog();
					}
				}
			}
			_isFolding = false;
			_linkTriggered = true;
		}
		break;

		case IDM_VIEW_PROJECT_PANEL_1:
		case IDM_VIEW_PROJECT_PANEL_2:
		case IDM_VIEW_PROJECT_PANEL_3:
		{
			ProjectPanel** pp [] = {&_pProjectPanel_1, &_pProjectPanel_2, &_pProjectPanel_3};
			int idx = id - IDM_VIEW_PROJECT_PANEL_1;
			if (*pp [idx] == nullptr)
			{
				launchProjectPanel(id, pp [idx], idx);
			}
			else
			{
				if (!(*pp[idx])->isClosed())
				{
					if ((*pp[idx])->checkIfNeedSave())
					{
						if ((*pp[idx])->isAncestorOf(QApplication::focusWidget()))
							_pEditView->setFocus();
						(*pp[idx])->display(false);
						(*pp[idx])->setClosed(true);
						checkMenuItem(id, false);
						checkProjectMenuItem();
					}
				}
				else
				{
					launchProjectPanel(id, pp [idx], idx);
				}
			}
		}
		break;

		case IDM_VIEW_SWITCHTO_PROJECT_PANEL_1:
		case IDM_VIEW_SWITCHTO_PROJECT_PANEL_2:
		case IDM_VIEW_SWITCHTO_PROJECT_PANEL_3:
		{
			ProjectPanel** pp [] = {&_pProjectPanel_1, &_pProjectPanel_2, &_pProjectPanel_3};
			int idx = id - IDM_VIEW_SWITCHTO_PROJECT_PANEL_1;
			launchProjectPanel(id - IDM_VIEW_SWITCHTO_PROJECT_PANEL_1 + IDM_VIEW_PROJECT_PANEL_1, pp [idx], idx);
		}
		break;

		case IDM_VIEW_FILEBROWSER:
		case IDM_VIEW_SWITCHTO_FILEBROWSER:
		{
			if (_pFileBrowser == nullptr) // first launch, check in params to open folders
			{
				std::vector<std::wstring> dummy; // use nppParam.getFileBrowserRoots() instead
				NppParameters& nppParam = NppParameters::getInstance();

				launchFileBrowser(dummy, nppParam.getFileBrowserSelectedItemPath(), true, &(nppParam.getFileBrowserRoots()));
				if (_pFileBrowser != nullptr)
				{
					checkMenuItem(IDM_VIEW_FILEBROWSER, true);
					_pFileBrowser->setClosed(false);
				}
			}
			else
			{
				if (!_pFileBrowser->isClosed() && (id != IDM_VIEW_SWITCHTO_FILEBROWSER))
				{
					_pFileBrowser->display(false);
					_pFileBrowser->setClosed(true);
					checkMenuItem(IDM_VIEW_FILEBROWSER, false);
				}
				else
				{
					std::vector<std::wstring> dummy;
					wstring emptyStr;
					launchFileBrowser(dummy, emptyStr);
					checkMenuItem(IDM_VIEW_FILEBROWSER, true);
					_pFileBrowser->setClosed(false);
				}
			}
		}
		break;

		case IDM_FILE_CONTAININGFOLDERASWORKSPACE:
		{
			// upstream: NPPM_GETFULLCURRENTPATH / NPPM_GETCURRENTDIRECTORY
			const wstring currentFile = _pEditView->getCurrentBuffer()->getFullPathName();
			const wstring currentDir = QFileInfo(QString::fromStdWString(currentFile)).absolutePath().toStdWString();

			if (!_pFileBrowser)
			{
				command(IDM_VIEW_FILEBROWSER);
			}

			std::vector<wstring> folders;
			folders.push_back(currentDir);

			launchFileBrowser(folders, currentFile);
		}
		break;

		case IDM_FILE_OPENFOLDERASWORKSPACE:
		{
			const NativeLangSpeaker* pNativeSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
			wstring openWorkspaceStr = pNativeSpeaker->getAttrNameStr(L"Select a folder to add in Folder as Workspace panel",
				FOLDERASWORKSPACE_NODE, "SelectFolderFromBrowserString");
			// NPLX: QFileDialog in place of folderBrowser (tests: nplx::uiServices()->getExistingDirectory)
			wstring folderPath = nplx::uiServices() ? nplx::uiServices()->getExistingDirectory(openWorkspaceStr) : wstring();
			if (!folderPath.empty())
			{
				if (_pFileBrowser == nullptr) // first launch, check in params to open folders
				{
					std::vector<wstring> dummy;
					wstring emptyStr;
					launchFileBrowser(dummy, emptyStr);
					if (_pFileBrowser != nullptr)
					{
						checkMenuItem(IDM_VIEW_FILEBROWSER, true);
						_pFileBrowser->setClosed(false);
					}
					else // problem
						return;
				}
				else
				{
					if (_pFileBrowser->isClosed())
					{
						_pFileBrowser->display();
						checkMenuItem(IDM_VIEW_FILEBROWSER, true);
						_pFileBrowser->setClosed(false);
					}
				}
				_pFileBrowser->addRootFolder(folderPath);
			}
		}
		break;

		case IDM_FILE_OPEN_CMD:
		{
			// NPLX: a terminal in the current file's folder (was Command(L"%COMSPEC%").run(..., L"$(CURRENT_DIRECTORY)"))
			const wstring currentFile = _pEditView->getCurrentBuffer()->getFullPathName();
			QString dir = QFileInfo(QString::fromStdWString(currentFile)).absolutePath();
			if (_pEditView->getCurrentBuffer()->isUntitled())
				dir = QDir::homePath();
			nplx::desktop::openTerminal(dir);
		}
		break;

		case IDM_VIEW_DOC_MAP:
		{
			if (_pDocMap && (!_pDocMap->isClosed()))
			{
				_pDocMap->display(false);
				_pDocMap->vzDlgDisplay(false);
				_pDocMap->setClosed(true);
				checkMenuItem(IDM_VIEW_DOC_MAP, false);
			}
			else
			{
				launchDocMap();
				if (_pDocMap)
				{
					checkMenuItem(IDM_VIEW_DOC_MAP, true);
					_pDocMap->setClosed(false);
				}
			}
		}
		break;

		case IDM_EDIT_CHAR_PANEL:
		{
			if (_pAnsiCharPanel && (!_pAnsiCharPanel->isClosed()))
			{
				_pAnsiCharPanel->display(false);
				_pAnsiCharPanel->setClosed(true);
				checkMenuItem(IDM_EDIT_CHAR_PANEL, false);
			}
			else
			{
				checkMenuItem(IDM_EDIT_CHAR_PANEL, true);
				launchAnsiCharPanel();
				_pAnsiCharPanel->setClosed(false);
			}
		}
		break;

		case IDM_EDIT_CLIPBOARDHISTORY_PANEL:
		{
			if (_pClipboardHistoryPanel && (!_pClipboardHistoryPanel->isClosed()))
			{
				_pClipboardHistoryPanel->display(false);
				_pClipboardHistoryPanel->setClosed(true);
				checkMenuItem(IDM_EDIT_CLIPBOARDHISTORY_PANEL, false);
			}
			else
			{
				checkMenuItem(IDM_EDIT_CLIPBOARDHISTORY_PANEL, true);
				launchClipboardHistoryPanel();
				_pClipboardHistoryPanel->setClosed(false);
			}
		}
		break;

		case IDM_VIEW_SWITCHTO_DOCLIST:
		{
			if (_pDocumentListPanel && _pDocumentListPanel->isVisible())
			{
				_pDocumentListPanel->grabFocus();
			}
			else
			{
				checkMenuItem(IDM_VIEW_DOCLIST, true);
				launchDocumentListPanel();
				_pDocumentListPanel->setClosed(false);
			}
		}
		break;

		case IDM_VIEW_DOCLIST:
		{
			if (_pDocumentListPanel && (!_pDocumentListPanel->isClosed()))
			{
				_pDocumentListPanel->display(false);
				_pDocumentListPanel->setClosed(true);
				checkMenuItem(IDM_VIEW_DOCLIST, false);
			}
			else
			{
				launchDocumentListPanel();
				if (_pDocumentListPanel)
				{
					checkMenuItem(IDM_VIEW_DOCLIST, true);
					_pDocumentListPanel->setClosed(false);
				}
			}
		}
		break;

		case IDM_DOCLIST_FILESCLOSE:
		case IDM_DOCLIST_FILESCLOSEOTHERS:
			if (_pDocumentListPanel)
			{
				std::vector<BufferViewInfo> bufs2Close = _pDocumentListPanel->getSelectedFiles(id == IDM_DOCLIST_FILESCLOSEOTHERS);

				fileCloseAllGiven(bufs2Close);

				if (id == IDM_DOCLIST_FILESCLOSEOTHERS)
				{
					// Get current buffer and its view
					_pDocumentListPanel->activateItem(_pEditView->getCurrentBufferID(), currentView());
				}
			}
			break;

		case IDM_DOCLIST_COPYNAMES:
		case IDM_DOCLIST_COPYPATHS:
			if (_pDocumentListPanel)
			{
				std::vector<Buffer*> buffers;
				auto files = _pDocumentListPanel->getSelectedFiles(false);
				for (auto&& sel : files)
					buffers.push_back(MainFileManager.getBufferByID(sel._bufID));
				buf2Clipboard(buffers, id == IDM_DOCLIST_COPYPATHS);
			}
			break;

		case IDM_VIEW_SWITCHTO_FUNC_LIST:
		{
			if (_pFuncList && _pFuncList->isVisible())
			{
				_pFuncList->grabFocus();
			}
			else
			{
				checkMenuItem(IDM_VIEW_FUNC_LIST, true);
				launchFunctionList();
				_pFuncList->setClosed(false);
			}
		}
		break;

		case IDM_VIEW_FUNC_LIST:
		{
			if (_pFuncList && (!_pFuncList->isClosed()))
			{
				_pFuncList->display(false);
				_pFuncList->setClosed(true);
				checkMenuItem(IDM_VIEW_FUNC_LIST, false);
			}
			else
			{
				checkMenuItem(IDM_VIEW_FUNC_LIST, true);
				launchFunctionList();
				_pFuncList->setClosed(false);
			}
		}
		break;

		case IDM_LANG_USER_DLG :
		{
			bool isUDDlgVisible = false;

			UserDefineDialog *udd = _userDefineDlg;

			if (!udd->isCreated())
			{
				udd->doDialog(true, _nativeLangSpeaker.isRTL()); // also changeUserDefineLang
				if (_isUDDocked)
					udd->command(IDC_DOCK_BUTTON);
			}
			else
			{
				isUDDlgVisible = udd->isVisible();
				bool isUDDlgDocked = udd->isDocked();

				if ((isUDDlgDocked)&&(isUDDlgVisible))
				{
					// NPLX: the dock (upstream: the main splitter) is hidden
					_udlDock->hide();
				}
				else if ((isUDDlgDocked)&&(!isUDDlgVisible))
				{
					udd->show();
					_udlDock->show();
					_udlDock->raise();
				}
				else if ((!isUDDlgDocked)&&(isUDDlgVisible))
				{
					udd->hide();
				}
				else //((!isUDDlgDocked)&&(!isUDDlgVisible))
					udd->doDialog();
			}
			checkMenuItem(IDM_LANG_USER_DLG, !isUDDlgVisible);
		}
		break;


		case IDM_FILE_LOADSESSION:
			fileLoadSession();
			break;

		case IDM_FILE_SAVESESSION:
			fileSaveSession();
			break;

		case IDM_SETTING_EDITCONTEXTMENU :
		{
			// NPLX: "NotePadLX" in place of "Notepad++" in the default text
			_nativeLangSpeaker.messageBox("ContextMenuXmlEditWarning",
				L"Editing contextMenu.xml allows you to modify your NotePadLX popup context menu on edit zone.\rYou have to restart your NotePadLX to take effect after modifying contextMenu.xml.",
				L"Editing contextMenu",
				MB_OK|MB_APPLMODAL);

			const NppParameters& nppParams = NppParameters::getInstance();
			BufferID bufID = doOpen((nppParams.getContextMenuPath()));
			switchToFile(bufID);
			break;
		}

		case IDM_SETTING_IMPORTSTYLETHEMES :
		{
			// get plugin source path
			const wchar_t *extFilterName = L"Notepad++ style theme";
			const wchar_t *extFilter = L".xml";
			const wchar_t *destDir = L"themes";

			// load styler
			NppParameters& nppParams = NppParameters::getInstance();
			ThemeSwitcher & themeSwitcher = nppParams.getThemeSwitcher();

			std::vector<std::wstring> copiedFiles = addNppComponents(destDir, extFilterName, extFilter);
			for (size_t i = 0, len = copiedFiles.size(); i < len ; ++i)
			{
				std::wstring themeName(themeSwitcher.getThemeFromXmlFileName(copiedFiles[i].c_str()));
				if (!themeSwitcher.themeNameExists(themeName.c_str()))
				{
					themeSwitcher.addThemeFromXml(copiedFiles[i].c_str());
					if (_configStyleDlg->isCreated())
					{
						_configStyleDlg->addLastThemeEntry();
					}
				}
			}
			break;
		}

		case IDM_SETTING_IMPORTPLUGIN :
		{
			// Copy plugins to Plugins Home
			const wchar_t *extFilterName = L"NotePadLX plugin";
			const wchar_t *extFilter = L".so";
			std::vector<std::wstring> copiedFiles = addNppPlugins(extFilterName, extFilter);

			// Tell users to restart NotePadLX to load plugin
			if (copiedFiles.size())
			{
				_nativeLangSpeaker.messageBox("NeedToRestartToLoadPlugins",
					L"You have to restart NotePadLX to load plugins you installed.",
					L"NotePadLX needs to be relaunched",
					MB_OK | MB_APPLMODAL);
			}
			break;
		}

		case IDM_SETTING_OPENPLUGINSDIR:
		{
			openPluginsFolder();
			break;
		}

		case IDM_LANGSTYLE_CONFIG_DLG :
		{
			if (!(NppParameters::getInstance()).isStylerDocLoaded())
			{
				// do nothing
			}
			else
			{
				_configStyleDlg->doDialog(_nativeLangSpeaker.isRTL()); // also changeConfigLang on first use
			}
			break;
		}

		case IDM_EDIT_AUTOCOMPLETE :
			showAutoComp();
			break;

		case IDM_EDIT_AUTOCOMPLETE_CURRENTFILE :
			autoCompFromCurrentFile();
			break;

		case IDM_EDIT_AUTOCOMPLETE_PATH :
			showPathCompletion();
			break;

		case IDM_EDIT_FUNCCALLTIP :
			showFunctionComp();
			break;

		case IDM_EDIT_FUNCCALLTIP_PREVIOUS :
			showFunctionNextHint(false);
			break;

		case IDM_EDIT_FUNCCALLTIP_NEXT :
			showFunctionNextHint();
			break;



		case IDM_VIEW_SYNSCROLLV:
		{
            bool isSynScrollV = !_syncInfo._isSynScrollV;

			checkMenuItem(IDM_VIEW_SYNSCROLLV, isSynScrollV);

            _syncInfo._isSynScrollV = isSynScrollV;
			if (_syncInfo._isSynScrollV)
			{
				intptr_t mainCurrentLine = _mainEditView->execute(SCI_GETFIRSTVISIBLELINE);
				intptr_t subCurrentLine = _subEditView->execute(SCI_GETFIRSTVISIBLELINE);
				_syncInfo._line = mainCurrentLine - subCurrentLine;
			}

		}
		break;

		case IDM_VIEW_SYNSCROLLH:
		{
            bool isSynScrollH = !_syncInfo._isSynScrollH;
			checkMenuItem(IDM_VIEW_SYNSCROLLH, isSynScrollH);

            _syncInfo._isSynScrollH = isSynScrollH;
			if (_syncInfo._isSynScrollH)
			{
				intptr_t mxoffset = _mainEditView->execute(SCI_GETXOFFSET);
				intptr_t pixel = _mainEditView->execute(SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>("P"));
				intptr_t mainColumn = mxoffset/pixel;

				intptr_t sxoffset = _subEditView->execute(SCI_GETXOFFSET);
				pixel = _subEditView->execute(SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>("P"));
				intptr_t subColumn = sxoffset/pixel;
				_syncInfo._column = mainColumn - subColumn;
			}
		}
		break;



		case IDM_VIEW_MONITORING:
		{
			Buffer * curBuf = _pEditView->getCurrentBuffer();
			if (curBuf->isMonitoringOn())
			{
				monitoringStartOrStopAndUpdateUI(curBuf, false);
			}
			else
			{
				const wchar_t *longFileName = curBuf->getFullPathName();
				if (doesFileExist(longFileName))
				{
					if (curBuf->isDirty())
					{
						_nativeLangSpeaker.messageBox("DocTooDirtyToMonitor",
							L"The document is dirty. Please save the modification before monitoring it.",
							L"Monitoring problem",
							MB_OK);
					}
					else
					{
						// Monitoring firstly for making monitoring icon
						monitoringStartOrStopAndUpdateUI(curBuf, true);
						createMonitoringThread(curBuf);
					}
				}
				else
				{
					_nativeLangSpeaker.messageBox("DocNoExistToMonitor",
						L"The file should exist to be monitored.",
						L"Monitoring problem",
						MB_OK);
				}
			}

			break;
		}

		case IDM_VIEW_SUMMARY:
		{
			Buffer * curBuf = _pEditView->getCurrentBuffer();
			int64_t fileLen = curBuf->getFileLength();

			// localization for summary date
			NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
			if (pNativeSpeaker)
			{
				wstring characterNumber = L"";

				if (fileLen != -1)
				{
					wstring filePathLabel = pNativeSpeaker->getLocalizedStrFromID("summary-filepath", L"Full file path: ");
					wstring fileCreateTimeLabel = pNativeSpeaker->getLocalizedStrFromID("summary-filecreatetime", L"Created: ");
					wstring fileModifyTimeLabel = pNativeSpeaker->getLocalizedStrFromID("summary-filemodifytime", L"Modified: ");

					characterNumber += filePathLabel;
					characterNumber += curBuf->getFullPathName();
					characterNumber += L"\r";

					characterNumber += fileCreateTimeLabel;
					characterNumber += curBuf->getFileTime(Buffer::ft_created);
					characterNumber += L"\r";

					characterNumber += fileModifyTimeLabel;
					characterNumber += curBuf->getFileTime(Buffer::ft_modified);
					characterNumber += L"\r";
				}
				wstring nbCharLabel = pNativeSpeaker->getLocalizedStrFromID("summary-nbchar", L"Characters (without line endings): ");
				wstring nbWordLabel = pNativeSpeaker->getLocalizedStrFromID("summary-nbword", L"Words: ");
				wstring nbLineLabel = pNativeSpeaker->getLocalizedStrFromID("summary-nbline", L"Lines: ");
				wstring nbByteLabel = pNativeSpeaker->getLocalizedStrFromID("summary-nbbyte", L"Document length: ");
				wstring nbSelLabel1 = pNativeSpeaker->getLocalizedStrFromID("summary-nbsel1", L" selected characters (");
				wstring nbSelLabel2 = pNativeSpeaker->getLocalizedStrFromID("summary-nbsel2", L" bytes) in ");
				wstring nbRangeLabel = pNativeSpeaker->getLocalizedStrFromID("summary-nbrange", L" ranges");

				UniMode um = _pEditView->getCurrentBuffer()->getUnicodeMode();
				size_t nbChar = getCurrentDocCharCount(um);
				int nbWord = wordCount();
				size_t nbLine = _pEditView->execute(SCI_GETLINECOUNT);
				size_t nbByte = _pEditView->execute(SCI_GETLENGTH);
				size_t nbSel = getSelectedCharNumber(um);
				size_t nbSelByte = getSelectedBytes();
				size_t nbRange = getSelectedAreas();

				characterNumber += nbCharLabel;
				characterNumber += commafyInt(nbChar).c_str();
				characterNumber += L"\r";

				characterNumber += nbWordLabel;
				characterNumber += commafyInt(nbWord).c_str();
				characterNumber += L"\r";

				characterNumber += nbLineLabel;
				characterNumber += commafyInt(nbLine).c_str();
				characterNumber += L"\r";

				characterNumber += nbByteLabel;
				characterNumber += commafyInt(nbByte).c_str();
				characterNumber += L"\r";

				characterNumber += commafyInt(nbSel).c_str();
				characterNumber += nbSelLabel1;
				characterNumber += commafyInt(nbSelByte).c_str();
				characterNumber += nbSelLabel2;
				characterNumber += commafyInt(nbRange).c_str();
				characterNumber += nbRangeLabel;
				characterNumber += L"\r";

				wstring summaryLabel = pNativeSpeaker->getLocalizedStrFromID("summary", L"Summary");

				nplx::uiServices()->messageBox(characterNumber, summaryLabel, MB_OK | MB_APPLMODAL); // NPLX: was NppDarkMode::darkMessageBoxW
			}
		}
		break;


		case IDM_SEARCH_FINDCHARINRANGE :
		{
			if (!_findCharsInRangeDlg)
			{
				// upstream Notepad_plus::init: _findCharsInRangeDlg.init(_pPublicInterface->getHinst(), hwnd, &_pEditView)
				_findCharsInRangeDlg = new FindCharsInRangeDlg(this);
				_findCharsInRangeDlg->init(&_pEditView);
			}
			_findCharsInRangeDlg->doDialog(); // translated when created
		}
		break;

		case IDM_SEARCH_GOTOLINE:
		{
			_goToLineDlg->doDialog(_nativeLangSpeaker.isRTL()); // translated when created
		}
		break;

		case IDM_FILE_CLOSEALL_BUT_CURRENT :
			fileCloseAllButCurrent();
			checkDocState();
			break;

		case IDM_FILE_CLOSEALL_BUT_PINNED :
			fileCloseAllButPinned();
			checkDocState();
			break;

		case IDM_FILE_CLOSEALL_TOLEFT :
			fileCloseAllToLeft();
			checkDocState();
			break;

		case IDM_FILE_CLOSEALL_TORIGHT :
			fileCloseAllToRight();
			checkDocState();
			break;

		case IDM_FILE_CLOSEALL_UNCHANGED:
			fileCloseAllUnchanged();
			checkDocState();
			break;


		case IDM_EDIT_BEGINENDSELECT:
		case IDM_EDIT_BEGINENDSELECT_COLUMNMODE:
		{
			_pEditView->beginOrEndSelect(id == IDM_EDIT_BEGINENDSELECT_COLUMNMODE);
			bool isStarted = _pEditView->beginEndSelectedIsStarted();
			checkMenuItem(id, isStarted);
			int otherId = (id == IDM_EDIT_BEGINENDSELECT) ? IDM_EDIT_BEGINENDSELECT_COLUMNMODE : IDM_EDIT_BEGINENDSELECT;
			enableCommand(otherId, !isStarted);
		}
		break;

		case IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING:
		case IDM_EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING:
		case IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_ASCENDING:
		case IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING:
		case IDM_EDIT_SORTLINES_INTEGER_ASCENDING:
		case IDM_EDIT_SORTLINES_INTEGER_DESCENDING:
		case IDM_EDIT_SORTLINES_DECIMALCOMMA_ASCENDING:
		case IDM_EDIT_SORTLINES_DECIMALCOMMA_DESCENDING:
		case IDM_EDIT_SORTLINES_DECIMALDOT_ASCENDING:
		case IDM_EDIT_SORTLINES_DECIMALDOT_DESCENDING:
		case IDM_EDIT_SORTLINES_REVERSE_ORDER:
		case IDM_EDIT_SORTLINES_RANDOMLY:
		case IDM_EDIT_SORTLINES_LENGTH_ASCENDING:
		case IDM_EDIT_SORTLINES_LENGTH_DESCENDING:
		{
			std::lock_guard<std::mutex> lock(command_mutex);

			size_t fromLine = 0, toLine = 0;
			size_t fromColumn = 0, toColumn = 0;

			bool hasLineSelection = false;
			if (_pEditView->execute(SCI_GETSELECTIONS) > 1)
			{
				if (_pEditView->execute(SCI_SELECTIONISRECTANGLE) || _pEditView->execute(SCI_GETSELECTIONMODE) == SC_SEL_THIN)
				{
					size_t rectSelAnchor = _pEditView->execute(SCI_GETRECTANGULARSELECTIONANCHOR);
					size_t rectSelCaret = _pEditView->execute(SCI_GETRECTANGULARSELECTIONCARET);
					size_t anchorLine = _pEditView->execute(SCI_LINEFROMPOSITION, rectSelAnchor);
					size_t caretLine = _pEditView->execute(SCI_LINEFROMPOSITION, rectSelCaret);
					fromLine = std::min<size_t>(anchorLine, caretLine);
					toLine = std::max<size_t>(anchorLine, caretLine);
					size_t anchorLineOffset = rectSelAnchor - _pEditView->execute(SCI_POSITIONFROMLINE, anchorLine) + _pEditView->execute(SCI_GETRECTANGULARSELECTIONANCHORVIRTUALSPACE);
					size_t caretLineOffset = rectSelCaret - _pEditView->execute(SCI_POSITIONFROMLINE, caretLine) + _pEditView->execute(SCI_GETRECTANGULARSELECTIONCARETVIRTUALSPACE);
					fromColumn = std::min<size_t>(anchorLineOffset, caretLineOffset);
					toColumn = std::max<size_t>(anchorLineOffset, caretLineOffset);
				}
				else
				{
					return;
				}
			}
			else
			{
				auto selStart = _pEditView->execute(SCI_GETSELECTIONSTART);
				auto selEnd = _pEditView->execute(SCI_GETSELECTIONEND);
				hasLineSelection = selStart != selEnd;
				if (hasLineSelection)
				{
					const pair<size_t, size_t> lineRange = _pEditView->getSelectionLinesRange();
					// One single line selection is not allowed.
					if (lineRange.first == lineRange.second)
					{
						return;
					}
					fromLine = lineRange.first;
					toLine = lineRange.second;
				}
				else
				{
					// No selection.
					fromLine = 0;
					toLine = _pEditView->execute(SCI_GETLINECOUNT) - 1;
				}
			}

			bool isDescending = id == IDM_EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING ||
								id == IDM_EDIT_SORTLINES_INTEGER_DESCENDING ||
								id == IDM_EDIT_SORTLINES_DECIMALCOMMA_DESCENDING ||
								id == IDM_EDIT_SORTLINES_DECIMALDOT_DESCENDING ||
								id == IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING ||
								id == IDM_EDIT_SORTLINES_LENGTH_DESCENDING;

			_pEditView->execute(SCI_BEGINUNDOACTION);
			std::unique_ptr<ISorter> pSorter;
			if (id == IDM_EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING || id == IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING)
			{
				pSorter = std::make_unique<LexicographicSorter>(isDescending, fromColumn, toColumn);
			}
			else if (id == IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING || id == IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_ASCENDING)
			{
				pSorter = std::make_unique<LexicographicCaseInsensitiveSorter>(isDescending, fromColumn, toColumn);
			}
			else if (id == IDM_EDIT_SORTLINES_INTEGER_DESCENDING || id == IDM_EDIT_SORTLINES_INTEGER_ASCENDING)
			{
				pSorter = std::make_unique<IntegerSorter>(isDescending, fromColumn, toColumn);
			}
			else if (id == IDM_EDIT_SORTLINES_DECIMALCOMMA_DESCENDING || id == IDM_EDIT_SORTLINES_DECIMALCOMMA_ASCENDING)
			{
				pSorter = std::make_unique<DecimalCommaSorter>(isDescending, fromColumn, toColumn);
			}
			else if (id == IDM_EDIT_SORTLINES_DECIMALDOT_DESCENDING || id == IDM_EDIT_SORTLINES_DECIMALDOT_ASCENDING)
			{
				pSorter = std::make_unique<DecimalDotSorter>(isDescending, fromColumn, toColumn);
			}
			else if (id == IDM_EDIT_SORTLINES_LENGTH_ASCENDING || id == IDM_EDIT_SORTLINES_LENGTH_DESCENDING)
			{
				pSorter = std::make_unique<LineLengthSorter>(isDescending, fromColumn, toColumn);
			}
			else if (id == IDM_EDIT_SORTLINES_REVERSE_ORDER)
			{
				pSorter = std::make_unique<ReverseSorter>(isDescending, fromColumn, toColumn);
			}
			else
			{
				pSorter = std::make_unique<RandomSorter>(isDescending, fromColumn, toColumn);
			}
			try {
				_pEditView->sortLines(fromLine, toLine, pSorter.get());
			}
			catch (size_t& failedLineIndex)
			{
				size_t lineNo = 1 + fromLine + failedLineIndex;

				_nativeLangSpeaker.messageBox("SortingError",
					L"Unable to perform numeric sorting due to line $INT_REPLACE$.",
					L"Sorting Error",
					MB_OK | MB_ICONINFORMATION | MB_APPLMODAL,
					static_cast<int>(lineNo),
					0);
			}
			_pEditView->execute(SCI_ENDUNDOACTION);

			if (hasLineSelection) // there was 1 selection, so we restore it
			{
				auto posStart = _pEditView->execute(SCI_POSITIONFROMLINE, fromLine);
				auto posEnd = _pEditView->execute(SCI_GETLINEENDPOSITION, toLine);
				_pEditView->execute(SCI_SETSELECTIONSTART, posStart);
				_pEditView->execute(SCI_SETSELECTIONEND, posEnd);
			}
		}
		break;

		case IDM_EDIT_SORTLINES_LOCALE_ASCENDING:
		case IDM_EDIT_SORTLINES_LOCALE_DESCENDING:
		{
			std::lock_guard<std::mutex> lock(command_mutex);
			SortLocale sortLocale;
			auto result = sortLocale.sort(_pEditView, id == IDM_EDIT_SORTLINES_LOCALE_DESCENDING);
			if (result.status)
				_nativeLangSpeaker.messageBox(result.tagName.c_str(),
					result.message.c_str(),
					result.status == MB_ICONERROR ? L"Sort Failed" : L"Sort not performed",
					result.status | MB_OK | MB_APPLMODAL, 0, result.message.c_str());
		}
		break;


		case IDM_VIEW_TAB1:
		case IDM_VIEW_TAB2:
		case IDM_VIEW_TAB3:
		case IDM_VIEW_TAB4:
		case IDM_VIEW_TAB5:
		case IDM_VIEW_TAB6:
		case IDM_VIEW_TAB7:
		case IDM_VIEW_TAB8:
		case IDM_VIEW_TAB9:
		{
			const int index = id - IDM_VIEW_TAB1;
			BufferID buf = _pDocTab->getBufferByIndex(index);
			_isFolding = true;
			if (buf == BUFFER_INVALID)
			{
				// No buffer at chosen index, select the very last buffer instead.
				const int last_index = _pDocTab->getItemCount() - 1;
				if (last_index > 0)
					switchToFile(_pDocTab->getBufferByIndex(last_index));
			}
			else
			{
				switchToFile(buf);
			}
			_isFolding = false;
		}
		break;

		case IDM_VIEW_TAB_START:
		case IDM_VIEW_TAB_END:
		{
			size_t index = id == IDM_VIEW_TAB_START ? 0 : _pDocTab->nbItem() - 1;
			switchToFile(_pDocTab->getBufferByIndex(index));
		}
		break;

		case IDM_VIEW_TAB_NEXT:
		{
			const int current_index = _pDocTab->getCurrentTabIndex();
			const int last_index = _pDocTab->getItemCount() - 1;
			_isFolding = true;
			if (current_index < last_index)
				switchToFile(_pDocTab->getBufferByIndex(current_index + 1));
			else
			{
				switchToFile(_pDocTab->getBufferByIndex(0)); // Loop around.
			}
			_isFolding = false;
		}
		break;

		case IDM_VIEW_TAB_PREV:
		{
			const int current_index = _pDocTab->getCurrentTabIndex();
			_isFolding = true;
			if (current_index > 0)
				switchToFile(_pDocTab->getBufferByIndex(current_index - 1));
			else
			{
				const int last_index = _pDocTab->getItemCount() - 1;
				switchToFile(_pDocTab->getBufferByIndex(last_index)); // Loop around.
			}
			_isFolding = false;
		}
		break;


		case IDM_EDIT_REMOVE_CONSECUTIVE_DUP_LINES:
			_pEditView->execute(SCI_BEGINUNDOACTION);
			removeDuplicateLines();
			_pEditView->execute(SCI_ENDUNDOACTION);
			break;


		case IDM_EDIT_REMOVEEMPTYLINES:
			_pEditView->execute(SCI_BEGINUNDOACTION);
			removeEmptyLine(false);
			_pEditView->execute(SCI_ENDUNDOACTION);
			break;

		case IDM_EDIT_REMOVEEMPTYLINESWITHBLANK:
			_pEditView->execute(SCI_BEGINUNDOACTION);
			removeEmptyLine(true);
			_pEditView->execute(SCI_ENDUNDOACTION);
			break;

		case IDM_EDIT_BLOCK_COMMENT:
			doBlockComment(cm_toggle);
 			break;

		case IDM_EDIT_BLOCK_COMMENT_SET:
			doBlockComment(cm_comment);
			break;

		case IDM_EDIT_BLOCK_UNCOMMENT:
			doBlockComment(cm_uncomment);
			break;

		case IDM_EDIT_STREAM_COMMENT:
			doStreamComment();
			break;

		case IDM_EDIT_STREAM_UNCOMMENT:
			undoStreamComment();
			break;

		case IDM_EDIT_TRIMTRAILING:
		{
			std::lock_guard<std::mutex> lock(command_mutex);

			_pEditView->execute(SCI_BEGINUNDOACTION);
			doTrim(lineTail);
			_pEditView->execute(SCI_ENDUNDOACTION);
			break;
		}

		case IDM_EDIT_TRIMLINEHEAD:
		{
			std::lock_guard<std::mutex> lock(command_mutex);

			_pEditView->execute(SCI_BEGINUNDOACTION);
			doTrim(lineHeader);
			_pEditView->execute(SCI_ENDUNDOACTION);
			break;
		}

		case IDM_EDIT_TRIM_BOTH:
		{
			std::lock_guard<std::mutex> lock(command_mutex);

			_pEditView->execute(SCI_BEGINUNDOACTION);
			doTrim(lineBoth);
			_pEditView->execute(SCI_ENDUNDOACTION);
			break;
		}

		case IDM_EDIT_EOL2WS:
			_pEditView->execute(SCI_BEGINUNDOACTION);
			eol2ws();
			_pEditView->execute(SCI_ENDUNDOACTION);
			break;

		case IDM_EDIT_TRIMALL:
		{
			std::lock_guard<std::mutex> lock(command_mutex);

			_pEditView->execute(SCI_BEGINUNDOACTION);
			bool isEntireDoc = _pEditView->execute(SCI_GETANCHOR) == _pEditView->execute(SCI_GETCURRENTPOS);
			doTrim(lineBoth);
			if (isEntireDoc || _pEditView->execute(SCI_GETANCHOR) != _pEditView->execute(SCI_GETCURRENTPOS))
				eol2ws();
			_pEditView->execute(SCI_ENDUNDOACTION);
			break;
		}

		case IDM_EDIT_TAB2SW:
			wsTabConvert(tab2Space);
			break;

		case IDM_EDIT_SW2TAB_LEADING:
			wsTabConvert(space2TabLeading);
			break;

		case IDM_EDIT_SW2TAB_ALL:
			wsTabConvert(space2TabAll);
			break;


		case IDM_EDIT_SETREADONLYFORALLDOCS:
		{
			changeReadOnlyUserModeForAllOpenedTabs(true);
			break;
		}

		case IDM_EDIT_CLEARREADONLYFORALLDOCS:
		{
			changeReadOnlyUserModeForAllOpenedTabs(false);
			break;
		}

		case IDM_EDIT_TOGGLESYSTEMREADONLY:
		{
			Buffer* buf = _pEditView->getCurrentBuffer();
			bool isSysReadOnly = false;
			if (toggleReadOnlyFlagFromFileAttributes(buf->getFullPathName(), isSysReadOnly))
			{
				buf->setFileReadOnly(isSysReadOnly);
			}
			else
			{
				// NPLX: no administrator mode on Linux; the owner (or root) can change the permissions
				_nativeLangSpeaker.messageBox("NoAdminRight2ChangeReadOnlyFileAttribute",
					L"Please run Notepad++ as administrator to change the file attributes.",
					L"Changing file read-only attribute failed",
					MB_OK | MB_ICONWARNING);
			}
			break;
		}

		case IDM_EDIT_MULTISELECTALL:
		case IDM_EDIT_MULTISELECTALLMATCHCASE:
		case IDM_EDIT_MULTISELECTALLWHOLEWORD:
		case IDM_EDIT_MULTISELECTALLMATCHCASEWHOLEWORD:
		{
			_multiSelectFlag = id == IDM_EDIT_MULTISELECTALL ? 0 :
				(id == IDM_EDIT_MULTISELECTALLMATCHCASE ? SCFIND_MATCHCASE :
					(id == IDM_EDIT_MULTISELECTALLWHOLEWORD ? SCFIND_WHOLEWORD: SCFIND_MATCHCASE| SCFIND_WHOLEWORD));

			// Don't use _pEditView->hasSelection() because when multi-selection is active and main selection has no selection,
			// it will cause an infinite loop on SCI_MULTIPLESELECTADDEACH. See:
			// https://github.com/notepad-plus-plus/notepad-plus-plus/pull/14330#issuecomment-1797080251
			bool hasSelection = (_pEditView->execute(SCI_GETSELECTIONSTART) != _pEditView->execute(SCI_GETSELECTIONEND));
			if (!hasSelection)
				_pEditView->expandWordSelection();

			_pEditView->execute(SCI_TARGETWHOLEDOCUMENT);

			// Firstly do a selection of the word on which the cursor is
			_pEditView->execute(SCI_SETSEARCHFLAGS, _multiSelectFlag);
			_pEditView->execute(SCI_MULTIPLESELECTADDEACH);
		}
		break;

		case IDM_EDIT_MULTISELECTNEXT:
		case IDM_EDIT_MULTISELECTNEXTMATCHCASE:
		case IDM_EDIT_MULTISELECTNEXTWHOLEWORD:
		case IDM_EDIT_MULTISELECTNEXTMATCHCASEWHOLEWORD:
		{
			_multiSelectFlag = id == IDM_EDIT_MULTISELECTNEXT ? 0 :
				(id == IDM_EDIT_MULTISELECTNEXTMATCHCASE ? SCFIND_MATCHCASE :
					(id == IDM_EDIT_MULTISELECTNEXTWHOLEWORD ? SCFIND_WHOLEWORD : SCFIND_MATCHCASE | SCFIND_WHOLEWORD));

			_pEditView->execute(SCI_TARGETWHOLEDOCUMENT);
			_pEditView->execute(SCI_SETSEARCHFLAGS, _multiSelectFlag);
			_pEditView->execute(SCI_MULTIPLESELECTADDNEXT);
		}
		break;

		case IDM_EDIT_MULTISELECTUNDO:
		{
			LRESULT n = _pEditView->execute(SCI_GETSELECTIONS);
			if (n > 0)
				_pEditView->execute(SCI_DROPSELECTIONN, n - 1);
		}
		break;

		case IDM_EDIT_MULTISELECTSSKIP:
		{
			_pEditView->execute(SCI_TARGETWHOLEDOCUMENT);
			_pEditView->execute(SCI_SETSEARCHFLAGS, _multiSelectFlag); // use the last used flag to select the next one
			_pEditView->execute(SCI_MULTIPLESELECTADDNEXT);

			LRESULT n = _pEditView->execute(SCI_GETSELECTIONS);
			if (n > 1)
				_pEditView->execute(SCI_DROPSELECTIONN, n - 2);
		}
		break;


		case IDM_SEARCH_CUTMARKEDLINES :
			cutMarkedLines();
			break;

		case IDM_SEARCH_COPYMARKEDLINES :
			copyMarkedLines();
			break;

		case IDM_SEARCH_PASTEMARKEDLINES :
			pasteToMarkedLines();
			break;

		case IDM_SEARCH_DELETEMARKEDLINES :
			deleteMarkedLines(true);
			break;

		case IDM_SEARCH_DELETEUNMARKEDLINES :
			deleteMarkedLines(false);
			break;

		case IDM_SEARCH_INVERSEMARKS :
			inverseMarks();
			break;

		case IDM_VIEW_FULLSCREENTOGGLE:
		{
			if (!_beforeSpecialView._isDistractionFree)
				fullScreenToggle();
		}
		break;

		case IDM_VIEW_POSTIT :
		{
			if (!_beforeSpecialView._isDistractionFree)
				postItToggle();
		}
		break;

		case IDM_VIEW_DISTRACTIONFREE:
		{
			if ((_beforeSpecialView._isDistractionFree && _beforeSpecialView._isFullScreen && _beforeSpecialView._isPostIt) ||
				(!_beforeSpecialView._isDistractionFree && !_beforeSpecialView._isFullScreen && !_beforeSpecialView._isPostIt))
				distractionFreeToggle();
		}
		break;

	    case IDM_VIEW_ALWAYSONTOP:
		{
			const bool check = !isMenuItemChecked(id);
			checkMenuItem(id, check);
			// NPLX: was SetWindowPos(HWND_TOPMOST); honoured on X11, Wayland compositors decide
			setWindowFlag(Qt::WindowStaysOnTopHint, check);
			show();
		}
		break;



		case IDM_VIEW_FOLD_1:
		case IDM_VIEW_FOLD_2:
		case IDM_VIEW_FOLD_3:
		case IDM_VIEW_FOLD_4:
		case IDM_VIEW_FOLD_5:
		case IDM_VIEW_FOLD_6:
		case IDM_VIEW_FOLD_7:
		case IDM_VIEW_FOLD_8:
			_isFolding = true; // So we can ignore events while folding is taking place
 			_pEditView->foldLevel(id - IDM_VIEW_FOLD - 1, fold_collapse);
			_isFolding = false;
			break;

		case IDM_VIEW_UNFOLD_1:
		case IDM_VIEW_UNFOLD_2:
		case IDM_VIEW_UNFOLD_3:
		case IDM_VIEW_UNFOLD_4:
		case IDM_VIEW_UNFOLD_5:
		case IDM_VIEW_UNFOLD_6:
		case IDM_VIEW_UNFOLD_7:
		case IDM_VIEW_UNFOLD_8:
			_isFolding = true; // So we can ignore events while folding is taking place
 			_pEditView->foldLevel(id - IDM_VIEW_UNFOLD - 1, fold_expand);
			_isFolding = false;
			break;

		case IDM_VIEW_HIDELINES:
		{
			_pEditView->hideLines();
			break;
		}

		case IDM_VIEW_ZOOM_SYNC:
		{
			NppParameters& nppParam = NppParameters::getInstance();
			ScintillaViewParams& svp = (ScintillaViewParams&)nppParam.getSVP();
			svp._zoomSync = !svp._zoomSync;
			checkMenuItem(IDM_VIEW_ZOOM_SYNC, svp._zoomSync);
			if (svp._zoomSync)
				syncZoom();
			break;
		}

		case IDM_VIEW_NPC:
		{
			const bool isChecked = !(isMenuItemChecked(id));
			checkMenuItem(id, isChecked);

			auto& svp1 = const_cast<ScintillaViewParams&>(NppParameters::getInstance().getSVP());
			svp1._npcShow = isChecked;

			// setNpcAndCcUniEOL() in showNpc() uses svp1._npcShow
			_mainEditView->showNpc(isChecked);
			_subEditView->showNpc(isChecked);

			const bool allChecked = svp1._whiteSpaceShow && svp1._eolShow && svp1._npcShow && svp1._ccUniEolShow;

			checkMenuItem(IDM_VIEW_ALL_CHARACTERS, allChecked);

			_findReplaceDlg->updateFinderScintillaForNpc();

			break;
		}

		case IDM_VIEW_NPC_CCUNIEOL:
		{
			const bool isChecked = !(isMenuItemChecked(id));
			checkMenuItem(id, isChecked);

			auto& svp1 = const_cast<ScintillaViewParams&>(NppParameters::getInstance().getSVP());
			svp1._ccUniEolShow = isChecked;

			// setNpcAndCcUniEOL() in showCcUniEol() uses svp1._ccUniEolShow
			_mainEditView->showCcUniEol(isChecked);
			_subEditView->showCcUniEol(isChecked);

			const bool allChecked = svp1._whiteSpaceShow && svp1._eolShow && svp1._npcShow && svp1._ccUniEolShow;

			checkMenuItem(IDM_VIEW_ALL_CHARACTERS, allChecked);

			// inform the relevant lexers about the update
			std::vector<ScintillaEditView*> pViews{ _mainEditView, _subEditView };
			for (auto pView : pViews)
			{
				Buffer* pBuf = pView->getCurrentBuffer();
				// ErrorList and EscapeSequence both need to set visiblity of ANSI control sequences based on View setting
				if (pBuf->getLangType() == L_ERRORLIST)
				{
					pView->execute(SCI_STYLESETVISIBLE, static_cast<WPARAM>(SCE_ERR_ESCSEQ), static_cast<LPARAM>(isChecked));
					pView->execute(SCI_STYLESETVISIBLE, static_cast<WPARAM>(SCE_ERR_ESCSEQ_UNKNOWN), static_cast<LPARAM>(isChecked));
				}
				else if (pBuf->getLangType() == L_ESCSEQ)
				{
					pView->execute(SCI_STYLESETVISIBLE, static_cast<WPARAM>(SCE_ESCSEQ_IDENTIFIER), static_cast<LPARAM>(isChecked));
					pView->execute(SCI_STYLESETVISIBLE, static_cast<WPARAM>(SCE_ESCSEQ_UNKNOWN), static_cast<LPARAM>(isChecked));
				}
			}

			break;
		}


        case IDM_FORMAT_WIN_1250 :
        case IDM_FORMAT_WIN_1251 :
        case IDM_FORMAT_WIN_1252 :
        case IDM_FORMAT_WIN_1253 :
        case IDM_FORMAT_WIN_1254 :
        case IDM_FORMAT_WIN_1255 :
        case IDM_FORMAT_WIN_1256 :
        case IDM_FORMAT_WIN_1257 :
        case IDM_FORMAT_WIN_1258 :
        case IDM_FORMAT_ISO_8859_1  :
        case IDM_FORMAT_ISO_8859_2  :
        case IDM_FORMAT_ISO_8859_3  :
        case IDM_FORMAT_ISO_8859_4  :
        case IDM_FORMAT_ISO_8859_5  :
        case IDM_FORMAT_ISO_8859_6  :
        case IDM_FORMAT_ISO_8859_7  :
        case IDM_FORMAT_ISO_8859_8  :
        case IDM_FORMAT_ISO_8859_9  :
        case IDM_FORMAT_ISO_8859_13 :
        case IDM_FORMAT_ISO_8859_14 :
        case IDM_FORMAT_ISO_8859_15 :
        case IDM_FORMAT_DOS_437 :
        case IDM_FORMAT_DOS_720 :
        case IDM_FORMAT_DOS_737 :
        case IDM_FORMAT_DOS_775 :
        case IDM_FORMAT_DOS_850 :
        case IDM_FORMAT_DOS_852 :
        case IDM_FORMAT_DOS_855 :
        case IDM_FORMAT_DOS_857 :
        case IDM_FORMAT_DOS_858 :
        case IDM_FORMAT_DOS_860 :
        case IDM_FORMAT_DOS_861 :
        case IDM_FORMAT_DOS_862 :
        case IDM_FORMAT_DOS_863 :
        case IDM_FORMAT_DOS_865 :
        case IDM_FORMAT_DOS_866 :
        case IDM_FORMAT_DOS_869 :
        case IDM_FORMAT_BIG5 :
        case IDM_FORMAT_GB2312 :
        case IDM_FORMAT_SHIFT_JIS :
        case IDM_FORMAT_KOREAN_WIN :
        case IDM_FORMAT_EUC_KR :
        case IDM_FORMAT_TIS_620 :
        case IDM_FORMAT_MAC_CYRILLIC :
        case IDM_FORMAT_KOI8U_CYRILLIC :
        case IDM_FORMAT_KOI8R_CYRILLIC :
        {
			int index = id - IDM_FORMAT_ENCODE;

			const EncodingMapper& em = EncodingMapper::getInstance();
			int encoding = em.getEncodingFromIndex(index);
			if (encoding == -1)
			{
				//printStr(L"Encoding problem. Command is not added in encoding_table?");
				return;
			}

            Buffer* buf = _pEditView->getCurrentBuffer();
            if (buf->isDirty())
            {
				int answer = _nativeLangSpeaker.messageBox("SaveCurrentModifWarning",
					L"You should save the current modification.\rAll the saved modifications cannot be undone.\r\rContinue?",
					L"Save Current Modification",
					MB_YESNO);

                if (answer == IDYES)
                {
                    fileSave();
					_pEditView->execute(SCI_EMPTYUNDOBUFFER);
                }
                else
                    return;
            }

            if (_pEditView->execute(SCI_CANUNDO) == TRUE)
            {
				int answer = _nativeLangSpeaker.messageBox("LoseUndoAbilityWarning",
					L"You should save the current modification.\rAll the saved modifications cannot be undone.\r\rContinue?",
					L"Lose Undo Ability Warning",
					MB_YESNO);

                if (answer != IDYES)
                    return;
            }

            if (!buf->isDirty())
            {
				buf->setEncoding(encoding);
				buf->setUnicodeMode(uniUTF8_NoBOM);

				MainFileManager.disableAutoDetectEncoding4Loading();
				fileReload();
				MainFileManager.enableAutoDetectEncoding4Loading();
            }
			break;
		}

		case IDM_FORMAT_CONV2_ANSI:
		case IDM_FORMAT_CONV2_AS_UTF_8:
		case IDM_FORMAT_CONV2_UTF_8:
		case IDM_FORMAT_CONV2_UTF_16BE:
		case IDM_FORMAT_CONV2_UTF_16LE:
		{
			int idEncoding = -1;
			Buffer *buf = _pEditView->getCurrentBuffer();
            UniMode um = buf->getUnicodeMode();
            int encoding = buf->getEncoding();

			switch(id)
			{
				case IDM_FORMAT_CONV2_ANSI:
				{
                    if (encoding != -1)
                    {
                        // do nothing
                        return;
                    }
                    else
                    {
					    if (um == uni8Bit)
						    return;

                        // set scintilla to ANSI
					    idEncoding = IDM_FORMAT_ANSI;
                    }
					break;
				}
				case IDM_FORMAT_CONV2_AS_UTF_8:
				{
                    if (encoding != -1)
                    {
                        buf->setDirty(true);
                        buf->setUnicodeMode(uniUTF8_NoBOM);
                        buf->setEncoding(-1);
                        return;
                    }

					idEncoding = IDM_FORMAT_AS_UTF_8;
					if (um == uniUTF8_NoBOM)
						return;

					if (um != uni8Bit)
					{
						command(idEncoding);
						_pEditView->execute(SCI_EMPTYUNDOBUFFER);
						return;
					}

					break;
				}
				case IDM_FORMAT_CONV2_UTF_8:
				{
                    if (encoding != -1)
                    {
                        buf->setDirty(true);
                        buf->setUnicodeMode(uniUTF8);
                        buf->setEncoding(-1);
                        return;
                    }

					idEncoding = IDM_FORMAT_UTF_8;
					if (um == uniUTF8)
						return;

					if (um != uni8Bit)
					{
						command(idEncoding);
						_pEditView->execute(SCI_EMPTYUNDOBUFFER);
						return;
					}
					break;
				}

				case IDM_FORMAT_CONV2_UTF_16BE:
				{
                    if (encoding != -1)
                    {
                        buf->setDirty(true);
                        buf->setUnicodeMode(uni16BE);
                        buf->setEncoding(-1);
                        return;
                    }

					idEncoding = IDM_FORMAT_UTF_16BE;
					if (um == uni16BE)
						return;

					if (um != uni8Bit)
					{
						command(idEncoding);
						_pEditView->execute(SCI_EMPTYUNDOBUFFER);
						return;
					}
					break;
				}

				case IDM_FORMAT_CONV2_UTF_16LE:
				{
                    if (encoding != -1)
                    {
                        buf->setDirty(true);
                        buf->setUnicodeMode(uni16LE);
                        buf->setEncoding(-1);
                        return;
                    }

					idEncoding = IDM_FORMAT_UTF_16LE;
					if (um == uni16LE)
						return;
					if (um != uni8Bit)
					{
						command(idEncoding);
						_pEditView->execute(SCI_EMPTYUNDOBUFFER);
						return;
					}
					break;
				}
			}

			if (idEncoding != -1)
			{
				// try to save the current clipboard text content 1st
				// NPLX: QClipboard in place of the CF_UNICODETEXT HGLOBAL copy
				QClipboard* clipboard = QGuiApplication::clipboard();
				const QString clipboardCopy = clipboard->text();

				_pEditView->saveCurrentPos();

				bool bPreviousCHPanelTrackingState = true;
				if (_pClipboardHistoryPanel)
					bPreviousCHPanelTrackingState = _pClipboardHistoryPanel->trackClipboardOps(false); // we do not want to track & show the next Clipboard op

				// Cut all text
				size_t docLen = _pEditView->getCurrentDocLen();
				_pEditView->execute(SCI_COPYRANGE, 0, docLen);
				_pEditView->execute(SCI_CLEARALL);

				if (_pClipboardHistoryPanel)
					_pClipboardHistoryPanel->trackClipboardOps(bPreviousCHPanelTrackingState); // restore


				// Change to the proper buffer, save buffer status

				command(idEncoding);

				// Paste the text, restore buffer status
				_pEditView->execute(SCI_PASTE);
				_pEditView->restoreCurrentPosPreStep();

				// Restore the previous Clipboard data if any
				if (!clipboardCopy.isEmpty())
					clipboard->setText(clipboardCopy);
				else
					clipboard->clear(); // no previous Clipboard data, clear the ones used by the Scintilla's conversion

				//Do not free anything, EmptyClipboard does that
				_pEditView->execute(SCI_EMPTYUNDOBUFFER);

				// The "save" point is on dirty state, so let's memorize it
				buf->setSavePointDirty(true);
			}
			break;
		}

		case IDM_SYSTRAYPOPUP_NEWDOC:
		{
			showFromTray();
			fileNew();
		}
		break;

		case IDM_SYSTRAYPOPUP_ACTIVATE :
		{
			showFromTray();
		}
		break;

		case IDM_SYSTRAYPOPUP_NEW_AND_PASTE:
		{
			showFromTray();

			BufferID bufferID = _pEditView->getCurrentBufferID();
			Buffer * buf = MainFileManager.getBufferByID(bufferID);
			if (!buf->isUntitled() || buf->docLength() != 0)
			{
				fileNew();
			}
			// NPLX: upstream IDM_EDIT_PASTE pastes where the focus is, the editor once ShowWindow returns; a window
			// just out of the tray is not active yet, so the editor gets the text directly
			_pEditView->setFocus();
			_pEditView->execute(SCI_PASTE);
		}
		break;

		case IDM_SYSTRAYPOPUP_OPENFILE:
		{
			showFromTray();
			fileOpen();
		}
		break;

		case IDM_SYSTRAYPOPUP_CLOSE:
		{
			if (_pTrayIco)
				_pTrayIco->hide();
			if (!isVisible())
				close(); // upstream WM_CLOSE: not spontaneous, so it quits
		}
		break;

		case IDM_PINTAB:
			// upstream: TCN_TABPINNED on the current tab
			tabPinned(_pDocTab, _pDocTab->getCurrentTabIndex());
			break;

        case IDM_VIEW_GOTO_START:
			_pDocTab->tabToStart();
			break;

        case IDM_VIEW_GOTO_END:
			_pDocTab->tabToEnd();
			break;

		case IDM_EDIT_SEARCHONINTERNET:
		{
			if (_pEditView->execute(SCI_GETSELECTIONS) != 1) // Multi-Selection || Column mode || no selection
				return;

			const NppGUI & nppGui = (NppParameters::getInstance()).getNppGUI();
			wstring url;
			if (nppGui._searchEngineChoice == nppGui.se_custom)
			{
				url = nppGui._searchEngineCustom;
				url.erase(std::remove_if(url.begin(), url.end(), [](wchar_t x) {return std::iswspace(x); }),
					url.end());

				auto httpPos = url.find(L"http://");
				auto httpsPos = url.find(L"https://");

				if (url.empty() || (httpPos != 0 && httpsPos != 0)) // if string is not a url (for launching only browser)
				{
					url = L"https://www.google.com/search?q=$(CURRENT_WORD)";
				}
			}
			else if (nppGui._searchEngineChoice == nppGui.se_duckDuckGo || nppGui._searchEngineChoice == nppGui.se_bing)
			{
				url = L"https://duckduckgo.com/?q=$(CURRENT_WORD)";
			}
			else if (nppGui._searchEngineChoice == nppGui.se_google)
			{
				url = L"https://www.google.com/search?q=$(CURRENT_WORD)";
			}
			else if (nppGui._searchEngineChoice == nppGui.se_yahoo)
			{
				url = L"https://search.yahoo.com/search?q=$(CURRENT_WORD)";
			}
			else if (nppGui._searchEngineChoice == nppGui.se_stackoverflow)
			{
				url = L"https://stackoverflow.com/search?q=$(CURRENT_WORD)";
			}

			// NPLX: upstream Command::run expands $(CURRENT_WORD) and launches the URL; QDesktopServices opens it
			const wstring currentWord = _pEditView->getSelectedTextToWChar(true);
			url = stringReplace(url, L"$(CURRENT_WORD)", QString::fromUtf8(QUrl::toPercentEncoding(QString::fromStdWString(currentWord))).toStdWString());
			QDesktopServices::openUrl(QUrl(QString::fromStdWString(url)));
		}
		break;

		case IDM_EDIT_COPY_ALL_NAMES:
		case IDM_EDIT_COPY_ALL_PATHS:
			{
				std::vector<DocTabView*> docTabs;
				if (viewVisible(MAIN_VIEW))
					docTabs.push_back(_mainDocTab);
				if (viewVisible(SUB_VIEW))
					docTabs.push_back(_subDocTab);
				std::vector<Buffer*> buffers;
				for (auto&& docTab : docTabs)
				{
					for (size_t i = 0, len = docTab->nbItem(); i < len; ++i)
					{
						BufferID bufID = docTab->getBufferByIndex(i);
						Buffer* buf = MainFileManager.getBufferByID(bufID);
						// Don't add duplicates because a buffer might be cloned in other view.
						if (docTabs.size() < 2 || std::find(buffers.begin(), buffers.end(), buf) == buffers.end())
							buffers.push_back(buf);
					}
				}
				buf2Clipboard(buffers, id == IDM_EDIT_COPY_ALL_PATHS);
			}
			break;

	    case IDM_SEARCH_CHANGED_PREV:
	    case IDM_SEARCH_CHANGED_NEXT:
			changedHistoryGoTo(id);
		    break;
			
		case IDM_SEARCH_CLEAR_CHANGE_HISTORY:
			clearChangesHistory(currentView());
			break;

		case IDM_HOMESWEETHOME :
		{
			// NPLX: the NotePadLX repository (upstream: https://notepad-plus-plus.org/)
			QDesktopServices::openUrl(QUrl(QStringLiteral(NPLX_HOMEPAGE_URL)));
			break;
		}
		case IDM_PROJECTPAGE :
		{
			// NPLX: "Report an Issue" on the NotePadLX repository (upstream: the Notepad++ project page)
			QDesktopServices::openUrl(QUrl(QStringLiteral(NPLX_HOMEPAGE_URL "/issues")));
			break;
		}

		case IDM_ONLINEDOCUMENT:
		{
			QDesktopServices::openUrl(QUrl(QStringLiteral("https://npp-user-manual.org/"))); // NPLX: was ::ShellExecute
			break;
		}


		case IDM_FORUM:
		{
			QDesktopServices::openUrl(QUrl(QStringLiteral("https://community.notepad-plus-plus.org/"))); // NPLX: was ::ShellExecute
			break;
		}

		case IDM_EDIT_RTL :
		case IDM_EDIT_LTR :
		{
			bool toRTL = id == IDM_EDIT_RTL;

			bool isRTL = _pEditView->isTextDirectionRTL();

			if ((toRTL && isRTL) || (!toRTL && !isRTL))
			{
				if (! ((NppParameters::getInstance()).getNppGUI())._muteSounds)
					QApplication::beep(); // NPLX: was ::MessageBeep

				return;
			}

			_pEditView->changeTextDirection(toRTL);

			// Wrap then !wrap to fix problem of mirror characters
			bool isWrapped = _pEditView->isWrap();
			_pEditView->wrap(!isWrapped);
			_pEditView->wrap(isWrapped);

			if (_pDocMap)
			{
				_pDocMap->changeTextDirection(toRTL);
			}
		}
		break;

		case IDM_EDIT_COLUMNMODETIP :
		{
			_nativeLangSpeaker.messageBox("ColumnModeTip",
					L"There are 3 ways to switch to column-select mode:\r\n\r\n"
					L"1. (Keyboard and Mouse)  Hold Alt while left-click dragging\r\n\r\n"
					L"2. (Keyboard only)  Hold Alt+Shift while using arrow keys\r\n\r\n"
					L"3. (Keyboard or Mouse)\r\n"
					L"      Put caret at desired start of column block position, then\r\n"
					L"       execute \"Begin/End Select in Column Mode\" command;\r\n"
					L"      Move caret to desired end of column block position, then\r\n"
					L"       execute \"Begin/End Select in Column Mode\" command again\r\n",
					L"Column Mode Tip",
					MB_OK|MB_APPLMODAL);
		}
		break;


		case IDM_EDIT_INSERT_DATETIME_SHORT:
		case IDM_EDIT_INSERT_DATETIME_LONG:
		{
			// NPLX: QLocale in place of GetDateFormatEx / GetTimeFormatEx (TIME_NOSECONDS)
			const QDateTime currentTime = QDateTime::currentDateTime();
			const QLocale locale = QLocale::system();
			const QLocale::FormatType dateFlag = (id == IDM_EDIT_INSERT_DATETIME_SHORT) ? QLocale::ShortFormat : QLocale::LongFormat;
			const wstring dateStr = locale.toString(currentTime.date(), dateFlag).toStdWString();
			const wstring timeStr = locale.toString(currentTime.time(), QLocale::ShortFormat).toStdWString();

			wstring dateTimeStr;
			if (NppParameters::getInstance().getNppGUI()._dateTimeReverseDefaultOrder)
			{
				// reverse default order: DATE + TIME
				dateTimeStr = dateStr;
				dateTimeStr += L" ";
				dateTimeStr += timeStr;
			}
			else
			{
				// default: TIME + DATE (Microsoft Notepad behaviour)
				dateTimeStr = timeStr;
				dateTimeStr += L" ";
				dateTimeStr += dateStr;
			}
			_pEditView->execute(SCI_BEGINUNDOACTION);

			_pEditView->execute(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(""));
			_pEditView->addGenericText(dateTimeStr.c_str());

			_pEditView->execute(SCI_ENDUNDOACTION);
		}
		break;

		case IDM_EDIT_INSERT_DATETIME_CUSTOMIZED:
		{
			const std::time_t now = std::time(nullptr);
			std::tm currentTime{};
			localtime_r(&now, &currentTime); // NPLX: was ::GetLocalTime

			NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
			wstring dateTimeStr = getDateTimeStrFrom(nppGUI._dateTimeFormat, currentTime);

			_pEditView->execute(SCI_BEGINUNDOACTION);

			_pEditView->execute(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(""));
			_pEditView->addGenericText(dateTimeStr.c_str());

			_pEditView->execute(SCI_ENDUNDOACTION);
		}
		break;

		case IDM_VIEW_TAB_MOVEFORWARD:
		case IDM_VIEW_TAB_MOVEBACKWARD:
		{
			const int currentTabIndex = _pDocTab->getCurrentTabIndex();
			const int lastTabIndex = _pDocTab->getItemCount() - 1;
			int newTabIndex = currentTabIndex;

			if (id == IDM_VIEW_TAB_MOVEFORWARD)
			{
				if (currentTabIndex >= lastTabIndex)
					return;
				++newTabIndex;
			}
			else
			{
				if (currentTabIndex < 1)
					return;
				--newTabIndex;
			}

			// NPLX: QTabBar::moveTab in place of the TCM_GETITEM / TCM_SETITEM swap
			_pDocTab->moveTab(currentTabIndex, newTabIndex);
			_pDocTab->setCurrentIndex(newTabIndex);

			// upstream NPPM_INTERNAL_DOCORDERCHANGED
			if (_pDocumentListPanel)
			{
				_pDocumentListPanel->updateTabOrder();
			}
			// Notify plugins that the document order changed (hwndFrom: the new index of the current document)
			notifyPlugins(NPPN_DOCORDERCHANGED, reinterpret_cast<void*>(static_cast<intptr_t>(newTabIndex)),
				reinterpret_cast<uptr_t>(_pEditView->getCurrentBufferID()));
		}
		break;

		case IDM_VIEW_IN_FIREFOX:
		case IDM_VIEW_IN_CHROME:
		case IDM_VIEW_IN_EDGE:
		case IDM_VIEW_IN_IE:
		{
			auto currentBuf = _pEditView->getCurrentBuffer();
			if (!currentBuf->isUntitled())
			{
				// NPLX: the browsers are looked up in PATH (upstream: the "App Paths" registry entries)
				QStringList candidates;
				if (id == IDM_VIEW_IN_FIREFOX)
					candidates = { QStringLiteral("firefox") };
				else if (id == IDM_VIEW_IN_CHROME)
					candidates = { QStringLiteral("google-chrome"), QStringLiteral("google-chrome-stable"), QStringLiteral("chromium"), QStringLiteral("chromium-browser") };
				else if (id == IDM_VIEW_IN_EDGE)
					candidates = { QStringLiteral("microsoft-edge"), QStringLiteral("microsoft-edge-stable") };

				QString appPath;
				for (const QString& candidate : candidates)
				{
					appPath = QStandardPaths::findExecutable(candidate);
					if (!appPath.isEmpty())
						break;
				}

				if (!appPath.isEmpty())
				{
					QProcess::startDetached(appPath, { QString::fromWCharArray(currentBuf->getFullPathName()) });
				}
				else
				{
					_nativeLangSpeaker.messageBox("ViewInBrowser",
						L"Application cannot be found in your system.",
						L"View Current File in Browser",
						MB_OK);
				}
			}
		}
		break;

		default:
		{
			if (id > IDM_FILEMENU_LASTONE && id < (IDM_FILEMENU_LASTONE + _lastRecentFileList.getMaxNbLRF() + 1))
			{
				BufferID lastOpened = doOpen(_lastRecentFileList.getItem(id));
				if (lastOpened != BUFFER_INVALID)
				{
					switchToFile(lastOpened);
				}
				break;
			}
			if ((id > IDM_LANG_USER) && (id < IDM_LANG_USER_LIMIT))
			{
				// upstream: ::GetMenuString(_mainMenuHandle, id, langName, ...) gives the UDL name
				const QAction* udlAction = actionFor(id);
				if (udlAction)
					_pEditView->getCurrentBuffer()->setLangType(L_USER, udlAction->text().toStdWString().c_str());
				if (_pDocMap)
				{
					_pDocMap->setSyntaxHiliting();
				}
				break;
			}
			if ((id >= IDM_WINDOW_MRU_FIRST) && (id <= IDM_WINDOW_MRU_LIMIT))
			{
				activateDoc(id - IDM_WINDOW_MRU_FIRST);
				break;
			}
			if (id >= IDM_LANG && id < IDM_LANG + 400)
			{
				// upstream: case IDM_LANG_C ... setLanguage(menuID2LangType(id))
				LangType lang = menuID2LangType(id);
				if (lang != L_EXTERNAL || id >= IDM_LANG_EXTERNAL)
					setLanguage(lang);
				break;
			}
			if (((id >= ID_MACRO) && (id < ID_MACRO_LIMIT)) || ((id >= ID_USER_CMD) && (id < ID_USER_CMD_LIMIT))) // shortcuts.xml commands
			{
				runShortcutsXmlCommand(id);
				break;
			}
			if ((id >= ID_PLUGINS_CMD) && (id < ID_PLUGINS_CMD_LIMIT))
			{
				if (_pluginsManager)
					_pluginsManager->runPluginCommand(id - ID_PLUGINS_CMD);
				break;
			}
			if (_pluginsManager && _pluginsManager->inDynamicRange(id)) // in the dynamic range allocated with NPPM_ALLOCATECMDID
			{
				_pluginsManager->relayNppMessages(WM_COMMAND, id, 0);
				break;
			}
			statusBar()->showMessage(tr("This command is not yet ported to NotePadLX."), 3000);
			break;
		}
	}

	if (_recordingMacro)
		recordMenuCommand(id); // NPLX: upstream recording switch in NppMacro.cpp
}

QWidget* Notepad_plus::getHashDlg(int cmdID) const
{
	switch (cmdID)
	{
		case IDM_TOOL_MD5_GENERATE: return _md5FromTextDlg;
		case IDM_TOOL_SHA1_GENERATE: return _sha1FromTextDlg;
		case IDM_TOOL_SHA256_GENERATE: return _sha2FromTextDlg;
		case IDM_TOOL_SHA512_GENERATE: return _sha512FromTextDlg;
		case IDM_TOOL_MD5_GENERATEFROMFILE: return _md5FromFilesDlg;
		case IDM_TOOL_SHA1_GENERATEFROMFILE: return _sha1FromFilesDlg;
		case IDM_TOOL_SHA256_GENERATEFROMFILE: return _sha2FromFilesDlg;
		case IDM_TOOL_SHA512_GENERATEFROMFILE: return _sha512FromFilesDlg;
		default: return nullptr;
	}
}

void Notepad_plus::checkForUpdates()
{
	// NPLX: replaces the WinGUp updater: the latest release of the NotePadLX repository
	if (!_updateChecker)
		_updateChecker = new UpdateChecker(this);
	statusBar()->showMessage(tr("Checking for updates..."), 15000);
	_updateChecker->check([this](UpdateChecker::Result result, const QString& latest, const QString& page, const QString& error) {
		statusBar()->clearMessage();
		switch (result)
		{
			case UpdateChecker::Result::NewVersion:
			{
				const std::wstring msg = QStringLiteral("NotePadLX %1 is available (you have %2).\n\nDo you want to open its download page?")
					.arg(latest, QStringLiteral(NPLX_VERSION)).toStdWString();
				if (nplx::messageBox(msg, L"Check for Updates", MB_YESNO | MB_ICONINFORMATION) == IDYES)
					QDesktopServices::openUrl(QUrl(page));
				break;
			}
			case UpdateChecker::Result::UpToDate:
				nplx::messageBox(QStringLiteral("NotePadLX %1 is the latest version.").arg(QStringLiteral(NPLX_VERSION)).toStdWString(),
					L"Check for Updates", MB_OK | MB_ICONINFORMATION);
				break;
			case UpdateChecker::Result::Error:
				nplx::messageBox(QStringLiteral("The latest version could not be checked:\n%1\n\nThe releases are on %2/releases")
					.arg(error, QStringLiteral(NPLX_HOMEPAGE_URL)).toStdWString(), L"Check for Updates", MB_OK | MB_ICONWARNING);
				break;
		}
	});
}
