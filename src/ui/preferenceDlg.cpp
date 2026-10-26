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

#include "preferenceDlg.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

#include <QSystemTrayIcon>
#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QEvent>
#include <QGroupBox>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QStyleHints>
#include <QTimer>
#include <QToolTip>
#include <QUrl>

#include "ColourPicker.h"
#include "Common.h"
#include "EncodingMapper.h"
#include "LocalizationQt.h"
#include "Notepad_plus_msgs.h"
#include "NppConstants.h"
#include "NppDarkMode.h"
#include "NppDarkModeOptions.h"
#include "NppXml.h"
#include "Parameters.h"
#include "RcDialog.h"
#include "ScintillaEditView.h"
#include "UiServices.h"
#include "localization.h"
#include "menuCmdID.h"
#include "preference_rc.h"
#include "resource.h"
#include "shortcut.h"

#include "RcPreference.inc"

using namespace std;

static constexpr int BLINKRATE_FASTEST = 50;
static constexpr int BLINKRATE_SLOWEST = 2500;
static constexpr int BLINKRATE_INTERVAL = 50;

static constexpr int CARETLINEFRAME_SMALLEST = 1;
static constexpr int CARETLINEFRAME_LARGEST = 6;
static constexpr int CARETLINEFRAME_INTERVAL = 1;

static constexpr int BORDERWIDTH_SMALLEST = 0;
static constexpr int BORDERWIDTH_LARGEST = 30;
static constexpr int BORDERWIDTH_INTERVAL = 1;

static constexpr int PADDING_SMALLEST = 0;
static constexpr int PADDING_LARGEST = 30;
static constexpr int PADDING_INTERVAL = 1;

static constexpr int DISTRACTIONFREE_SMALLEST = 3;
static constexpr int DISTRACTIONFREE_LARGEST = 9;
static constexpr int DISTRACTIONFREE_INTERVAL = 1;

static constexpr int AUTOCOMPLETEFROMCHAR_SMALLEST = 1;
static constexpr int AUTOCOMPLETEFROMCHAR_LARGEST = 9;
static constexpr int AUTOCOMPLETEFROMCHAR_INTERVAL = 1;

// This int encoding array is built from "EncodingUnit encodings[]" (see EncodingMapper.cpp)
// And NewDocumentSubDlg will use "int encoding array" to get more info from "EncodingUnit encodings[]"
static constexpr int encodings[]{
	1250, 1251, 1252, 1253, 1254, 1255, 1256, 1257, 1258,
	28591, 28592, 28593, 28594, 28595, 28596, 28597, 28598, 28599, 28603, 28604, 28605,
	437, 720, 737, 775, 850, 852, 855, 857, 858, 860, 861, 862, 863, 865, 866, 869,
	950, 936, 932, 949, 51949, 874, 10007, 21866, 20866
};

namespace
{
	// upstream messageBox(..., MB_OK | MB_APPLMODAL)
	void infoBox(const char* tagName, const wchar_t* message, const wchar_t* title)
	{
		NativeLangSpeaker* pNativeSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
		pNativeSpeaker->messageBox(tagName, message, title, MB_OK | MB_APPLMODAL);
	}

	void setSliderRange(RcDialog* dlg, int id, int smallest, int largest, int interval, int pos)
	{
		if (QSlider* slider = dlg->slider(id))
		{
			QSignalBlocker blocker(slider);
			slider->setRange(smallest, largest);
			slider->setPageStep(interval);
			slider->setValue(pos);
		}
	}

	int comboData(const RcDialog* dlg, int id, int index)
	{
		QComboBox* combo = dlg->comboBox(id);
		return combo && index >= 0 ? combo->itemData(index).toInt() : 0;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// PreferenceSubDlg

PreferenceSubDlg::PreferenceSubDlg(QWidget* parent, ParentMessenger sendToParent)
	: RcDialog(parent, true), _sendToParent(std::move(sendToParent))
{
}

void PreferenceSubDlg::create(const RcDialogDef& def)
{
	buildFrom(def);

	// NPLX: the upstream labels name the program, and a two-line label is "...\r..."
	auto linuxText = [](QString text) {
		text.replace(QStringLiteral("Notepad++"), QStringLiteral("NotePadLX"));
		text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
		return text;
	};
	for (QAbstractButton* button : findChildren<QAbstractButton*>())
		button->setText(linuxText(button->text()));
	for (QLabel* label : findChildren<QLabel*>())
		label->setText(linuxText(label->text()));
	for (QGroupBox* group : findChildren<QGroupBox*>())
		group->setTitle(linuxText(group->title()));

	// WM_INITDIALOG: filling the controls does not notify
	_isUpdatingControls = true;
	initDialog();
	_isUpdatingControls = false;
}

void PreferenceSubDlg::setItemInt(int id, int value) const
{
	setItemText(id, std::to_wstring(value));
}

unsigned int PreferenceSubDlg::getItemInt(int id) const
{
	const wstring text = getItemText(id);
	if (text.empty())
		return 0;
	try
	{
		const long long value = std::stoll(text);
		return value < 0 ? 0 : static_cast<unsigned int>(std::min<long long>(value, UINT32_MAX));
	}
	catch (...)
	{
		return 0;
	}
}

void PreferenceSubDlg::setTip(int id, const std::wstring& tip) const
{
	if (QWidget* w = dlgItem(id))
	{
		// upstream tooltips wrap at about 260 px (TTM_SETMAXTIPWIDTH); rich text makes Qt wrap
		QString text = QString::fromStdWString(tip).toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
		w->setToolTip(QStringLiteral("<p style='white-space:pre-wrap'>%1</p>").arg(text));
	}
}

void PreferenceSubDlg::redrawDlgItem(int id) const
{
	if (QWidget* w = dlgItem(id))
		w->update();
}

// ---------------------------------------------------------------------------------------------------------------------
// PreferenceDlg

PreferenceDlg::PreferenceDlg(QWidget* parent)
	: RcDialog(parent)
{
	setObjectName(QStringLiteral("PreferenceDlg"));
	setModal(false);
}

PreferenceDlg::~PreferenceDlg() = default;

template <class T>
T* PreferenceDlg::addPage(const RcDialogDef& def, const wchar_t* name, const wchar_t* internalName)
{
	auto* page = new T(this, _sendToParent);
	page->create(def);
	// upstream: the pages are at 115,10 in IDD_PREFERENCE_BOX
	page->move(mapDialogRect(115, 10, def._w, def._h).topLeft());
	page->hide();
	_wVector.push_back(DlgInfo{ page, name, internalName });
	return page;
}

void PreferenceDlg::create()
{
	if (isCreated())
		return;

	buildFrom(IDD_PREFERENCE_BOX_def);

	// upstream WM_INITDIALOG (same order; File Association is not ported)
	addPage<GeneralSubDlg>(IDD_PREFERENCE_SUB_GENERAL_def, L"General", L"Global");
	_toolbarSubDlg = addPage<ToolbarSubDlg>(IDD_PREFERENCE_SUB_TOOLBAR_def, L"Toolbar", L"Toolbar");
	_tabbarSubDlg = addPage<TabbarSubDlg>(IDD_PREFERENCE_SUB_TABBAR_def, L"Tab Bar", L"Tabbar");
	addPage<EditingSubDlg>(IDD_PREFERENCE_SUB_EDITING_def, L"Editing 1", L"Scintillas");
	addPage<Editing2SubDlg>(IDD_PREFERENCE_SUB_EDITING2_def, L"Editing 2", L"Scintillas2");
	_darkModeSubDlg = addPage<DarkModeSubDlg>(IDD_PREFERENCE_SUB_DARKMODE_def, L"Dark Mode", L"DarkMode");
	addPage<MarginsBorderEdgeSubDlg>(IDD_PREFERENCE_SUB_MARGING_BORDER_EDGE_def, L"Margins/Border/Edge", L"MarginsBorderEdge");
	addPage<NewDocumentSubDlg>(IDD_PREFERENCE_SUB_NEWDOCUMENT_def, L"New Document", L"NewDoc");
	addPage<DefaultDirectorySubDlg>(IDD_PREFERENCE_SUB_DEFAULTDIRECTORY_def, L"Default Directory", L"DefaultDir");
	addPage<RecentFilesHistorySubDlg>(IDD_PREFERENCE_SUB_RECENTFILESHISTORY_def, L"Recent Files History", L"RecentFilesHistory");
	addPage<LanguageSubDlg>(IDD_PREFERENCE_SUB_LANGUAGE_def, L"Language", L"Language");
	addPage<IndentationSubDlg>(IDD_PREFERENCE_SUB_INDENTATION_def, L"Indentation", L"Indentation");
	addPage<HighlightingSubDlg>(IDD_PREFERENCE_SUB_HIGHLIGHTING_def, L"Highlighting", L"Highlighting");
	addPage<PrintSubDlg>(IDD_PREFERENCE_SUB_PRINT_def, L"Print", L"Print");
	addPage<SearchingSubDlg>(IDD_PREFERENCE_SUB_SEARCHING_def, L"Searching", L"Searching");
	addPage<BackupSubDlg>(IDD_PREFERENCE_SUB_BACKUP_def, L"Backup", L"Backup");
	addPage<AutoCompletionSubDlg>(IDD_PREFERENCE_SUB_AUTOCOMPLETION_def, L"Auto-Completion", L"AutoCompletion");
	addPage<MultiInstanceSubDlg>(IDD_PREFERENCE_SUB_MULTIINSTANCE_def, L"Multi-Instance & Date", L"MultiInstance");
	addPage<DelimiterSubDlg>(IDD_PREFERENCE_SUB_DELIMITER_def, L"Delimiter", L"Delimiter");
	addPage<PerformanceSubDlg>(IDD_PREFERENCE_SUB_PERFORMANCE_def, L"Performance", L"Performance");
	addPage<CloudAndLinkSubDlg>(IDD_PREFERENCE_SUB_CLOUD_LINK_def, L"Cloud & Link", L"Cloud");
	addPage<SearchEngineSubDlg>(IDD_PREFERENCE_SUB_SEARCHENGINE_def, L"Search Engine", L"SearchEngine");
	_miscSubDlg = addPage<MiscSubDlg>(IDD_PREFERENCE_SUB_MISC_def, L"MISC.", L"MISC");

	makeCategoryList();
	showDialogByIndex(0);

	// upstream IDM_SETTING_PREFERENCE: changePreferenceDlgLang on first use
	nplx::changePreferenceDlgLang(*this);
}

void PreferenceDlg::doDialog(bool isRTL)
{
	if (!isCreated())
	{
		if (isRTL)
			setLayoutDirection(Qt::RightToLeft);
		create();
		// upstream goToCenter
		if (QWidget* p = parentWidget())
			move(p->geometry().center() - rect().center());
	}
	show();
	raise();
	activateWindow();
}

void PreferenceDlg::onCommand(int id, int notification)
{
	if (id == IDC_LIST_DLGTITLE)
	{
		if (notification == RcNotifySelChange)
		{
			const int i = getListSelectedIndex();
			if (i >= 0)
				showDialogByIndex(i);
		}
		return;
	}

	switch (id)
	{
		case IDC_BUTTON_CLOSE:
		case IDCANCEL:
			hide();
			return;

		default:
			// upstream ::SendMessage(_hParent, WM_COMMAND, wParam, lParam)
			if (_sendToParent)
				_sendToParent(WM_COMMAND, id, 0);
			return;
	}
}

void PreferenceDlg::reject()
{
	hide(); // upstream IDCANCEL: display(false)
}

void PreferenceDlg::makeCategoryList()
{
	QListWidget* list = listBox(IDC_LIST_DLGTITLE);
	if (!list)
		return;
	{
		QSignalBlocker blocker(list);
		for (const DlgInfo& info : _wVector)
			list->addItem(QString::fromStdWString(info._name));
	}
	setListSelection(0);
}

int PreferenceDlg::getListSelectedIndex() const
{
	QListWidget* list = listBox(IDC_LIST_DLGTITLE);
	return list ? list->currentRow() : -1;
}

int PreferenceDlg::getIndexFromName(const wchar_t* name) const
{
	if (!name)
		return -1;

	int i = 0;
	for (auto it = _wVector.begin(); it != _wVector.end(); ++it, ++i)
	{
		if (it->_internalName == name)
			return i;
	}
	return -1;
}

bool PreferenceDlg::setListSelection(size_t currentSel) const
{
	QListWidget* list = listBox(IDC_LIST_DLGTITLE);
	if (!list || currentSel >= static_cast<size_t>(list->count()))
		return false;
	QSignalBlocker blocker(list);
	list->setCurrentRow(static_cast<int>(currentSel));
	return true;
}

bool PreferenceDlg::renameDialogTitle(const wchar_t* internalName, const wchar_t* newName)
{
	const int i = getIndexFromName(internalName);
	QListWidget* list = listBox(IDC_LIST_DLGTITLE);
	if (i < 0 || !list || i >= list->count())
		return false;

	_wVector[i]._name = newName;
	list->item(i)->setText(QString::fromWCharArray(newName));
	return true;
}

void PreferenceDlg::showDialogByName(const wchar_t* name)
{
	const int i = getIndexFromName(name);
	if (i >= 0)
	{
		showDialogByIndex(i);
		setListSelection(i);
	}
}

RcDialog* PreferenceDlg::pageByName(const wchar_t* internalName) const
{
	const int i = getIndexFromName(internalName);
	return i < 0 ? nullptr : _wVector[i]._dlg;
}

void PreferenceDlg::showDialogByIndex(size_t index) const
{
	for (size_t i = 0; i < _wVector.size(); ++i)
		_wVector[i]._dlg->setVisible(i == index);
}

bool PreferenceDlg::goToSection(size_t iPage, intptr_t ctrlID)
{
	if (iPage >= _wVector.size())
		return false;

	setListSelection(iPage);
	showDialogByIndex(iPage);
	activateWindow();

	if (ctrlID != -1)
	{
		QWidget* ctrl = _wVector[iPage]._dlg->dlgItem(static_cast<int>(ctrlID));
		if (!ctrl)
			return false;
		ctrl->setFocus();

		// upstream _gotoTip: "Find your setting here", hidden after 2 s
		NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
		const wstring hereTip = pNativeSpeaker->getLocalizedStrFromID("goto-setting-tip", L"Find your setting here");
		QToolTip::showText(ctrl->mapToGlobal(QPoint(0, ctrl->height())), QString::fromStdWString(hereTip), ctrl, QRect(), 2000);
	}
	return true;
}

void PreferenceDlg::setToolIconsSet(bool useDark)
{
	// upstream NPPM_INTERNAL_SETTOOLICONSSET: the check boxes of the toolbar page, the real work is done by
	// NPPM_INTERNAL_REFRESHDARKMODE
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	auto& nppGUITbInfo = nppGUI._tbIconInfo;
	nppGUITbInfo = NppDarkMode::getToolbarIconInfo(useDark);

	ToolbarSubDlg* tb = _toolbarSubDlg;
	if (!tb)
		return;

	tb->setChecked(IDC_RADIO_SMALLICON, nppGUITbInfo._tbIconSet == TB_SMALL);
	tb->setChecked(IDC_RADIO_BIGICON, nppGUITbInfo._tbIconSet == TB_LARGE);
	tb->setChecked(IDC_RADIO_SMALLICON2, nppGUITbInfo._tbIconSet == TB_SMALL2);
	tb->setChecked(IDC_RADIO_BIGICON2, nppGUITbInfo._tbIconSet == TB_LARGE2);
	tb->setChecked(IDC_RADIO_STANDARD, nppGUITbInfo._tbIconSet == TB_STANDARD);

	tb->setChecked(IDC_RADIO_RED, nppGUITbInfo._tbColor == FluentColor::red);
	tb->setChecked(IDC_RADIO_GREEN, nppGUITbInfo._tbColor == FluentColor::green);
	tb->setChecked(IDC_RADIO_BLUE, nppGUITbInfo._tbColor == FluentColor::blue);
	tb->setChecked(IDC_RADIO_PURPLE, nppGUITbInfo._tbColor == FluentColor::purple);
	tb->setChecked(IDC_RADIO_CYAN, nppGUITbInfo._tbColor == FluentColor::cyan);
	tb->setChecked(IDC_RADIO_OLIVE, nppGUITbInfo._tbColor == FluentColor::olive);
	tb->setChecked(IDC_RADIO_YELLOW, nppGUITbInfo._tbColor == FluentColor::yellow);
	tb->setChecked(IDC_RADIO_ACCENTCOLOR, nppGUITbInfo._tbColor == FluentColor::accent);
	tb->setChecked(IDC_RADIO_CUSTOMCOLOR, nppGUITbInfo._tbColor == FluentColor::custom);
	tb->setChecked(IDC_RADIO_DEFAULTCOLOR, nppGUITbInfo._tbColor == FluentColor::defaultColor);

	tb->setChecked(IDC_RADIO_COMPLETE, nppGUITbInfo._tbUseMono);
	tb->setChecked(IDC_RADIO_PARTIAL, !nppGUITbInfo._tbUseMono);

	tb->changeToolbarColorableState();
}

// ---------------------------------------------------------------------------------------------------------------------
// General

void GeneralSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	setChecked(IDC_CHECK_HIDESTATUSBAR, !nppGUI._statusBarShow);
	setChecked(IDC_CHECK_HIDEMENUBAR, !nppGUI._menuBarShow);
	setChecked(IDC_CHECK_HIDERIGHTSHORTCUTSOFMENUBAR, nppGUI._hideMenuRightShortcuts);

	LocalizationSwitcher& localizationSwitcher = nppParam.getLocalizationSwitcher();
	QComboBox* langCombo = comboBox(IDC_COMBO_LOCALIZATION);
	for (size_t i = 0, len = localizationSwitcher.size(); i < len; ++i)
	{
		pair<wstring, wstring> localizationInfo = localizationSwitcher.getElementFromIndex(i);
		langCombo->addItem(QString::fromStdWString(localizationInfo.first));
	}
	wstring lang = L"English"; // Set default language as English
	if (nppParam.getNativeLang()) // if nativeLang is not nullptr, then we can be sure the default language (English) is not used
	{
		string fn = localizationSwitcher.getFileName();
		wstring fnW = string2wstring(fn, CP_UTF8);
		lang = localizationSwitcher.getLangFromXmlFileName(fnW.c_str());
	}
	const int index = langCombo->findText(QString::fromStdWString(lang), Qt::MatchFixedString);
	if (index != -1)
		langCombo->setCurrentIndex(index);

	static constexpr const wchar_t* fontSizes[]{ L"8", L"9", L"10", L"11", L"12", L"14", L"16", L"18", L"20", L"22", L"24" };
	QComboBox* fontSizeCombo = comboBox(IDC_COMBO_DIALOG_FONT_SIZE);
	for (const auto& fontSize : fontSizes)
		fontSizeCombo->addItem(QString::fromWCharArray(fontSize));
	const int cbFontSzIdx = fontSizeCombo->findText(QString::number(nppParam.getDlgFontSize()));
	if (cbFontSzIdx != -1)
		fontSizeCombo->setCurrentIndex(cbFontSzIdx);

	QComboBox* fontNameCombo = comboBox(IDC_COMBO_DIALOG_FONT_NAME);
	for (const wstring& font : nppParam.getFontList())
		if (!font.empty())
			fontNameCombo->addItem(QString::fromStdWString(font));
	fontNameCombo->model()->sort(0); // CBS_SORT
	int iFontName = fontNameCombo->findText(QString::fromStdWString(nppGUI._fontDlgName));
	if (iFontName == -1)
	{
		nppGUI._fontDlgName = DEFAULT_DLG_FONT;
		iFontName = fontNameCombo->findText(QString::fromStdWString(nppGUI._fontDlgName));
	}
	fontNameCombo->setCurrentIndex(iFontName);

	// NPLX: the dialog font settings are stored for Notepad++ compatibility; NotePadLX dialogs use the desktop font
	const QString desktopFontTip = QStringLiteral("NotePadLX dialogs use the desktop font. This setting is kept in config.xml.");
	for (int id : { IDC_COMBO_DIALOG_FONT_SIZE, IDC_COMBO_DIALOG_FONT_NAME })
		dlgItem(id)->setToolTip(desktopFontTip);
	setItemText(IDC_DIALOG_FONT_GB_STATIC, L"Dialog Font (Notepad++ only)");
}

void GeneralSubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	switch (id)
	{
		case IDC_CHECK_HIDESTATUSBAR:
		{
			const bool isChecked = isCheckedOrNot(IDC_CHECK_HIDESTATUSBAR);
			sendToNpp(NPPM_HIDESTATUSBAR, 0, isChecked ? TRUE : FALSE);
			return;
		}

		case IDC_CHECK_HIDEMENUBAR:
		{
			bool isChecked = isCheckedOrNot(IDC_CHECK_HIDEMENUBAR);
			sendToNpp(NPPM_HIDEMENU, 0, isChecked ? TRUE : FALSE);
			return;
		}

		case IDC_CHECK_HIDERIGHTSHORTCUTSOFMENUBAR:
		{
			// NPLX: the menu bar has no "＋ ▼ ✕" items (they are tab bar commands); the setting is kept
			bool isChecked = isCheckedOrNot(IDC_CHECK_HIDERIGHTSHORTCUTSOFMENUBAR);
			nppGUI._hideMenuRightShortcuts = isChecked;
			sendToNpp(NPPM_INTERNAL_HIDEMENURIGHTSHORTCUTS, 0, isChecked ? TRUE : FALSE);
			return;
		}

		case IDC_COMBO_LOCALIZATION:
		{
			if (notification != RcNotifySelChange)
				return;
			LocalizationSwitcher& localizationSwitcher = nppParam.getLocalizationSwitcher();
			const wstring langName = comboBox(IDC_COMBO_LOCALIZATION)->currentText().toStdWString();
			if (!langName.empty())
			{
				// Make English as basic language
				if (localizationSwitcher.switchToLang(L"English"))
					sendToNpp(NPPM_INTERNAL_RELOADNATIVELANG, FALSE, 0);

				// Change the language
				if (localizationSwitcher.switchToLang(langName.c_str()))
					sendToNpp(NPPM_INTERNAL_RELOADNATIVELANG, TRUE, 0);
			}
			return;
		}

		case IDC_COMBO_DIALOG_FONT_SIZE:
		{
			if (notification != RcNotifySelChange)
				return;
			const int fontSize = comboBox(IDC_COMBO_DIALOG_FONT_SIZE)->currentText().toInt();
			if (fontSize > 0)
				nppGUI._fontDlgSize = static_cast<WORD>(fontSize);
			return;
		}

		case IDC_COMBO_DIALOG_FONT_NAME:
		{
			if (notification == RcNotifySelChange)
				nppGUI._fontDlgName = comboBox(IDC_COMBO_DIALOG_FONT_NAME)->currentText().toStdWString();
			return;
		}

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Toolbar

unsigned int ToolbarSubDlg::getToolbarIconSetMsg(int* iconSetID)
{
	struct { int _id; unsigned int _msg; } const sets[] = {
		{ IDC_RADIO_STANDARD, NPPM_INTERNAL_TOOLBARSTANDARD },
		{ IDC_RADIO_SMALLICON, NPPM_INTERNAL_TOOLBARREDUCE },
		{ IDC_RADIO_BIGICON, NPPM_INTERNAL_TOOLBARENLARGE },
		{ IDC_RADIO_SMALLICON2, NPPM_INTERNAL_TOOLBARREDUCESET2 },
		{ IDC_RADIO_BIGICON2, NPPM_INTERNAL_TOOLBARENLARGESET2 },
	};
	for (const auto& set : sets)
	{
		if (isCheckedOrNot(set._id))
		{
			if (iconSetID)
				*iconSetID = set._id;
			return set._msg;
		}
	}
	return NPPM_INTERNAL_TOOLBARSTANDARD;
}

void ToolbarSubDlg::enableIconColorPicker(bool enable)
{
	if (!_pIconColorPicker)
		return;
	_pIconColorPicker->QWidget::setEnabled(enable);
	if (enable)
	{
		const auto& tbIconInfo = NppDarkMode::getToolbarIconInfo(NppDarkMode::isEnabled());
		_pIconColorPicker->setColour(tbIconInfo._tbCustomColor);
	}
	else
	{
		const QColor bg = palette().color(QPalette::Window);
		_pIconColorPicker->setColour(RGB(bg.red(), bg.green(), bg.blue()));
	}
}

void ToolbarSubDlg::changeToolbarColorableState()
{
	// upstream NPPM_INTERNAL_CHANGETOOLBARCOLORABLESTATE
	bool enableColor = !isCheckedOrNot(IDC_RADIO_STANDARD);
	bool enableCustom = isCheckedOrNot(IDC_RADIO_CUSTOMCOLOR);

	for (int id : { IDC_RADIO_COMPLETE, IDC_RADIO_PARTIAL, IDC_RADIO_RED, IDC_RADIO_GREEN, IDC_RADIO_BLUE, IDC_RADIO_PURPLE,
		IDC_RADIO_CYAN, IDC_RADIO_OLIVE, IDC_RADIO_YELLOW, IDC_RADIO_DEFAULTCOLOR, IDC_RADIO_ACCENTCOLOR, IDC_RADIO_CUSTOMCOLOR,
		IDC_TOOLBAR_GB_COLORCHOICE, IDC_TOOLBAR_GB_COLORIZATION, IDD_ACCENT_TIP_STATIC })
		enableItem(id, enableColor);

	enableIconColorPicker(enableColor && enableCustom);
}

void ToolbarSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();
	auto& nppGUITbInfo = nppGUI._tbIconInfo;

	setChecked(IDC_CHECK_HIDE, !nppGUI._toolbarShow);

	int ID2Check = 0;
	switch (nppGUITbInfo._tbIconSet)
	{
		case TB_SMALL: ID2Check = IDC_RADIO_SMALLICON; break;
		case TB_LARGE: ID2Check = IDC_RADIO_BIGICON; break;
		case TB_SMALL2: ID2Check = IDC_RADIO_SMALLICON2; break;
		case TB_LARGE2: ID2Check = IDC_RADIO_BIGICON2; break;
		case TB_STANDARD:
		default: ID2Check = IDC_RADIO_STANDARD;
	}
	setChecked(ID2Check);

	switch (nppGUITbInfo._tbColor)
	{
		case FluentColor::custom: ID2Check = IDC_RADIO_CUSTOMCOLOR; break;
		case FluentColor::accent: ID2Check = IDC_RADIO_ACCENTCOLOR; break;
		case FluentColor::red: ID2Check = IDC_RADIO_RED; break;
		case FluentColor::green: ID2Check = IDC_RADIO_GREEN; break;
		case FluentColor::blue: ID2Check = IDC_RADIO_BLUE; break;
		case FluentColor::purple: ID2Check = IDC_RADIO_PURPLE; break;
		case FluentColor::cyan: ID2Check = IDC_RADIO_CYAN; break;
		case FluentColor::olive: ID2Check = IDC_RADIO_OLIVE; break;
		case FluentColor::yellow: ID2Check = IDC_RADIO_YELLOW; break;
		case FluentColor::defaultColor:
		default: ID2Check = IDC_RADIO_DEFAULTCOLOR;
	}
	setChecked(ID2Check);

	// NPLX: NotePadLX has one icon set (line icons drawn for the theme), in the upstream sizes: the "Fluent UI" sets
	// are the NotePadLX icons, "Standard icons" the same icons without colorization
	setItemText(IDC_RADIO_SMALLICON, L"Small icons");
	setItemText(IDC_RADIO_BIGICON, L"Large icons");
	setItemText(IDC_RADIO_SMALLICON2, L"Small icons, bold");
	setItemText(IDC_RADIO_BIGICON2, L"Large icons, bold");
	setItemText(IDC_RADIO_STANDARD, L"Small icons, theme colours");

	NativeLangSpeaker* pNativeSpeaker = nppParam.getNativeLangSpeaker();
	wstring accentTip = pNativeSpeaker->getLocalizedStrFromID("toolbar-accent-tip", L"This option makes your toolbar icons follow the accent color of your desktop.");
	setTip(IDD_ACCENT_TIP_STATIC, accentTip);
	setItemText(IDC_RADIO_ACCENTCOLOR, L"Accent color");

	setChecked(nppGUITbInfo._tbUseMono ? IDC_RADIO_COMPLETE : IDC_RADIO_PARTIAL);

	// upstream: the colour picker left of the IDC_STATIC placeholder (310,127)
	_pIconColorPicker = new ColourPicker(this);
	_pIconColorPicker->disableRightClick();
	const QRect anchor = mapDialogRect(310, 127, 1, 8);
	_pIconColorPicker->setGeometry(anchor.left() - 5 - 25, anchor.center().y() - 12, 25, 25);
	_pIconColorPicker->setColour(nppGUITbInfo._tbCustomColor);
	connect(_pIconColorPicker, &ColourPicker::colourPicked, this, [this]() {
		// upstream CPN_COLOURPICKED
		NppGUI& gui = NppParameters::getInstance().getNppGUI();
		COLORREF c = _pIconColorPicker->getColour();
		gui._tbIconInfo._tbCustomColor = c;
		NppDarkMode::setToolbarFluentCustomColor(c);
		sendToNpp(getToolbarIconSetMsg(nullptr), TRUE, 0);
	});

	changeToolbarColorableState();
	if (nppGUITbInfo._tbIconSet != TB_STANDARD && ID2Check != IDC_RADIO_CUSTOMCOLOR)
		enableIconColorPicker(false);
}

void ToolbarSubDlg::onCommand(int id, int /*notification*/)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	auto setIconSet = [this](unsigned int message, int state2Set) {
		sendToNpp(message, 0, 0);
		NppDarkMode::setToolbarIconSet(state2Set, NppDarkMode::isEnabled());
		changeToolbarColorableState();
	};

	auto setColor = [this, &nppGUI](FluentColor c, bool enablePicker) {
		NppDarkMode::setToolbarFluentColor(c);
		nppGUI._tbIconInfo._tbColor = c;
		sendToNpp(getToolbarIconSetMsg(nullptr), TRUE, 0);
		enableIconColorPicker(enablePicker);
	};

	switch (id)
	{
		case IDC_CHECK_HIDE:
		{
			bool isChecked = isCheckedOrNot(IDC_CHECK_HIDE);
			sendToNpp(NPPM_HIDETOOLBAR, 0, isChecked ? TRUE : FALSE);
			return;
		}

		case IDC_RADIO_SMALLICON: setIconSet(NPPM_INTERNAL_TOOLBARREDUCE, 0); return;
		case IDC_RADIO_BIGICON: setIconSet(NPPM_INTERNAL_TOOLBARENLARGE, 1); return;
		case IDC_RADIO_SMALLICON2: setIconSet(NPPM_INTERNAL_TOOLBARREDUCESET2, 2); return;
		case IDC_RADIO_BIGICON2: setIconSet(NPPM_INTERNAL_TOOLBARENLARGESET2, 3); return;
		case IDC_RADIO_STANDARD: setIconSet(NPPM_INTERNAL_TOOLBARSTANDARD, 4); return;

		case IDC_RADIO_CUSTOMCOLOR: setColor(FluentColor::custom, true); return;
		case IDC_RADIO_ACCENTCOLOR: setColor(FluentColor::accent, false); return;
		case IDC_RADIO_RED: setColor(FluentColor::red, false); return;
		case IDC_RADIO_GREEN: setColor(FluentColor::green, false); return;
		case IDC_RADIO_BLUE: setColor(FluentColor::blue, false); return;
		case IDC_RADIO_PURPLE: setColor(FluentColor::purple, false); return;
		case IDC_RADIO_CYAN: setColor(FluentColor::cyan, false); return;
		case IDC_RADIO_OLIVE: setColor(FluentColor::olive, false); return;
		case IDC_RADIO_YELLOW: setColor(FluentColor::yellow, false); return;
		case IDC_RADIO_DEFAULTCOLOR: setColor(FluentColor::defaultColor, false); return;

		case IDC_RADIO_COMPLETE:
		case IDC_RADIO_PARTIAL:
		{
			const bool useMono = (id == IDC_RADIO_COMPLETE);
			NppDarkMode::setToolbarFluentMonochrome(useMono);
			nppGUI._tbIconInfo._tbUseMono = useMono;
			sendToNpp(getToolbarIconSetMsg(nullptr), TRUE, 0);
			return;
		}

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Tab Bar

void TabbarSubDlg::setTabbarAlternateIcons(bool enable)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	if (!enable)
		nppGUI._tabStatus &= ~TAB_ALTICONS;
	else
		nppGUI._tabStatus |= TAB_ALTICONS;
	setChecked(IDC_CHECK_TAB_ALTICONS, enable);
}

void TabbarSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	int tabBarStatus = nppGUI._tabStatus;
	setChecked(IDC_CHECK_REDUCE, tabBarStatus & TAB_REDUCE);
	setChecked(IDC_CHECK_LOCK, !(tabBarStatus & TAB_DRAGNDROP));
	setChecked(IDC_CHECK_ORANGE, tabBarStatus & TAB_DRAWTOPBAR);
	setChecked(IDC_CHECK_DRAWINACTIVE, tabBarStatus & TAB_DRAWINACTIVETAB);

	bool showCloseButton = tabBarStatus & TAB_CLOSEBUTTON;
	bool enablePinButton = tabBarStatus & TAB_PINBUTTON;
	bool showOnlyPinnedButton = tabBarStatus & TAB_SHOWONLYPINNEDBUTTON;
	bool showButtonOnInactiveTabs = tabBarStatus & TAB_INACTIVETABSHOWBUTTON;
	bool hideTabbar = tabBarStatus & TAB_HIDE;

	setChecked(IDC_CHECK_ENABLETABCLOSE, showCloseButton);
	setChecked(IDC_CHECK_ENABLETABPIN, enablePinButton);
	setChecked(IDC_CHECK_SHOWONLYPINNEDBUTTON, showOnlyPinnedButton);
	setChecked(IDC_CHECK_INACTTABDRAWBUTTON, showButtonOnInactiveTabs);

	if (!(showCloseButton || enablePinButton))
	{
		nppGUI._tabStatus &= ~TAB_INACTIVETABSHOWBUTTON;
		setChecked(IDC_CHECK_INACTTABDRAWBUTTON, false);
		enableItem(IDC_CHECK_INACTTABDRAWBUTTON, false);
	}

	if (!enablePinButton)
		enableItem(IDC_CHECK_SHOWONLYPINNEDBUTTON, false);

	setChecked(IDC_CHECK_DBCLICK2CLOSE, tabBarStatus & TAB_DBCLK2CLOSE);
	setChecked(IDC_CHECK_TAB_VERTICAL, tabBarStatus & TAB_VERTICAL);
	setChecked(IDC_CHECK_TAB_MULTILINE, tabBarStatus & TAB_MULTILINE);
	setChecked(IDC_CHECK_TAB_LAST_EXIT, tabBarStatus & TAB_QUITONEMPTY);
	setChecked(IDC_CHECK_TAB_ALTICONS, tabBarStatus & TAB_ALTICONS);
	setChecked(IDC_CHECK_TAB_HIDE, tabBarStatus & TAB_HIDE);

	if (hideTabbar)
		onCommand(IDC_CHECK_TAB_HIDE, RcNotifyClicked);

	setItemInt(IDC_EDIT_TABCOMPACTLABELLEN, static_cast<int>(nppParam.getNbTabCompactLabelLen()));

	NativeLangSpeaker* pNativeSpeaker = nppParam.getNativeLangSpeaker();
	wstring tabCompactLabelLenTip = pNativeSpeaker->getLocalizedStrFromID("tabbar-tabcompactlabellen-tip",
		L"Limits the visible length of long tab names. Enter to apply the given value. Value range: 1-257 characters (0 disables the truncation).");
	setTip(IDC_TABCOMPACTLABELLEN_TIP_STATIC, tabCompactLabelLenTip);

	// NPLX: pinned tabs are not ported yet
	for (int ctrl : { IDC_CHECK_ENABLETABPIN, IDC_CHECK_SHOWONLYPINNEDBUTTON })
	{
		enableItem(ctrl, false);
		dlgItem(ctrl)->setToolTip(QStringLiteral("Pinned tabs are not ported to NotePadLX yet."));
	}

	// NPLX: Qt tab bars have one row; the tabs scroll instead (upstream TCS_MULTILINE)
	enableItem(IDC_CHECK_TAB_MULTILINE, false);
	dlgItem(IDC_CHECK_TAB_MULTILINE)->setToolTip(QStringLiteral("Not available in NotePadLX: the tabs scroll."));
}

void TabbarSubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	auto setStatus = [&nppGUI](int flag, bool isChecked) {
		if (isChecked)
			nppGUI._tabStatus |= flag;
		else
			nppGUI._tabStatus &= ~flag;
	};

	if (id == IDC_EDIT_TABCOMPACTLABELLEN)
	{
		if (notification != RcNotifyKillFocus)
			return;

		const wstring str = getItemText(IDC_EDIT_TABCOMPACTLABELLEN);
		if (str.empty())
		{
			// user removed the value completely, the compacting is considered as disabled
			nppParam.setNbTabCompactLabelLen(0);
			setItemInt(IDC_EDIT_TABCOMPACTLABELLEN, 0);
			sendToNpp(NPPM_INTERNAL_SETTING_TABCOMPACTLABELLEN);
			return;
		}

		int iSize = 0;
		try
		{
			iSize = stoi(str);
		}
		catch (...)
		{
			iSize = -1;
		}

		if ((iSize >= 0) && (iSize == static_cast<int>(nppParam.getNbTabCompactLabelLen())))
			return; // nothing changed

		bool change = false;
		if (iSize < 0)
		{
			iSize = 0;
			change = true;
		}
		else if (iSize > NB_MAX_TAB_COMPACT_LABEL_LEN)
		{
			iSize = NB_MAX_TAB_COMPACT_LABEL_LEN;
			change = true;
		}
		if (change)
			setItemInt(IDC_EDIT_TABCOMPACTLABELLEN, iSize);

		nppParam.setNbTabCompactLabelLen(iSize);
		sendToNpp(NPPM_INTERNAL_SETTING_TABCOMPACTLABELLEN);
		return;
	}

	switch (id)
	{
		case IDC_CHECK_TAB_HIDE:
		{
			bool toBeHidden = isCheckedOrNot(IDC_CHECK_TAB_HIDE);
			setStatus(TAB_HIDE, toBeHidden);

			for (int ctrl : { IDC_CHECK_TAB_VERTICAL, IDC_CHECK_REDUCE, IDC_CHECK_LOCK, IDC_CHECK_ORANGE,
				IDC_CHECK_DRAWINACTIVE, IDC_CHECK_ENABLETABCLOSE, IDC_CHECK_INACTTABDRAWBUTTON, IDC_CHECK_DBCLICK2CLOSE, IDC_CHECK_TAB_LAST_EXIT, IDC_CHECK_TAB_ALTICONS })
				enableItem(ctrl, !toBeHidden);
			if (!toBeHidden)
			{
				const bool showCloseButton = isCheckedOrNot(IDC_CHECK_ENABLETABCLOSE);
				const bool enablePinButton = isCheckedOrNot(IDC_CHECK_ENABLETABPIN);
				enableItem(IDC_CHECK_INACTTABDRAWBUTTON, showCloseButton || enablePinButton);
			}

			if (!_isUpdatingControls)
			{
				// upstream WM_SIZE: the main window shows or hides the tab bars
				sendToNpp(NPPM_INTERNAL_REFRESHTABBAR);

				// At startup, if "-notabbar" or "asNotepad.xml" is used, and tab bar was not hidden in the previous session,
				// we force tab bar visible in the next session by setting _forceTabbarVisible to true.
				// However, if "Hide tab bar" option is changed manually by user, then we don't force tab bar visible for the next session,
				// and apply user's choice instead.
				nppGUI._forceTabbarVisible = false;
			}
			return;
		}

		case IDC_CHECK_TAB_VERTICAL:
			setStatus(TAB_VERTICAL, isCheckedOrNot(IDC_CHECK_TAB_VERTICAL));
			sendToNpp(NPPM_INTERNAL_VERTICALTABBAR);
			return;

		case IDC_CHECK_TAB_MULTILINE:
			setStatus(TAB_MULTILINE, isCheckedOrNot(IDC_CHECK_TAB_MULTILINE));
			sendToNpp(NPPM_INTERNAL_MULTILINETABBAR);
			return;

		case IDC_CHECK_TAB_LAST_EXIT:
			setStatus(TAB_QUITONEMPTY, isCheckedOrNot(IDC_CHECK_TAB_LAST_EXIT));
			return;

		case IDC_CHECK_TAB_ALTICONS:
		{
			const bool isChecked = isCheckedOrNot(IDC_CHECK_TAB_ALTICONS);
			setStatus(TAB_ALTICONS, isChecked);
			const bool isBtnCmd = true;
			sendToNpp(NPPM_INTERNAL_CHANGETABBARICONSET, static_cast<uptr_t>(isBtnCmd), isChecked ? 1 : (nppGUI._darkmode._isEnabled ? 2 : 0));
			NppDarkMode::setTabIconSet(isChecked, NppDarkMode::isEnabled());
			return;
		}

		case IDC_CHECK_REDUCE:
			setStatus(TAB_REDUCE, isCheckedOrNot(IDC_CHECK_REDUCE));
			sendToNpp(NPPM_INTERNAL_REDUCETABBAR);
			return;

		case IDC_CHECK_LOCK:
			setStatus(TAB_DRAGNDROP, !isCheckedOrNot(IDC_CHECK_LOCK));
			sendToNpp(NPPM_INTERNAL_REFRESHTABBAR); // NPLX: QTabBar::setMovable
			return;

		case IDC_CHECK_ORANGE:
			setStatus(TAB_DRAWTOPBAR, isCheckedOrNot(IDC_CHECK_ORANGE));
			sendToNpp(NPPM_INTERNAL_DRAWTABTOPBAR);
			return;

		case IDC_CHECK_DRAWINACTIVE:
			setStatus(TAB_DRAWINACTIVETAB, isCheckedOrNot(IDC_CHECK_DRAWINACTIVE));
			sendToNpp(NPPM_INTERNAL_DRAWINACTIVETAB);
			return;

		case IDC_CHECK_ENABLETABCLOSE:
		case IDC_CHECK_ENABLETABPIN:
		{
			if (id == IDC_CHECK_ENABLETABCLOSE)
				setStatus(TAB_CLOSEBUTTON, isCheckedOrNot(IDC_CHECK_ENABLETABCLOSE));
			else
				setStatus(TAB_PINBUTTON, isCheckedOrNot(IDC_CHECK_ENABLETABPIN));

			sendToNpp(id == IDC_CHECK_ENABLETABCLOSE ? NPPM_INTERNAL_DRAWTABBARCLOSEBUTTON : NPPM_INTERNAL_DRAWTABBARPINBUTTON);

			bool showCloseButton = isCheckedOrNot(IDC_CHECK_ENABLETABCLOSE);
			bool enablePinButton = isCheckedOrNot(IDC_CHECK_ENABLETABPIN);

			if (!(showCloseButton || enablePinButton))
			{
				nppGUI._tabStatus &= ~TAB_INACTIVETABSHOWBUTTON;
				setChecked(IDC_CHECK_INACTTABDRAWBUTTON, false);
				sendToNpp(NPPM_INTERNAL_REFRESHTABBAR);
			}
			enableItem(IDC_CHECK_INACTTABDRAWBUTTON, showCloseButton || enablePinButton);

			return;
		}

		case IDC_CHECK_INACTTABDRAWBUTTON:
			setStatus(TAB_INACTIVETABSHOWBUTTON, isCheckedOrNot(IDC_CHECK_INACTTABDRAWBUTTON));
			sendToNpp(NPPM_INTERNAL_REFRESHTABBAR);
			return;

		case IDC_CHECK_SHOWONLYPINNEDBUTTON:
			setStatus(TAB_SHOWONLYPINNEDBUTTON, isCheckedOrNot(IDC_CHECK_SHOWONLYPINNEDBUTTON));
			sendToNpp(NPPM_INTERNAL_REFRESHTABBAR);
			return;

		case IDC_CHECK_DBCLICK2CLOSE:
			setStatus(TAB_DBCLK2CLOSE, isCheckedOrNot(IDC_CHECK_DBCLICK2CLOSE));
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Editing 1

void EditingSubDlg::initScintParam()
{
	NppParameters& nppParam = NppParameters::getInstance();
	const ScintillaViewParams& svp = nppParam.getSVP();

	int id = 0;
	switch (svp._lineWrapMethod)
	{
		case LINEWRAP_ALIGNED: id = IDC_RADIO_LWALIGN; break;
		case LINEWRAP_INDENT: id = IDC_RADIO_LWINDENT; break;
		default: id = IDC_RADIO_LWDEF; // LINEWRAP_DEFAULT
	}
	setChecked(id);

	setChecked(IDC_CHECK_SMOOTHFONT, svp._doSmoothFont);

	int lineHilite = 0;
	switch (svp._currentLineHiliteMode)
	{
		case LINEHILITE_NONE: lineHilite = IDC_RADIO_CLM_NONE; break;
		case LINEHILITE_FRAME: lineHilite = IDC_RADIO_CLM_FRAME; break;
		default: lineHilite = IDC_RADIO_CLM_HILITE; // LINEHILITE_HILITE
	}
	setChecked(lineHilite);
	enableItem(IDC_CARETLINEFRAME_WIDTH_SLIDER, svp._currentLineHiliteMode == LINEHILITE_FRAME);
	enableItem(IDC_CARETLINEFRAME_WIDTH_STATIC, svp._currentLineHiliteMode == LINEHILITE_FRAME);
	enableItem(IDC_CARETLINEFRAME_WIDTH_DISPLAY, svp._currentLineHiliteMode == LINEHILITE_FRAME);

	setChecked(IDC_CHECK_VIRTUALSPACE, svp._virtualSpace);
	setChecked(IDC_CHECK_SCROLLBEYONDLASTLINE, svp._scrollBeyondLastLine);
	setChecked(IDC_CHECK_RIGHTCLICKKEEPSSELECTION, svp._rightClickKeepsSelection);
	setChecked(IDC_CHECK_SELECTEDTEXTSINGLECOLOR, svp._selectedTextForegroundSingleColor);
	setChecked(IDC_CHECK_DISABLEADVANCEDSCROLL, svp._disableAdvancedScrolling);
	setChecked(IDC_CHECK_LINECUTCOPYWITHOUTSELECTION, svp._lineCopyCutWithoutSelection);
	setChecked(IDC_CHECK_DISABLESELECTEDTEXTDRAGDROP, svp._disableSelectedTextDragDrop);
}

void EditingSubDlg::changeLineHiliteMode(bool enableSlider)
{
	enableItem(IDC_CARETLINEFRAME_WIDTH_SLIDER, enableSlider);
	enableItem(IDC_CARETLINEFRAME_WIDTH_STATIC, enableSlider);
	enableItem(IDC_CARETLINEFRAME_WIDTH_DISPLAY, enableSlider);
	sendToNpp(NPPM_INTERNAL_HILITECURRENTLINE);
}

void EditingSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();
	const ScintillaViewParams& svp = nppParam.getSVP();

	QComboBox* widthCombo = comboBox(IDC_WIDTH_COMBO);
	for (const wchar_t* width : { L"0", L"1", L"2", L"3", L"Block", L"Block After" })
		widthCombo->addItem(QString::fromWCharArray(width));
	widthCombo->setCurrentIndex(nppGUI._caretWidth);

	setChecked(IDC_CHECK_FOLDINGTOGGLE, nppGUI._enableFoldCmdToggable);

	int blinkRate = (nppGUI._caretBlinkRate == 0) ? BLINKRATE_SLOWEST : nppGUI._caretBlinkRate;
	setSliderRange(this, IDC_CARETBLINKRATE_SLIDER, BLINKRATE_FASTEST, BLINKRATE_SLOWEST, BLINKRATE_INTERVAL, blinkRate);

	setSliderRange(this, IDC_CARETLINEFRAME_WIDTH_SLIDER, CARETLINEFRAME_SMALLEST, CARETLINEFRAME_LARGEST, CARETLINEFRAME_INTERVAL, svp._currentLineFrameWidth);
	setItemInt(IDC_CARETLINEFRAME_WIDTH_DISPLAY, svp._currentLineFrameWidth);

	initScintParam();
}

void EditingSubDlg::onSliderChanged(int id, int value)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();
	auto& svp = const_cast<ScintillaViewParams&>(nppParam.getSVP());

	if (id == IDC_CARETBLINKRATE_SLIDER)
	{
		int blinkRate = value;
		if (blinkRate == BLINKRATE_SLOWEST)
			blinkRate = 0;
		nppGUI._caretBlinkRate = blinkRate;
		sendToNpp(NPPM_INTERNAL_SETCARETBLINKRATE);
	}
	else if (id == IDC_CARETLINEFRAME_WIDTH_SLIDER)
	{
		svp._currentLineFrameWidth = static_cast<unsigned char>(value);
		setItemInt(IDC_CARETLINEFRAME_WIDTH_DISPLAY, svp._currentLineFrameWidth);
		sendToNpp(NPPM_INTERNAL_CARETLINEFRAME, 0, svp._currentLineFrameWidth);
	}
}

void EditingSubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();
	auto& svp = const_cast<ScintillaViewParams&>(nppParam.getSVP());

	switch (id)
	{
		case IDC_CHECK_SMOOTHFONT:
			svp._doSmoothFont = isCheckedOrNot(IDC_CHECK_SMOOTHFONT);
			sendToNpp(NPPM_SETSMOOTHFONT, 0, svp._doSmoothFont);
			return;

		case IDC_RADIO_CLM_NONE:
			svp._currentLineHiliteMode = LINEHILITE_NONE;
			changeLineHiliteMode(false);
			return;

		case IDC_RADIO_CLM_HILITE:
			svp._currentLineHiliteMode = LINEHILITE_HILITE;
			changeLineHiliteMode(false);
			return;

		case IDC_RADIO_CLM_FRAME:
			svp._currentLineHiliteMode = LINEHILITE_FRAME;
			changeLineHiliteMode(true);
			return;

		case IDC_CHECK_VIRTUALSPACE:
			svp._virtualSpace = isCheckedOrNot(IDC_CHECK_VIRTUALSPACE);
			sendToNpp(NPPM_INTERNAL_VIRTUALSPACE);
			return;

		case IDC_CHECK_SCROLLBEYONDLASTLINE:
			svp._scrollBeyondLastLine = isCheckedOrNot(IDC_CHECK_SCROLLBEYONDLASTLINE);
			sendToNpp(NPPM_INTERNAL_SCROLLBEYONDLASTLINE);
			return;

		case IDC_CHECK_LINECUTCOPYWITHOUTSELECTION:
			svp._lineCopyCutWithoutSelection = isCheckedOrNot(IDC_CHECK_LINECUTCOPYWITHOUTSELECTION);
			sendToNpp(NPPM_INTERNAL_LINECUTCOPYWITHOUTSELECTION);
			return;

		case IDC_CHECK_DISABLESELECTEDTEXTDRAGDROP:
			svp._disableSelectedTextDragDrop = isCheckedOrNot(IDC_CHECK_DISABLESELECTEDTEXTDRAGDROP);
			sendToNpp(NPPM_INTERNAL_DISABLESELECTEDTEXTDRAGDROP);
			return;

		case IDC_CHECK_RIGHTCLICKKEEPSSELECTION:
			svp._rightClickKeepsSelection = isCheckedOrNot(IDC_CHECK_RIGHTCLICKKEEPSSELECTION);
			return;

		case IDC_CHECK_SELECTEDTEXTSINGLECOLOR:
			svp._selectedTextForegroundSingleColor = isCheckedOrNot(IDC_CHECK_SELECTEDTEXTSINGLECOLOR);
			sendToNpp(NPPM_INTERNAL_CHANGESELECTTEXTFORGROUND);
			return;

		case IDC_CHECK_DISABLEADVANCEDSCROLL:
			svp._disableAdvancedScrolling = isCheckedOrNot(IDC_CHECK_DISABLEADVANCEDSCROLL);
			return;

		case IDC_CHECK_FOLDINGTOGGLE:
			nppGUI._enableFoldCmdToggable = isCheckedOrNot(IDC_CHECK_FOLDINGTOGGLE);
			return;

		case IDC_RADIO_LWDEF:
			svp._lineWrapMethod = LINEWRAP_DEFAULT;
			sendToNpp(NPPM_INTERNAL_LWDEF);
			return;

		case IDC_RADIO_LWALIGN:
			svp._lineWrapMethod = LINEWRAP_ALIGNED;
			sendToNpp(NPPM_INTERNAL_LWALIGN);
			return;

		case IDC_RADIO_LWINDENT:
			svp._lineWrapMethod = LINEWRAP_INDENT;
			sendToNpp(NPPM_INTERNAL_LWINDENT);
			return;

		case IDC_WIDTH_COMBO:
			if (notification == RcNotifySelChange)
			{
				nppGUI._caretWidth = comboBox(IDC_WIDTH_COMBO)->currentIndex();
				sendToNpp(NPPM_INTERNAL_SETCARETWIDTH);
			}
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Editing 2

void Editing2SubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	const ScintillaViewParams& svp = nppParam.getSVP();

	// default =>  (svp._eolMode == svp.roundedRectangleText)
	bool checkDefaultCRLF = true;
	bool checkPlainTextCRLF = false;
	bool checkWithColorCRLF = false;

	if (svp._eolMode == svp.plainText)
	{
		checkDefaultCRLF = false;
		checkPlainTextCRLF = true;
		checkWithColorCRLF = false;
	}
	else if (svp._eolMode == svp.plainTextCustomColor)
	{
		checkDefaultCRLF = false;
		checkPlainTextCRLF = true;
		checkWithColorCRLF = true;
	}
	else if (svp._eolMode == svp.roundedRectangleTextCustomColor)
	{
		checkDefaultCRLF = true;
		checkPlainTextCRLF = false;
		checkWithColorCRLF = true;
	}

	setChecked(IDC_RADIO_ROUNDCORNER_CRLF, checkDefaultCRLF);
	setChecked(IDC_RADIO_PLAINTEXT_CRLF, checkPlainTextCRLF);
	setChecked(IDC_CHECK_WITHCUSTOMCOLOR_CRLF, checkWithColorCRLF);

	setChecked(IDC_CHECK_MULTISELECTION, svp._multiSelection);
	setChecked(IDC_CHECK_COLUMN2MULTIEDITING, svp._columnSel2MultiEdit);
	enableItem(IDC_CHECK_COLUMN2MULTIEDITING, svp._multiSelection);

	NativeLangSpeaker* pNativeSpeaker = nppParam.getNativeLangSpeaker();
	setTip(IDC_BUTTON_LAUNCHSTYLECONF_CRLF, pNativeSpeaker->getLocalizedStrFromID("eol-custom-color-tip", L"Go to Style Configurator to change the default EOL custom color (\"EOL custom color\")."));

	const bool isNpcModeAbbrv = svp._npcMode == svp.abbreviation;
	setChecked(IDC_RADIO_NPC_ABBREVIATION, isNpcModeAbbrv);
	setChecked(IDC_RADIO_NPC_CODEPOINT, !isNpcModeAbbrv);
	setChecked(IDC_CHECK_NPC_COLOR, svp._npcCustomColor);
	setChecked(IDC_CHECK_NPC_INCLUDECCUNIEOL, svp._npcIncludeCcUniEol);
	setChecked(IDC_CHECK_NPC_NOINPUTC0, svp._npcNoInputC0);

	setTip(IDC_BUTTON_NPC_NOTE, pNativeSpeaker->getLocalizedStrFromID("npcNote-tip",
		L"Representation of selected \"non-ASCII\" whitespace and non-printing (control) characters.\n\n"
		L"NOTE:\n"
		L"Using representation will disable character effects on text.\n\n"
		L"For the full list of selected whitespace and non-printing characters check User Manual.\n\n"
		L"Click on this button to open website with User Manual."));
	setTip(IDC_RADIO_NPC_ABBREVIATION, pNativeSpeaker->getLocalizedStrFromID("npcAbbreviation-tip",
		L"Abbreviation : name\n"
		L"NBSP : no-break space\n"
		L"ZWSP : zero-width space\n"
		L"ZWNBSP : zero-width no-break space\n\n"
		L"For the full list check User Manual.\n"
		L"Click on \"?\" button on right to open website with User Manual."));
	setTip(IDC_RADIO_NPC_CODEPOINT, pNativeSpeaker->getLocalizedStrFromID("npcCodepoint-tip",
		L"Codepoint : name\n"
		L"U+00A0 : no-break space\n"
		L"U+200B : zero-width space\n"
		L"U+FEFF : zero-width no-break space\n\n"
		L"For the full list check User Manual.\n"
		L"Click on \"?\" button on right to open website with User Manual."));
	setTip(IDC_BUTTON_NPC_LAUNCHSTYLECONF, pNativeSpeaker->getLocalizedStrFromID("npcCustomColor-tip",
		L"Go to Style Configurator to change the default custom color for selected whitespace and non-printing characters (\"Non-printing characters custom color\")."));
	setTip(IDC_CHECK_NPC_INCLUDECCUNIEOL, pNativeSpeaker->getLocalizedStrFromID("npcIncludeCcUniEol-tip",
		L"Apply non-printing characters appearance settings to C0, C1 control and Unicode EOL (next line, line separator and paragraph separator) characters."));
}

void Editing2SubDlg::onCommand(int id, int /*notification*/)
{
	NppParameters& nppParam = NppParameters::getInstance();
	ScintillaViewParams& svp = const_cast<ScintillaViewParams&>(nppParam.getSVP());

	switch (id)
	{
		case IDC_CHECK_MULTISELECTION:
		{
			svp._multiSelection = isCheckedOrNot(IDC_CHECK_MULTISELECTION);
			if (!svp._multiSelection)
			{
				setChecked(IDC_CHECK_COLUMN2MULTIEDITING, false);
				svp._columnSel2MultiEdit = false;
			}
			enableItem(IDC_CHECK_COLUMN2MULTIEDITING, svp._multiSelection);
			sendToNpp(NPPM_INTERNAL_SETMULTISELECTION);
			return;
		}

		case IDC_CHECK_COLUMN2MULTIEDITING:
			svp._columnSel2MultiEdit = isCheckedOrNot(IDC_CHECK_COLUMN2MULTIEDITING);
			return;

		case IDC_RADIO_ROUNDCORNER_CRLF:
		case IDC_RADIO_PLAINTEXT_CRLF:
		case IDC_CHECK_WITHCUSTOMCOLOR_CRLF:
		{
			bool doCustomColor = isCheckedOrNot(IDC_CHECK_WITHCUSTOMCOLOR_CRLF);

			if (id == IDC_RADIO_ROUNDCORNER_CRLF)
			{
				svp._eolMode = doCustomColor ? svp.roundedRectangleTextCustomColor : svp.roundedRectangleText;
			}
			else if (id == IDC_RADIO_PLAINTEXT_CRLF)
			{
				svp._eolMode = doCustomColor ? svp.plainTextCustomColor : svp.plainText;
			}
			else // IDC_CHECK_WITHCUSTOMCOLOR_CRLF
			{
				if (isCheckedOrNot(IDC_RADIO_ROUNDCORNER_CRLF))
					svp._eolMode = doCustomColor ? svp.roundedRectangleTextCustomColor : svp.roundedRectangleText;
				else // IDC_RADIO_PLAINTEXT_CRLF
					svp._eolMode = doCustomColor ? svp.plainTextCustomColor : svp.plainText;
			}
			sendToNpp(NPPM_INTERNAL_CRLFFORMCHANGED);
			return;
		}

		case IDC_BUTTON_LAUNCHSTYLECONF_CRLF:
			sendToNpp(NPPM_INTERNAL_CRLFLAUNCHSTYLECONF);
			return;

		case IDC_RADIO_NPC_ABBREVIATION:
		case IDC_RADIO_NPC_CODEPOINT:
		{
			svp._npcMode = (id == IDC_RADIO_NPC_CODEPOINT) ? svp.codepoint : svp.abbreviation;
			sendToNpp(NPPM_INTERNAL_SETNPC);
			return;
		}

		case IDC_BUTTON_NPC_NOTE:
			QDesktopServices::openUrl(QUrl(QStringLiteral("https://npp-user-manual.org/docs/views/#show-symbol")));
			return;

		case IDC_CHECK_NPC_COLOR:
			svp._npcCustomColor = isCheckedOrNot(IDC_CHECK_NPC_COLOR);
			sendToNpp(NPPM_INTERNAL_NPCFORMCHANGED);
			return;

		case IDC_BUTTON_NPC_LAUNCHSTYLECONF:
			sendToNpp(NPPM_INTERNAL_NPCLAUNCHSTYLECONF);
			return;

		case IDC_CHECK_NPC_INCLUDECCUNIEOL:
			svp._npcIncludeCcUniEol = isCheckedOrNot(IDC_CHECK_NPC_INCLUDECCUNIEOL);
			sendToNpp(NPPM_INTERNAL_SETNPC, IDC_CHECK_NPC_INCLUDECCUNIEOL, 0);
			return;

		case IDC_CHECK_NPC_NOINPUTC0:
			svp._npcNoInputC0 = isCheckedOrNot(IDC_CHECK_NPC_NOINPUTC0);
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Dark Mode

void DarkModeSubDlg::enableCustomizedColorCtrls(bool doEnable)
{
	const NppDarkMode::Colors& colors = NppParameters::getInstance().getNppGUI()._darkmode._customColors;
	const QColor bg = palette().color(QPalette::Window);
	for (auto& [picker, member] : _colorPickers)
	{
		picker->QWidget::setEnabled(doEnable);
		picker->setColour(doEnable ? colors.*member : RGB(bg.red(), bg.green(), bg.blue()));
	}
	enableItem(IDD_CUSTOMIZED_RESET_BUTTON, doEnable);
	for (int id = IDD_CUSTOMIZED_COLOR1_STATIC; id <= IDD_CUSTOMIZED_COLOR12_STATIC; ++id)
		enableItem(id, doEnable);
}

void DarkModeSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	// NPLX: "Follow Windows" follows the desktop colour scheme (Qt::ColorScheme)
	setItemText(IDC_RADIO_DARKMODE_FOLLOWWINDOWS, L"Follow the desktop");

	const int topControlID = NppDarkMode::isWindowsModeEnabled() ? IDC_RADIO_DARKMODE_FOLLOWWINDOWS : NppDarkMode::isEnabled() ? IDC_RADIO_DARKMODE_DARKMODE : IDC_RADIO_DARKMODE_LIGHTMODE;
	setChecked(topControlID);

	int id = IDC_RADIO_DARKMODE_BLACK;
	switch (nppGUI._darkmode._colorTone)
	{
		case NppDarkMode::redTone: id = IDC_RADIO_DARKMODE_RED; break;
		case NppDarkMode::greenTone: id = IDC_RADIO_DARKMODE_GREEN; break;
		case NppDarkMode::blueTone: id = IDC_RADIO_DARKMODE_BLUE; break;
		case NppDarkMode::purpleTone: id = IDC_RADIO_DARKMODE_PURPLE; break;
		case NppDarkMode::cyanTone: id = IDC_RADIO_DARKMODE_CYAN; break;
		case NppDarkMode::oliveTone: id = IDC_RADIO_DARKMODE_OLIVE; break;
		case NppDarkMode::customizedTone: id = IDC_RADIO_DARKMODE_CUSTOMIZED; break;
		case NppDarkMode::blackTone:
		default: break;
	}
	setChecked(id);

	// upstream: the colour pickers left of their labels (move2CtrlLeft)
	using C = NppDarkMode::Colors;
	const std::pair<int, COLORREF C::*> pickers[] = {
		{ IDD_CUSTOMIZED_COLOR1_STATIC, &C::background },
		{ IDD_CUSTOMIZED_COLOR2_STATIC, &C::hotBackground },
		{ IDD_CUSTOMIZED_COLOR3_STATIC, &C::softerBackground },
		{ IDD_CUSTOMIZED_COLOR4_STATIC, &C::pureBackground },
		{ IDD_CUSTOMIZED_COLOR5_STATIC, &C::errorBackground },
		{ IDD_CUSTOMIZED_COLOR6_STATIC, &C::text },
		{ IDD_CUSTOMIZED_COLOR7_STATIC, &C::darkerText },
		{ IDD_CUSTOMIZED_COLOR8_STATIC, &C::disabledText },
		{ IDD_CUSTOMIZED_COLOR9_STATIC, &C::edge },
		{ IDD_CUSTOMIZED_COLOR10_STATIC, &C::linkText },
		{ IDD_CUSTOMIZED_COLOR11_STATIC, &C::hotEdge },
		{ IDD_CUSTOMIZED_COLOR12_STATIC, &C::disabledEdge },
	};
	for (const auto& [labelID, member] : pickers)
	{
		auto* picker = new ColourPicker(this);
		picker->disableRightClick();
		const QRect label = dlgItem(labelID)->geometry();
		picker->setGeometry(label.left() - 5 - 20, label.center().y() - 10, 20, 20);
		connect(picker, &ColourPicker::colourPicked, this, [this, picker]() { colourPicked(picker); });
		_colorPickers.emplace_back(picker, member);
	}

	// upstream BS_SPLITBUTTON drop-down: reset the custom colours to a tone
	_resetPopupMenu = new QMenu(this);
	struct { int _id; const wchar_t* _name; } const resets[] = {
		{ IDD_CUSTOMIZED_RESET_BUTTON, L"Black" }, { IDD_DROPDOWN_RESET_RED, L"Red" }, { IDD_DROPDOWN_RESET_GREEN, L"Green" },
		{ IDD_DROPDOWN_RESET_BLUE, L"Blue" }, { IDD_DROPDOWN_RESET_PURPLE, L"Purple" }, { IDD_DROPDOWN_RESET_CYAN, L"Cyan" },
		{ IDD_DROPDOWN_RESET_OLIVE, L"Olive" },
	};
	for (const auto& reset : resets)
	{
		const int resetID = reset._id;
		_resetPopupMenu->addAction(QString::fromWCharArray(reset._name), this, [this, resetID]() { onCommand(resetID, RcNotifyClicked); });
	}
	if (auto* resetButton = qobject_cast<QPushButton*>(dlgItem(IDD_CUSTOMIZED_RESET_BUTTON)))
	{
		resetButton->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(resetButton, &QWidget::customContextMenuRequested, this, [this, resetButton](const QPoint& pos) {
			_resetPopupMenu->exec(resetButton->mapToGlobal(pos));
		});
		resetButton->setToolTip(QStringLiteral("Reset to Black (right click: choose the tone)"));
	}

	for (int toneID : { IDC_RADIO_DARKMODE_BLACK, IDC_RADIO_DARKMODE_RED, IDC_RADIO_DARKMODE_GREEN, IDC_RADIO_DARKMODE_BLUE,
		IDC_RADIO_DARKMODE_PURPLE, IDC_RADIO_DARKMODE_CYAN, IDC_RADIO_DARKMODE_OLIVE, IDC_RADIO_DARKMODE_CUSTOMIZED })
		enableItem(toneID, nppGUI._darkmode._isEnabled);

	enableCustomizedColorCtrls(nppGUI._darkmode._isEnabled && id == IDC_RADIO_DARKMODE_CUSTOMIZED);
}

void DarkModeSubDlg::colourPicked(ColourPicker* picker)
{
	// upstream CPN_COLOURPICKED
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	for (auto& [p, member] : _colorPickers)
	{
		if (p == picker)
		{
			nppGUI._darkmode._customColors.*member = picker->getColour();
			nppGUI._darkmode._isEnabled = true;
			applyChange(true, true);
			return;
		}
	}
}

void DarkModeSubDlg::applyChange(bool forceRefresh, bool doEnableCustomizedColorCtrls)
{
	if (!doEnableCustomizedColorCtrls)
		enableCustomizedColorCtrls(false);

	// upstream NppDarkMode::refreshDarkMode(_hSelf, forceRefresh): when the dark mode is switched on or off, the
	// main window also resets the editor theme and the icons (wParam TRUE)
	const bool wasEnabled = NppDarkMode::isEnabled();
	const bool isEnabled = NppParameters::getInstance().getNppGUI()._darkmode._isEnabled;
	if (wasEnabled != isEnabled || forceRefresh)
		sendToNpp(NPPM_INTERNAL_REFRESHDARKMODE, static_cast<uptr_t>(wasEnabled != isEnabled), 0);
}

void DarkModeSubDlg::followSystemColorScheme()
{
	onCommand(IDC_RADIO_DARKMODE_DARKMODE, 0);
}

void DarkModeSubDlg::onCommand(int id, int /*notification*/)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	bool doEnableCustomizedColorCtrls = false;

	switch (id)
	{
		case IDC_RADIO_DARKMODE_FOLLOWWINDOWS:
		case IDC_RADIO_DARKMODE_LIGHTMODE:
		case IDC_RADIO_DARKMODE_DARKMODE:
		{
			const bool isFollowWindows = isCheckedOrNot(IDC_RADIO_DARKMODE_FOLLOWWINDOWS);
			NppDarkMode::setWindowsMode(isFollowWindows);
			const bool systemIsDark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
			const bool enableDarkMode = isCheckedOrNot(IDC_RADIO_DARKMODE_DARKMODE) || (isFollowWindows && systemIsDark);
			nppGUI._darkmode._isEnabled = enableDarkMode;

			for (int toneID : { IDC_RADIO_DARKMODE_BLACK, IDC_RADIO_DARKMODE_RED, IDC_RADIO_DARKMODE_GREEN, IDC_RADIO_DARKMODE_BLUE,
				IDC_RADIO_DARKMODE_PURPLE, IDC_RADIO_DARKMODE_CYAN, IDC_RADIO_DARKMODE_OLIVE, IDC_RADIO_DARKMODE_CUSTOMIZED })
				enableItem(toneID, enableDarkMode);

			doEnableCustomizedColorCtrls = enableDarkMode && nppGUI._darkmode._colorTone == NppDarkMode::customizedTone;
			enableCustomizedColorCtrls(doEnableCustomizedColorCtrls);

			// Set icons set only option (checkbox) on general sub-dialog, the remaining real operations will be done in
			// NPPM_INTERNAL_REFRESHDARKMODE
			if (auto* box = qobject_cast<PreferenceDlg*>(parentWidget()))
				box->setToolIconsSet(enableDarkMode);
			applyChange(false, doEnableCustomizedColorCtrls);
			return;
		}

		case IDC_RADIO_DARKMODE_BLACK:
		case IDC_RADIO_DARKMODE_RED:
		case IDC_RADIO_DARKMODE_GREEN:
		case IDC_RADIO_DARKMODE_BLUE:
		case IDC_RADIO_DARKMODE_PURPLE:
		case IDC_RADIO_DARKMODE_CYAN:
		case IDC_RADIO_DARKMODE_OLIVE:
		case IDC_RADIO_DARKMODE_CUSTOMIZED:
		{
			NppDarkMode::ColorTone tone = NppDarkMode::blackTone;
			switch (id)
			{
				case IDC_RADIO_DARKMODE_RED: tone = NppDarkMode::redTone; break;
				case IDC_RADIO_DARKMODE_GREEN: tone = NppDarkMode::greenTone; break;
				case IDC_RADIO_DARKMODE_BLUE: tone = NppDarkMode::blueTone; break;
				case IDC_RADIO_DARKMODE_PURPLE: tone = NppDarkMode::purpleTone; break;
				case IDC_RADIO_DARKMODE_CYAN: tone = NppDarkMode::cyanTone; break;
				case IDC_RADIO_DARKMODE_OLIVE: tone = NppDarkMode::oliveTone; break;
				case IDC_RADIO_DARKMODE_CUSTOMIZED: tone = NppDarkMode::customizedTone; break;
				default: break;
			}
			if (nppGUI._darkmode._colorTone == tone)
				return;
			nppGUI._darkmode._colorTone = tone;
			doEnableCustomizedColorCtrls = (tone == NppDarkMode::customizedTone);
			break;
		}

		case IDD_CUSTOMIZED_RESET_BUTTON:
		case IDD_DROPDOWN_RESET_RED:
		case IDD_DROPDOWN_RESET_GREEN:
		case IDD_DROPDOWN_RESET_BLUE:
		case IDD_DROPDOWN_RESET_PURPLE:
		case IDD_DROPDOWN_RESET_CYAN:
		case IDD_DROPDOWN_RESET_OLIVE:
		{
			NppDarkMode::ColorTone tone = NppDarkMode::blackTone;
			switch (id)
			{
				case IDD_DROPDOWN_RESET_RED: tone = NppDarkMode::redTone; break;
				case IDD_DROPDOWN_RESET_GREEN: tone = NppDarkMode::greenTone; break;
				case IDD_DROPDOWN_RESET_BLUE: tone = NppDarkMode::blueTone; break;
				case IDD_DROPDOWN_RESET_PURPLE: tone = NppDarkMode::purpleTone; break;
				case IDD_DROPDOWN_RESET_CYAN: tone = NppDarkMode::cyanTone; break;
				case IDD_DROPDOWN_RESET_OLIVE: tone = NppDarkMode::oliveTone; break;
				default: break;
			}
			nppGUI._darkmode._customColors = NppDarkMode::getDarkModeDefaultColors(tone);
			doEnableCustomizedColorCtrls = true;
			break;
		}

		default:
			return;
	}

	// switch to chosen dark mode
	nppGUI._darkmode._isEnabled = true;
	enableCustomizedColorCtrls(doEnableCustomizedColorCtrls);
	applyChange(true, doEnableCustomizedColorCtrls);
}

// ---------------------------------------------------------------------------------------------------------------------
// Margins/Border/Edge

void MarginsBorderEdgeSubDlg::initScintParam()
{
	NppParameters& nppParam = NppParameters::getInstance();
	ScintillaViewParams& svp = const_cast<ScintillaViewParams&>(nppParam.getSVP());

	int id = 0;
	switch (svp._folderStyle)
	{
		case FOLDER_STYLE_NONE: id = IDC_RADIO_FOLDMARGENONE; break;
		case FOLDER_STYLE_BOX: id = IDC_RADIO_BOX; break;
		case FOLDER_STYLE_CIRCLE: id = IDC_RADIO_CIRCLE; break;
		case FOLDER_STYLE_ARROW: id = IDC_RADIO_ARROW; break;
		default: id = IDC_RADIO_SIMPLE; // FOLDER_STYLE_SIMPLE
	}
	setChecked(id);

	setChecked(IDC_CHECK_LINENUMBERMARGE, svp._lineNumberMarginShow);
	setChecked(IDC_RADIO_DYNAMIC, svp._lineNumberMarginDynamicWidth);
	setChecked(IDC_RADIO_CONSTANT, !svp._lineNumberMarginDynamicWidth);
	enableItem(IDC_RADIO_DYNAMIC, svp._lineNumberMarginShow);
	enableItem(IDC_RADIO_CONSTANT, svp._lineNumberMarginShow);
	setChecked(IDC_CHECK_BOOKMARKMARGE, svp._bookMarkMarginShow);
	setChecked(IDC_CHECK_CHANGHISTORYMARGIN, svp._isChangeHistoryMarginEnabled);
	setChecked(IDC_CHECK_CHANGHISTORYINDICATOR, svp._isChangeHistoryIndicatorEnabled);
	setChecked(IDC_CHECK_NOEDGE, !svp._showBorderEdge);

	bool canBeBg = svp._edgeMultiColumnPos.size() == 1;
	if (!canBeBg)
	{
		svp._isEdgeBgMode = false;
		setChecked(IDC_CHECK_EDGEBGMODE, false);
		enableItem(IDC_CHECK_EDGEBGMODE, false);
	}
	else
	{
		setChecked(IDC_CHECK_EDGEBGMODE, svp._isEdgeBgMode);
	}

	wstring edgeColumnPosStr;
	for (auto i : svp._edgeMultiColumnPos)
	{
		edgeColumnPosStr += std::to_wstring(i);
		edgeColumnPosStr += L" ";
	}
	setItemText(IDC_COLUMNPOS_EDIT, edgeColumnPosStr);
	dlgItem(IDC_COLUMNPOS_EDIT)->installEventFilter(this); // upstream EditNumSpaceProc
}

bool MarginsBorderEdgeSubDlg::eventFilter(QObject* watched, QEvent* event)
{
	// upstream EditNumSpaceProc: only decimal numbers and white space can be typed or pasted
	if (watched == dlgItem(IDC_COLUMNPOS_EDIT) && event->type() == QEvent::KeyPress)
	{
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		const auto isNumSpace = [](const QString& s) {
			for (QChar c : s)
				if (c != QLatin1Char(' ') && (c < QLatin1Char('0') || c > QLatin1Char('9')))
					return false;
			return true;
		};
		if (keyEvent->matches(QKeySequence::Paste))
			return !isNumSpace(QApplication::clipboard()->text()); // the format is not correct: stop here
		const QString text = keyEvent->text();
		const bool isCtrl = keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
		if (!isCtrl && !text.isEmpty() && text[0].isPrint() && !isNumSpace(text))
			return true;
	}
	return PreferenceSubDlg::eventFilter(watched, event);
}

void MarginsBorderEdgeSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	const ScintillaViewParams& svp = nppParam.getSVP();

	setSliderRange(this, IDC_BORDERWIDTH_SLIDER, BORDERWIDTH_SMALLEST, BORDERWIDTH_LARGEST, BORDERWIDTH_INTERVAL, svp._borderWidth);
	setItemInt(IDC_BORDERWIDTHVAL_STATIC, svp._borderWidth);

	setSliderRange(this, IDC_PADDINGLEFT_SLIDER, PADDING_SMALLEST, PADDING_LARGEST, PADDING_INTERVAL, svp._paddingLeft);
	setItemInt(IDC_PADDINGLEFTVAL_STATIC, svp._paddingLeft);

	setSliderRange(this, IDC_PADDINGRIGHT_SLIDER, PADDING_SMALLEST, PADDING_LARGEST, PADDING_INTERVAL, svp._paddingRight);
	setItemInt(IDC_PADDINGRIGHTVAL_STATIC, svp._paddingRight);

	setSliderRange(this, IDC_DISTRACTIONFREE_SLIDER, DISTRACTIONFREE_SMALLEST, DISTRACTIONFREE_LARGEST, DISTRACTIONFREE_INTERVAL, svp._distractionFreeDivPart);
	setItemInt(IDC_DISTRACTIONFREEVAL_STATIC, svp._distractionFreeDivPart);

	NativeLangSpeaker* pNativeSpeaker = nppParam.getNativeLangSpeaker();
	setTip(IDC_BUTTON_VES_TIP, pNativeSpeaker->getLocalizedStrFromID("verticalEdge-tip",
		L"Add your column marker by indicating its position with a decimal number. You can define several column markers by using white space to separate the different numbers."));

	initScintParam();
}

void MarginsBorderEdgeSubDlg::onSliderChanged(int id, int value)
{
	NppParameters& nppParam = NppParameters::getInstance();
	auto& svp = const_cast<ScintillaViewParams&>(nppParam.getSVP());

	switch (id)
	{
		case IDC_BORDERWIDTH_SLIDER:
			svp._borderWidth = value;
			setItemInt(IDC_BORDERWIDTHVAL_STATIC, value);
			sendToNpp(NPPM_SETEDITORBORDEREDGE, 0, svp._showBorderEdge ? TRUE : FALSE); // NPLX: upstream WM_SIZE
			return;

		case IDC_PADDINGLEFT_SLIDER:
			svp._paddingLeft = static_cast<unsigned char>(value);
			setItemInt(IDC_PADDINGLEFTVAL_STATIC, value);
			sendToNpp(NPPM_INTERNAL_UPDATETEXTZONEPADDING);
			return;

		case IDC_PADDINGRIGHT_SLIDER:
			svp._paddingRight = static_cast<unsigned char>(value);
			setItemInt(IDC_PADDINGRIGHTVAL_STATIC, value);
			sendToNpp(NPPM_INTERNAL_UPDATETEXTZONEPADDING);
			return;

		case IDC_DISTRACTIONFREE_SLIDER:
			svp._distractionFreeDivPart = static_cast<unsigned char>(value);
			setItemInt(IDC_DISTRACTIONFREEVAL_STATIC, value);
			sendToNpp(NPPM_INTERNAL_UPDATETEXTZONEPADDING);
			return;

		default:
			return;
	}
}

void MarginsBorderEdgeSubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	auto& svp = const_cast<ScintillaViewParams&>(nppParam.getSVP());
	static bool changeHistoryWarningHasBeenGiven = false;

	auto warnChangeHistoryRestart = []() {
		if (!changeHistoryWarningHasBeenGiven)
		{
			infoBox("ChangeHistoryEnabledWarning", L"You have to restart NotePadLX to enable Change History.", L"NotePadLX needs to be relaunched");
			changeHistoryWarningHasBeenGiven = true;
		}
	};

	switch (id)
	{
		case IDC_CHECK_LINENUMBERMARGE:
			svp._lineNumberMarginShow = isCheckedOrNot(IDC_CHECK_LINENUMBERMARGE);
			enableItem(IDC_RADIO_DYNAMIC, svp._lineNumberMarginShow);
			enableItem(IDC_RADIO_CONSTANT, svp._lineNumberMarginShow);
			sendToNpp(NPPM_INTERNAL_LINENUMBER);
			return;

		case IDC_RADIO_DYNAMIC:
			svp._lineNumberMarginDynamicWidth = isCheckedOrNot(IDC_RADIO_DYNAMIC);
			sendToNpp(NPPM_INTERNAL_LINENUMBER);
			return;

		case IDC_RADIO_CONSTANT:
			svp._lineNumberMarginDynamicWidth = !isCheckedOrNot(IDC_RADIO_CONSTANT);
			sendToNpp(NPPM_INTERNAL_LINENUMBER);
			return;

		case IDC_CHECK_BOOKMARKMARGE:
			svp._bookMarkMarginShow = isCheckedOrNot(IDC_CHECK_BOOKMARKMARGE);
			sendToNpp(NPPM_INTERNAL_SYMBOLMARGIN);
			return;

		case IDC_CHECK_CHANGHISTORYMARGIN:
		{
			bool isMarginJustEnabled = isCheckedOrNot(IDC_CHECK_CHANGHISTORYMARGIN);
			bool isIndicatorAlreadyEnabled = isCheckedOrNot(IDC_CHECK_CHANGHISTORYINDICATOR);
			if (isMarginJustEnabled && !isIndicatorAlreadyEnabled) // In the case that both "in margin" & "in text" were disabled, but "in margin" is just enabled
			{
				warnChangeHistoryRestart();
				svp._isChangeHistoryMarginEnabled = true;
				svp._isChangeHistoryEnabled4NextSession = changeHistoryState::margin;
			}
			else // otherwise
			{
				svp._isChangeHistoryMarginEnabled = isMarginJustEnabled;
				svp._isChangeHistoryEnabled4NextSession = (!isMarginJustEnabled && !isIndicatorAlreadyEnabled) ? changeHistoryState::disable :
					(isMarginJustEnabled && isIndicatorAlreadyEnabled) ? changeHistoryState::marginIndicator : changeHistoryState::indicator;
				sendToNpp(NPPM_INTERNAL_ENABLECHANGEHISTORY);
			}
			return;
		}

		case IDC_CHECK_CHANGHISTORYINDICATOR:
		{
			bool isIndicatorJustEnabled = isCheckedOrNot(IDC_CHECK_CHANGHISTORYINDICATOR);
			bool isMarginAlreadyEnabled = isCheckedOrNot(IDC_CHECK_CHANGHISTORYMARGIN);
			if (isIndicatorJustEnabled && !isMarginAlreadyEnabled) // In the case that both "in margin" & "in text" were disabled, but "in text" is just enabled
			{
				warnChangeHistoryRestart();
				svp._isChangeHistoryIndicatorEnabled = true;
				svp._isChangeHistoryEnabled4NextSession = changeHistoryState::indicator;
			}
			else
			{
				svp._isChangeHistoryIndicatorEnabled = isIndicatorJustEnabled;
				svp._isChangeHistoryEnabled4NextSession = (!isIndicatorJustEnabled && !isMarginAlreadyEnabled) ? changeHistoryState::disable :
					(isIndicatorJustEnabled && isMarginAlreadyEnabled) ? changeHistoryState::marginIndicator : changeHistoryState::margin;
				sendToNpp(NPPM_INTERNAL_ENABLECHANGEHISTORY);
			}
			return;
		}

		case IDC_CHECK_NOEDGE:
			svp._showBorderEdge = !isCheckedOrNot(IDC_CHECK_NOEDGE);
			sendToNpp(NPPM_SETEDITORBORDEREDGE, 0, svp._showBorderEdge ? TRUE : FALSE);
			return;

		case IDC_RADIO_SIMPLE:
			svp._folderStyle = FOLDER_STYLE_SIMPLE;
			sendToNpp(NPPM_INTERNAL_FOLDSYMBOLSIMPLE);
			return;

		case IDC_RADIO_ARROW:
			svp._folderStyle = FOLDER_STYLE_ARROW;
			sendToNpp(NPPM_INTERNAL_FOLDSYMBOLARROW);
			return;

		case IDC_RADIO_CIRCLE:
			svp._folderStyle = FOLDER_STYLE_CIRCLE;
			sendToNpp(NPPM_INTERNAL_FOLDSYMBOLCIRCLE);
			return;

		case IDC_RADIO_BOX:
			svp._folderStyle = FOLDER_STYLE_BOX;
			sendToNpp(NPPM_INTERNAL_FOLDSYMBOLBOX);
			return;

		case IDC_RADIO_FOLDMARGENONE:
			svp._folderStyle = FOLDER_STYLE_NONE;
			sendToNpp(NPPM_INTERNAL_FOLDSYMBOLNONE);
			return;

		case IDC_CHECK_EDGEBGMODE:
			svp._isEdgeBgMode = isCheckedOrNot(IDC_CHECK_EDGEBGMODE);
			sendToNpp(NPPM_INTERNAL_EDGEMULTISETSIZE);
			return;

		case IDC_COLUMNPOS_EDIT:
		{
			if (notification != RcNotifyEditChange)
				return;
			const wstring text = getItemText(IDC_COLUMNPOS_EDIT);
			if (str2numberVector(text, svp._edgeMultiColumnPos))
			{
				bool canBeBg = svp._edgeMultiColumnPos.size() == 1;
				if (!canBeBg)
				{
					svp._isEdgeBgMode = false;
					setChecked(IDC_CHECK_EDGEBGMODE, false);
				}
				enableItem(IDC_CHECK_EDGEBGMODE, canBeBg);
				sendToNpp(NPPM_INTERNAL_EDGEMULTISETSIZE);
			}
			return;
		}

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// New Document

void NewDocumentSubDlg::makeOpenAnsiAsUtf8(bool doIt) const
{
	if (!doIt)
		setChecked(IDC_CHECK_OPENANSIASUTF8, false);
	enableItem(IDC_CHECK_OPENANSIASUTF8, doIt && !NppParameters::getInstance().isCurrentSystemCodepageUTF8());
}

void NewDocumentSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();
	const NewDocDefaultSettings& ndds = nppGUI.getNewDocDefaultSettings();

	int ID2Check = IDC_RADIO_F_WIN;
	switch (ndds._format)
	{
		case EolType::windows: ID2Check = IDC_RADIO_F_WIN; break;
		case EolType::macos: ID2Check = IDC_RADIO_F_MAC; break;
		case EolType::unix: ID2Check = IDC_RADIO_F_UNIX; break;
		case EolType::unknown: break;
	}
	setChecked(ID2Check);

	switch (ndds._unicodeMode)
	{
		case uni16BE: ID2Check = IDC_RADIO_UTF16BIG; break;
		case uni16LE: ID2Check = IDC_RADIO_UTF16SMALL; break;
		case uniUTF8: ID2Check = IDC_RADIO_UTF8; break;
		case uniUTF8_NoBOM: ID2Check = IDC_RADIO_UTF8_NO_BOM; break;
		default: ID2Check = IDC_RADIO_ANSI; // uni8Bit
	}

	int selIndex = -1;
	wstring str;
	EncodingMapper& em = EncodingMapper::getInstance();
	QComboBox* otherCP = comboBox(IDC_COMBO_OTHERCP);
	for (int encoding : encodings)
	{
		int cmdID = em.getIndexFromEncoding(encoding);
		if (cmdID != -1)
		{
			cmdID += IDM_FORMAT_ENCODE;
			getNameStrFromCmd(cmdID, str);
			otherCP->addItem(QString::fromStdWString(str), encoding);
			if (ndds._codepage == encoding)
				selIndex = otherCP->count() - 1;
		}
	}

	if (ndds._codepage == -1 || selIndex == -1)
	{
		enableItem(IDC_COMBO_OTHERCP, false);
	}
	else
	{
		ID2Check = IDC_RADIO_OTHERCP;
		otherCP->setCurrentIndex(selIndex);
	}
	setChecked(ID2Check);

	setChecked(IDC_CHECK_OPENANSIASUTF8, ID2Check == IDC_RADIO_UTF8_NO_BOM && ndds._openAnsiAsUtf8);

	bool isEnableAnsiAsUTF = ID2Check == IDC_RADIO_UTF8_NO_BOM;
	if (nppParam.isCurrentSystemCodepageUTF8())
	{
		isEnableAnsiAsUTF = false;
		enableItem(IDC_RADIO_ANSI, false);
	}
	enableItem(IDC_CHECK_OPENANSIASUTF8, isEnableAnsiAsUTF);

	// Add lexer languages to the Default Language dropdown
	QComboBox* langCombo = comboBox(IDC_COMBO_DEFAULTLANG);
	for (int i = L_TEXT + 1; i < nppParam.L_END; ++i) // Skip L_TEXT
	{
		LangType lt = static_cast<LangType>(i);
		str.clear();
		if (lt != L_USER && lt != L_JS_EMBEDDED)
		{
			int cmdID = nppParam.langTypeToCommandID(lt);
			if (cmdID != -1)
			{
				getNameStrFromCmd(cmdID, str);
				if (str.length() > 0)
					langCombo->addItem(QString::fromStdWString(str), static_cast<int>(lt));
			}
		}
	}

	// Add UDL languages to the Default Language dropdown
	for (int u = 0; u < nppParam.getNbUserLang(); ++u)
	{
		const UserLangContainer* ulc = nppParam.getULCFromIndex(u);
		langCombo->addItem(QString::fromWCharArray(ulc->getName()), static_cast<int>(L_USER));
	}

	// Insert L_TEXT to the 1st position
	int normalTextCmdID = nppParam.langTypeToCommandID(L_TEXT);
	getNameStrFromCmd(normalTextCmdID, str);
	langCombo->insertItem(0, QString::fromStdWString(str), static_cast<int>(L_TEXT));

	// Set chosen language
	int j = 0;
	for (; j < langCombo->count(); ++j)
	{
		const LangType l = static_cast<LangType>(langCombo->itemData(j).toInt());
		if (ndds._lang == l)
		{
			if (l != L_USER || ndds._udl == langCombo->itemText(j).toStdWString())
				break;
		}
	}
	langCombo->setCurrentIndex(j < langCombo->count() ? j : 0);

	setChecked(IDC_CHECK_ADDNEWDOCONSTARTUP, ndds._addNewDocumentOnStartup);
	setChecked(IDC_CHECK_USECONTENTASTABNAME, ndds._useContentAsTabName);
}

void NewDocumentSubDlg::onCommand(int id, int notification)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	auto& ndds = const_cast<NewDocDefaultSettings&>(nppGUI.getNewDocDefaultSettings());

	auto setUnicodeMode = [this, &ndds](UniMode mode) {
		ndds._unicodeMode = mode;
		ndds._openAnsiAsUtf8 = false;
		makeOpenAnsiAsUtf8(mode == uniUTF8_NoBOM);
		ndds._codepage = -1;
		enableItem(IDC_COMBO_OTHERCP, false);
	};

	switch (id)
	{
		case IDC_RADIO_UTF16BIG: setUnicodeMode(uni16BE); return;
		case IDC_RADIO_UTF16SMALL: setUnicodeMode(uni16LE); return;
		case IDC_RADIO_UTF8: setUnicodeMode(uniUTF8); return;
		case IDC_RADIO_UTF8_NO_BOM: setUnicodeMode(uniUTF8_NoBOM); return;
		case IDC_RADIO_ANSI: setUnicodeMode(uni8Bit); return;

		case IDC_CHECK_OPENANSIASUTF8:
			ndds._openAnsiAsUtf8 = isCheckedOrNot(IDC_CHECK_OPENANSIASUTF8);
			return;

		case IDC_RADIO_OTHERCP:
		{
			ndds._openAnsiAsUtf8 = false;
			makeOpenAnsiAsUtf8(false);
			enableItem(IDC_COMBO_OTHERCP, true);
			QComboBox* otherCP = comboBox(IDC_COMBO_OTHERCP);
			if (otherCP->currentIndex() < 0 && otherCP->count() > 0)
			{
				QSignalBlocker blocker(otherCP);
				otherCP->setCurrentIndex(0);
			}
			ndds._codepage = comboData(this, IDC_COMBO_OTHERCP, otherCP->currentIndex());
			return;
		}

		case IDC_RADIO_F_MAC: ndds._format = EolType::macos; return;
		case IDC_RADIO_F_UNIX: ndds._format = EolType::unix; return;
		case IDC_RADIO_F_WIN: ndds._format = EolType::windows; return;

		case IDC_CHECK_ADDNEWDOCONSTARTUP:
			ndds._addNewDocumentOnStartup = isCheckedOrNot(IDC_CHECK_ADDNEWDOCONSTARTUP);
			return;

		case IDC_CHECK_USECONTENTASTABNAME:
			ndds._useContentAsTabName = isCheckedOrNot(IDC_CHECK_USECONTENTASTABNAME);
			return;

		case IDC_COMBO_DEFAULTLANG:
		{
			if (notification != RcNotifySelChange)
				return;
			QComboBox* langCombo = comboBox(IDC_COMBO_DEFAULTLANG);
			const int index = langCombo->currentIndex();
			ndds._lang = static_cast<LangType>(comboData(this, IDC_COMBO_DEFAULTLANG, index));
			ndds._udl = (ndds._lang == L_USER) ? langCombo->itemText(index).toStdWString() : wstring();
			return;
		}

		case IDC_COMBO_OTHERCP:
			if (notification == RcNotifySelChange)
				ndds._codepage = comboData(this, IDC_COMBO_OTHERCP, comboBox(IDC_COMBO_OTHERCP)->currentIndex());
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Default Directory

void DefaultDirectorySubDlg::initDialog()
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	int ID2Check = 0;
	bool shouldActivated = false;
	switch (nppGUI._openSaveDir)
	{
		case dir_last: ID2Check = IDC_OPENSAVEDIR_REMEMBERLAST_RADIO; break;
		case dir_userDef: ID2Check = IDC_OPENSAVEDIR_ALWAYSON_RADIO; shouldActivated = true; break;
		default: ID2Check = IDC_OPENSAVEDIR_FOLLOWCURRENT_RADIO;
	}
	setChecked(ID2Check);
	enableItem(IDD_OPENSAVEDIR_ALWAYSON_BROWSE_BUTTON, shouldActivated);
	enableItem(IDC_OPENSAVEDIR_ALWAYSON_EDIT, shouldActivated);
	setItemText(IDC_OPENSAVEDIR_ALWAYSON_EDIT, nppGUI._defaultDir);
	setChecked(IDC_OPENSAVEDIR_CHECK_DROPFOLDEROPENFILES, nppGUI._isFolderDroppedOpenFiles);
}

void DefaultDirectorySubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	switch (id)
	{
		case IDC_OPENSAVEDIR_ALWAYSON_EDIT:
		{
			if (notification != RcNotifyEditChange || !dlgItem(IDC_OPENSAVEDIR_ALWAYSON_EDIT)->isEnabled())
				return;
			const wstring inputDir = getItemText(IDC_OPENSAVEDIR_ALWAYSON_EDIT);
			wcsncpy(nppGUI._defaultDir, inputDir.c_str(), MAX_PATH - 1);
			nppGUI._defaultDir[MAX_PATH - 1] = L'\0';
			wstring expanded = inputDir;
			expandEnv(expanded); // NPLX: ExpandEnvironmentStrings, also $VAR and ~
			wcsncpy(nppGUI._defaultDirExp, expanded.c_str(), MAX_PATH - 1);
			nppGUI._defaultDirExp[MAX_PATH - 1] = L'\0';
			nppParam.setWorkingDir(nppGUI._defaultDirExp);
			return;
		}

		case IDC_OPENSAVEDIR_FOLLOWCURRENT_RADIO:
			nppGUI._openSaveDir = dir_followCurrent;
			sendToNpp(NPPM_INTERNAL_REFRESHWORKDIR);
			enableItem(IDC_OPENSAVEDIR_ALWAYSON_EDIT, false);
			enableItem(IDD_OPENSAVEDIR_ALWAYSON_BROWSE_BUTTON, false);
			return;

		case IDC_OPENSAVEDIR_REMEMBERLAST_RADIO:
			nppGUI._openSaveDir = dir_last;
			enableItem(IDC_OPENSAVEDIR_ALWAYSON_EDIT, false);
			enableItem(IDD_OPENSAVEDIR_ALWAYSON_BROWSE_BUTTON, false);
			return;

		case IDC_OPENSAVEDIR_ALWAYSON_RADIO:
			nppGUI._openSaveDir = dir_userDef;
			sendToNpp(NPPM_INTERNAL_REFRESHWORKDIR);
			enableItem(IDC_OPENSAVEDIR_ALWAYSON_EDIT, true);
			enableItem(IDD_OPENSAVEDIR_ALWAYSON_BROWSE_BUTTON, true);
			return;

		case IDD_OPENSAVEDIR_ALWAYSON_BROWSE_BUTTON:
		{
			const wstring title = nppParam.getNativeLangSpeaker()->getLocalizedStrFromID("default-open-save-select-folder",
				L"Select a folder as default directory");
			// upstream folderBrowser(_hSelf, title, IDC_OPENSAVEDIR_ALWAYSON_EDIT): the edit notifies EN_CHANGE
			const wstring dir = nplx::uiServices() ? nplx::uiServices()->getExistingDirectory(title) : wstring();
			if (!dir.empty())
			{
				setItemText(IDC_OPENSAVEDIR_ALWAYSON_EDIT, dir);
				onCommand(IDC_OPENSAVEDIR_ALWAYSON_EDIT, RcNotifyEditChange);
			}
			return;
		}

		case IDC_OPENSAVEDIR_CHECK_DROPFOLDEROPENFILES:
			nppGUI._isFolderDroppedOpenFiles = isCheckedOrNot(IDC_OPENSAVEDIR_CHECK_DROPFOLDEROPENFILES);
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Recent Files History

void RecentFilesHistorySubDlg::setCustomLen(int val)
{
	setItemInt(IDC_EDIT_CUSTOMIZELENGTHVAL, val);
	enableItem(IDC_EDIT_CUSTOMIZELENGTHVAL, val > 0);
	enableItem(IDC_CUSTOMIZELENGTH_RANGE_STATIC, val > 0);
}

void RecentFilesHistorySubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	// Max number recent file setting
	setItemInt(IDC_EDIT_MAXNBFILEVAL, nppParam.getNbMaxRecentFile());

	// Check on launch time settings
	setChecked(IDC_CHECK_DONTCHECKHISTORY, !nppGUI._checkHistoryFiles);

	// Display in submenu setting
	setChecked(IDC_CHECK_INSUBMENU, nppParam.putRecentFileInSubMenu());

	// Recent File menu entry length setting
	int customLength = nppParam.getRecentFileCustomLength();
	int id = IDC_RADIO_CUSTOMIZELENTH;
	int length = customLength;

	if (customLength == RECENTFILES_SHOWONLYFILENAME)
	{
		id = IDC_RADIO_ONLYFILENAME;
		length = 0;
	}
	else if (customLength == RECENTFILES_SHOWFULLPATH || customLength < 0)
	{
		id = IDC_RADIO_FULLFILENAMEPATH;
		length = 0;
	}
	setChecked(id);
	setCustomLen(length);
}

void RecentFilesHistorySubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	switch (id)
	{
		case IDC_EDIT_MAXNBFILEVAL:
		{
			if (notification != RcNotifyKillFocus)
				return;
			if (getItemText(IDC_EDIT_MAXNBFILEVAL).empty())
			{
				setItemInt(IDC_EDIT_MAXNBFILEVAL, nppParam.getNbMaxRecentFile());
				return;
			}

			unsigned int nbMaxFile = getItemInt(IDC_EDIT_MAXNBFILEVAL);
			if (nbMaxFile == static_cast<unsigned int>(nppParam.getNbMaxRecentFile()))
				return;

			if (nbMaxFile > NB_MAX_LRF_FILE)
			{
				setItemInt(IDC_EDIT_MAXNBFILEVAL, NB_MAX_LRF_FILE);
				nbMaxFile = NB_MAX_LRF_FILE;
			}

			nppParam.setNbMaxRecentFile(nbMaxFile);
			sendToNpp(NPPM_INTERNAL_SETTING_HISTORY_SIZE);
			return;
		}

		case IDC_EDIT_CUSTOMIZELENGTHVAL:
		{
			if (notification != RcNotifyKillFocus || !isCheckedOrNot(IDC_RADIO_CUSTOMIZELENTH))
				return;
			if (getItemText(IDC_EDIT_CUSTOMIZELENGTHVAL).empty())
			{
				setItemInt(IDC_EDIT_CUSTOMIZELENGTHVAL, nppParam.getRecentFileCustomLength());
				return;
			}

			unsigned int size = getItemInt(IDC_EDIT_CUSTOMIZELENGTHVAL);
			if (size == static_cast<unsigned int>(nppParam.getRecentFileCustomLength()))
				return;

			bool change = false;
			if (size == 0)
			{
				size = NB_DEFAULT_LRF_CUSTOMLENGTH;
				change = true;
			}
			else if (size > NB_MAX_LRF_CUSTOMLENGTH)
			{
				size = NB_MAX_LRF_CUSTOMLENGTH;
				change = true;
			}
			if (change)
				setItemInt(IDC_EDIT_CUSTOMIZELENGTHVAL, size);

			nppParam.setRecentFileCustomLength(size);
			sendToNpp(NPPM_INTERNAL_RECENTFILELIST_UPDATE);
			return;
		}

		case IDC_CHECK_DONTCHECKHISTORY:
			nppGUI._checkHistoryFiles = !isCheckedOrNot(IDC_CHECK_DONTCHECKHISTORY);
			return;

		case IDC_CHECK_INSUBMENU:
			nppParam.setPutRecentFileInSubMenu(isCheckedOrNot(IDC_CHECK_INSUBMENU));
			sendToNpp(NPPM_INTERNAL_RECENTFILELIST_SWITCH);
			return;

		case IDC_RADIO_ONLYFILENAME:
			setCustomLen(0);
			nppParam.setRecentFileCustomLength(0);
			sendToNpp(NPPM_INTERNAL_RECENTFILELIST_UPDATE);
			return;

		case IDC_RADIO_FULLFILENAMEPATH:
			setCustomLen(0);
			nppParam.setRecentFileCustomLength(-1);
			sendToNpp(NPPM_INTERNAL_RECENTFILELIST_UPDATE);
			return;

		case IDC_RADIO_CUSTOMIZELENTH:
		{
			int len = nppParam.getRecentFileCustomLength();
			if (len <= 0)
			{
				setCustomLen(NB_DEFAULT_LRF_CUSTOMLENGTH);
				nppParam.setRecentFileCustomLength(NB_DEFAULT_LRF_CUSTOMLENGTH);
				sendToNpp(NPPM_INTERNAL_RECENTFILELIST_UPDATE);
			}
			return;
		}

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Language

void LanguageSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	//
	// Lang Menu
	//
	for (int i = L_TEXT; i < nppParam.L_END; ++i)
	{
		if (static_cast<LangType>(i) != L_USER && static_cast<LangType>(i) != L_JS_EMBEDDED)
		{
			int cmdID = nppParam.langTypeToCommandID(static_cast<LangType>(i));
			if (cmdID != -1)
			{
				wstring str;
				getNameStrFromCmd(cmdID, str);
				if (str.length() > 0)
				{
					// NPLX: upstream lists the languages still in the menu (the excluded ones were removed from it)
					bool isExcluded = false;
					for (const LangMenuItem& lmi : nppGUI._excludedLangList)
						isExcluded = isExcluded || (lmi._langType == static_cast<LangType>(i));
					if (!isExcluded)
						_langList.push_back(LangMenuItem(static_cast<LangType>(i), cmdID, str));
				}
			}
		}
	}
	std::sort(_langList.begin(), _langList.end());

	QListWidget* enabledList = listBox(IDC_LIST_ENABLEDLANG);
	for (const LangMenuItem& lmi : _langList)
		enabledList->addItem(QString::fromStdWString(lmi._langName));

	std::sort(nppGUI._excludedLangList.begin(), nppGUI._excludedLangList.end());
	QListWidget* disabledList = listBox(IDC_LIST_DISABLEDLANG);
	for (const LangMenuItem& lmi : nppGUI._excludedLangList)
		disabledList->addItem(QString::fromStdWString(lmi._langName));

	setChecked(IDC_CHECK_LANGMENUCOMPACT, nppGUI._isLangMenuCompact);
	enableItem(IDC_BUTTON_REMOVE, false);
	enableItem(IDC_BUTTON_RESTORE, false);

	setChecked(IDC_CHECK_BACKSLASHISESCAPECHARACTERFORSQL, nppGUI._backSlashIsEscapeCharacterForSql);
}

void LanguageSubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	if (id == IDC_LIST_DISABLEDLANG || id == IDC_LIST_ENABLEDLANG)
	{
		if (notification == RcNotifySelChange)
		{
			const int idButton2Enable = (id == IDC_LIST_ENABLEDLANG) ? IDC_BUTTON_REMOVE : IDC_BUTTON_RESTORE;
			const int idButton2Disable = (id == IDC_LIST_ENABLEDLANG) ? IDC_BUTTON_RESTORE : IDC_BUTTON_REMOVE;
			if (listBox(id)->currentRow() != -1)
			{
				enableItem(idButton2Enable, true);
				QListWidget* other = listBox(id == IDC_LIST_ENABLEDLANG ? IDC_LIST_DISABLEDLANG : IDC_LIST_ENABLEDLANG);
				QSignalBlocker blocker(other);
				other->setCurrentRow(-1);
				other->clearSelection();
				enableItem(idButton2Disable, false);
			}
		}
		else if (notification == RcNotifyDblClick)
		{
			// On double click an item, the item should be moved from one list to other list
			onCommand(id == IDC_LIST_ENABLEDLANG ? IDC_BUTTON_REMOVE : IDC_BUTTON_RESTORE, RcNotifyClicked);
		}
		return;
	}

	switch (id)
	{
		case IDC_CHECK_LANGMENUCOMPACT:
			nppGUI._isLangMenuCompact = isCheckedOrNot(IDC_CHECK_LANGMENUCOMPACT);
			infoBox("LanguageMenuCompactWarning", L"This option will be changed on the next launch.", L"Compact Language Menu");
			return;

		case IDC_CHECK_BACKSLASHISESCAPECHARACTERFORSQL:
			nppGUI._backSlashIsEscapeCharacterForSql = isCheckedOrNot(IDC_CHECK_BACKSLASHISESCAPECHARACTERFORSQL);
			sendToNpp(NPPM_INTERNAL_SQLBACKSLASHESCAPE);
			return;

		case IDC_BUTTON_RESTORE:
		case IDC_BUTTON_REMOVE:
		{
			const bool isRemove = (id == IDC_BUTTON_REMOVE);
			const int list2Remove = isRemove ? IDC_LIST_ENABLEDLANG : IDC_LIST_DISABLEDLANG;
			const int list2Add = isRemove ? IDC_LIST_DISABLEDLANG : IDC_LIST_ENABLEDLANG;
			const int idButton2Enable = isRemove ? IDC_BUTTON_RESTORE : IDC_BUTTON_REMOVE;
			const int idButton2Disable = isRemove ? IDC_BUTTON_REMOVE : IDC_BUTTON_RESTORE;
			vector<LangMenuItem>* pSrcLst = isRemove ? &_langList : &nppGUI._excludedLangList;
			vector<LangMenuItem>* pDestLst = isRemove ? &nppGUI._excludedLangList : &_langList;

			QListWidget* removeList = listBox(list2Remove);
			QListWidget* addList = listBox(list2Add);
			const int iRemove = removeList->currentRow();
			if (iRemove < 0 || static_cast<size_t>(iRemove) >= pSrcLst->size())
				return;

			LangMenuItem lmi = pSrcLst->at(iRemove);
			pSrcLst->erase(pSrcLst->begin() + iRemove);
			{
				QSignalBlocker blockRemove(removeList);
				QSignalBlocker blockAdd(addList);
				delete removeList->takeItem(iRemove);
				addList->addItem(QString::fromStdWString(lmi._langName));
				addList->setCurrentRow(addList->count() - 1);
				removeList->setCurrentRow(-1);
				removeList->clearSelection();
			}
			pDestLst->push_back(lmi);

			enableItem(idButton2Enable, true);
			enableItem(idButton2Disable, false);

			if ((lmi._langType >= L_EXTERNAL) && (lmi._langType < nppParam.L_END))
			{
				const std::string langName = wstring2string(lmi._langName, CP_UTF8);
				const char* excludeValue = isRemove ? "yes" : "no";
				bool found = false;
				for (const auto& extLexer : *nppParam.getExternalLexerDoc())
				{
					NppXml::Element root = NppXml::firstChildElement(extLexer._doc, "NotepadPlus");
					if (!root)
						continue;
					NppXml::Element lexersRoot = NppXml::firstChildElement(root, "LexerStyles");
					if (!lexersRoot)
						continue;
					for (NppXml::Element childNode = NppXml::firstChildElement(lexersRoot, "LexerType");
						childNode;
						childNode = NppXml::nextSiblingElement(childNode, "LexerType"))
					{
						if (NppXml::attribute(childNode, "name", "") == langName)
						{
							NppXml::setAttribute(childNode, "excluded", excludeValue);
							static_cast<void>(NppXml::saveFile(extLexer._doc, extLexer._path.c_str()));
							found = true;
							break;
						}
					}
					if (found)
						break;
				}
			}

			// upstream: DeleteMenu / InsertMenu in the Language menu
			sendToNpp(NPPM_INTERNAL_NPLX_SHOWLANGMENUITEM, static_cast<uptr_t>(lmi._cmdID), isRemove ? FALSE : TRUE);
			return;
		}

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Indentation

Lang* IndentationSubDlg::selectedLang() const
{
	QListWidget* list = listBox(IDC_LIST_TABSETTNG);
	const int index = list ? list->currentRow() : -1;
	if (index <= 0)
		return nullptr;
	return reinterpret_cast<Lang*>(list->item(index)->data(Qt::UserRole).value<quintptr>());
}

void IndentationSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	//
	// Tab settings
	//
	setItemInt(IDC_EDIT_TABSIZEVAL, nppGUI._tabSize);
	setChecked(IDC_RADIO_REPLACEBYSPACE, nppGUI._tabReplacedBySpace);
	setChecked(IDC_RADIO_USINGTAB, !nppGUI._tabReplacedBySpace);
	setChecked(IDC_CHECK_BACKSPACEUNINDENT, nppGUI._backspaceUnindent);

	QListWidget* list = listBox(IDC_LIST_TABSETTNG);
	list->addItem(QStringLiteral("[Default]"));
	const int nbLang = nppParam.getNbLang();
	for (int i = 0; i < nbLang; ++i)
	{
		Lang* lang = nppParam.getLangFromIndex(i);
		if (!lang)
			continue;
		LanguageNameInfo lni = nppParam.getLangNameInfoFromNameID(lang->_langName);
		if (!lni._shortName || lni._langID == L_JS_EMBEDDED)
			continue;
		auto* item = new QListWidgetItem(QString::fromWCharArray(lni._shortName), list);
		item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(lang)));
	}

	{
		QSignalBlocker blocker(list);
		list->setCurrentRow(0);
	}
	showItem(IDC_GR_TABVALUE_STATIC, false);
	showItem(IDC_CHECK_DEFAULTTABVALUE, false);

	//
	// Auto-indent settings
	//
	int choiceID = IDC_RADIO_AUTOINDENT_ADVANCED;
	if (nppGUI._maintainIndent == autoIndent_none)
		choiceID = IDC_RADIO_AUTOINDENT_NONE;
	else if (nppGUI._maintainIndent == autoIndent_basic)
		choiceID = IDC_RADIO_AUTOINDENT_BASIC;
	setChecked(choiceID);

	NativeLangSpeaker* pNativeSpeaker = nppParam.getNativeLangSpeaker();
	setTip(IDC_RADIO_AUTOINDENT_BASIC, pNativeSpeaker->getLocalizedStrFromID("autoIndentBasic-tip",
		L"Ensure that the indentation of the current line (i.e. the new line created by pressing the ENTER key) matches the indentation of the previous line."));
	setTip(IDC_RADIO_AUTOINDENT_ADVANCED, pNativeSpeaker->getLocalizedStrFromID("autoIndentAdvanced-tip",
		L"Enable smart indentation for \"C-like\" languages and Python. The \"C-like\" languages include:\n"
		L"C, C++, Java, C#, Objective-C, PHP, JavaScript, JSP, CSS, Perl, Rust, PowerShell, TypeScript, Go, Swift and JSON.\n"
		L"\n"
		L"If you select advanced mode but do not edit files in the aforementioned languages, the indentation will remain in basic mode."));
}

void IndentationSubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	auto setTabCtrls = [this](int tabSize, bool replacedBySpace, bool backspaceUnindent, bool enable) {
		setItemInt(IDC_EDIT_TABSIZEVAL, tabSize);
		setChecked(IDC_RADIO_REPLACEBYSPACE, replacedBySpace);
		setChecked(IDC_RADIO_USINGTAB, !replacedBySpace);
		setChecked(IDC_CHECK_BACKSPACEUNINDENT, backspaceUnindent);
		for (int ctrl : { IDC_EDIT_TABSIZEVAL, IDC_RADIO_REPLACEBYSPACE, IDC_RADIO_USINGTAB, IDC_CHECK_BACKSPACEUNINDENT,
			IDC_TABSIZE_STATIC, IDC_INDENTUSING_STATIC })
			enableItem(ctrl, enable);
	};

	switch (id)
	{
		case IDC_LIST_TABSETTNG:
		{
			if (notification != RcNotifySelChange)
				return;
			const int index = listBox(IDC_LIST_TABSETTNG)->currentRow();
			if (index < 0)
				return;

			showItem(IDC_GR_TABVALUE_STATIC, index > 0);
			showItem(IDC_CHECK_DEFAULTTABVALUE, index > 0);

			if (index > 0)
			{
				const Lang* lang = selectedLang();
				if (!lang)
					return;
				bool useDefaultTab = (lang->_tabSize == -1 || lang->_tabSize == 0);
				setChecked(IDC_CHECK_DEFAULTTABVALUE, useDefaultTab);
				setTabCtrls(useDefaultTab ? nppGUI._tabSize : lang->_tabSize,
					useDefaultTab ? nppGUI._tabReplacedBySpace : lang->_isTabReplacedBySpace,
					useDefaultTab ? nppGUI._backspaceUnindent : lang->_isBackspaceUnindent, !useDefaultTab);
			}
			else
			{
				setTabCtrls(nppGUI._tabSize, nppGUI._tabReplacedBySpace, nppGUI._backspaceUnindent, true);
			}
			return;
		}

		case IDC_EDIT_TABSIZEVAL:
		{
			if (notification == RcNotifyEditChange)
			{
				const auto tabSize = static_cast<int>(getItemInt(IDC_EDIT_TABSIZEVAL));
				if (tabSize < 1)
					return;

				const bool useDefaultTab = isCheckedOrNot(IDC_CHECK_DEFAULTTABVALUE);
				Lang* lang = selectedLang();
				if (!useDefaultTab && lang)
				{
					lang->_tabSize = tabSize;
					// write in langs.xml
					nppParam.insertTabInfo(lang->getLangName(), lang->getTabInfo(), lang->_isBackspaceUnindent);
				}
				else
				{
					nppGUI._tabSize = tabSize;
				}
				sendToNpp(NPPM_INTERNAL_SET_TAB_SETTINGS);
			}
			else if (notification == RcNotifyKillFocus)
			{
				if (getItemInt(IDC_EDIT_TABSIZEVAL) < 1)
				{
					const bool useDefaultTab = isCheckedOrNot(IDC_CHECK_DEFAULTTABVALUE);
					auto prevSize = nppGUI._tabSize;
					const Lang* lang = selectedLang();
					if (!useDefaultTab && lang && lang->_tabSize > 0)
						prevSize = lang->_tabSize;
					setItemInt(IDC_EDIT_TABSIZEVAL, prevSize);
				}
			}
			return;
		}

		case IDC_RADIO_REPLACEBYSPACE:
		case IDC_RADIO_USINGTAB:
		{
			bool isTabReplacedBySpace = isCheckedOrNot(IDC_RADIO_REPLACEBYSPACE);
			if (Lang* lang = selectedLang())
			{
				if (!lang->_tabSize || lang->_tabSize == -1)
					lang->_tabSize = nppGUI._tabSize;
				lang->_isTabReplacedBySpace = isTabReplacedBySpace;
				// write in langs.xml
				nppParam.insertTabInfo(lang->getLangName(), lang->getTabInfo(), lang->_isBackspaceUnindent);
			}
			else
			{
				nppGUI._tabReplacedBySpace = isTabReplacedBySpace;
			}
			sendToNpp(NPPM_INTERNAL_SET_TAB_SETTINGS);
			return;
		}

		case IDC_CHECK_BACKSPACEUNINDENT:
		{
			bool isBackspaceUnindent = isCheckedOrNot(IDC_CHECK_BACKSPACEUNINDENT);
			if (Lang* lang = selectedLang())
			{
				if (!lang->_tabSize || lang->_tabSize == -1)
					lang->_tabSize = nppGUI._tabSize;
				lang->_isBackspaceUnindent = isBackspaceUnindent;
				// write in langs.xml
				nppParam.insertTabInfo(lang->getLangName(), lang->getTabInfo(), lang->_isBackspaceUnindent);
			}
			else
			{
				nppGUI._backspaceUnindent = isBackspaceUnindent;
			}
			sendToNpp(NPPM_INTERNAL_SET_TAB_SETTINGS);
			return;
		}

		case IDC_CHECK_DEFAULTTABVALUE:
		{
			const bool useDefaultTab = isCheckedOrNot(IDC_CHECK_DEFAULTTABVALUE);
			Lang* lang = selectedLang();
			if (!lang)
				return;

			//- Set tab setting in chosen language
			lang->_tabSize = useDefaultTab ? 0 : nppGUI._tabSize;
			lang->_isTabReplacedBySpace = useDefaultTab ? false : nppGUI._tabReplacedBySpace;
			lang->_isBackspaceUnindent = useDefaultTab ? false : nppGUI._backspaceUnindent;

			//- set visual effect
			setTabCtrls(useDefaultTab ? nppGUI._tabSize : lang->_tabSize,
				useDefaultTab ? nppGUI._tabReplacedBySpace : lang->_isTabReplacedBySpace,
				useDefaultTab ? nppGUI._backspaceUnindent : lang->_isBackspaceUnindent, !useDefaultTab);

			// write in langs.xml
			if (useDefaultTab)
				nppParam.insertTabInfo(lang->getLangName(), -1, false);
			return;
		}

		case IDC_RADIO_AUTOINDENT_NONE:
			nppGUI._maintainIndent = autoIndent_none;
			return;

		case IDC_RADIO_AUTOINDENT_BASIC:
			nppGUI._maintainIndent = autoIndent_basic;
			return;

		case IDC_RADIO_AUTOINDENT_ADVANCED:
			nppGUI._maintainIndent = autoIndent_advanced;
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Highlighting

void HighlightingSubDlg::initDialog()
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	setChecked(IDC_CHECK_MARKALLCASESENSITIVE, nppGUI._markAllCaseSensitive);
	setChecked(IDC_CHECK_MARKALLWHOLEWORDONLY, nppGUI._markAllWordOnly);
	setChecked(IDC_CHECK_ENABLSMARTHILITE, nppGUI._enableSmartHilite);
	setChecked(IDC_CHECK_SMARTHILITECASESENSITIVE, nppGUI._smartHiliteCaseSensitive);
	setChecked(IDC_CHECK_SMARTHILITEWHOLEWORDONLY, nppGUI._smartHiliteWordOnly);
	setChecked(IDC_CHECK_SMARTHILITEUSEFINDSETTINGS, nppGUI._smartHiliteUseFindSettings);
	setChecked(IDC_CHECK_SMARTHILITEANOTHERRVIEW, nppGUI._smartHiliteOnAnotherView);

	setChecked(IDC_CHECK_ENABLTAGSMATCHHILITE, nppGUI._enableTagsMatchHilite);
	setChecked(IDC_CHECK_ENABLTAGATTRHILITE, nppGUI._enableTagAttrsHilite);
	setChecked(IDC_CHECK_HIGHLITENONEHTMLZONE, nppGUI._enableHiliteNonHTMLZone);

	enableItem(IDC_CHECK_ENABLTAGATTRHILITE, nppGUI._enableTagsMatchHilite);
	enableItem(IDC_CHECK_HIGHLITENONEHTMLZONE, nppGUI._enableTagsMatchHilite);

	for (int ctrl : { IDC_CHECK_SMARTHILITECASESENSITIVE, IDC_CHECK_SMARTHILITEWHOLEWORDONLY, IDC_CHECK_SMARTHILITEUSEFINDSETTINGS,
		IDC_CHECK_SMARTHILITEANOTHERRVIEW, IDC_SMARTHILITEMATCHING_STATIC })
		enableItem(ctrl, nppGUI._enableSmartHilite);
}

void HighlightingSubDlg::onCommand(int id, int /*notification*/)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	switch (id)
	{
		case IDC_CHECK_MARKALLCASESENSITIVE:
			nppGUI._markAllCaseSensitive = isCheckedOrNot(IDC_CHECK_MARKALLCASESENSITIVE);
			sendToNpp(NPPM_INTERNAL_CLEARINDICATOR);
			return;

		case IDC_CHECK_MARKALLWHOLEWORDONLY:
			nppGUI._markAllWordOnly = isCheckedOrNot(IDC_CHECK_MARKALLWHOLEWORDONLY);
			sendToNpp(NPPM_INTERNAL_CLEARINDICATOR);
			return;

		case IDC_CHECK_ENABLSMARTHILITE:
			nppGUI._enableSmartHilite = !nppGUI._enableSmartHilite;
			for (int ctrl : { IDC_CHECK_SMARTHILITECASESENSITIVE, IDC_CHECK_SMARTHILITEWHOLEWORDONLY, IDC_CHECK_SMARTHILITEUSEFINDSETTINGS,
				IDC_CHECK_SMARTHILITEANOTHERRVIEW, IDC_SMARTHILITEMATCHING_STATIC })
				enableItem(ctrl, nppGUI._enableSmartHilite);
			sendToNpp(NPPM_INTERNAL_CLEARINDICATOR);
			return;

		case IDC_CHECK_SMARTHILITECASESENSITIVE:
			nppGUI._smartHiliteCaseSensitive = isCheckedOrNot(IDC_CHECK_SMARTHILITECASESENSITIVE);
			if (nppGUI._smartHiliteCaseSensitive)
			{
				setChecked(IDC_CHECK_SMARTHILITEUSEFINDSETTINGS, false);
				nppGUI._smartHiliteUseFindSettings = false;
			}
			sendToNpp(NPPM_INTERNAL_CLEARINDICATOR);
			return;

		case IDC_CHECK_SMARTHILITEWHOLEWORDONLY:
			nppGUI._smartHiliteWordOnly = isCheckedOrNot(IDC_CHECK_SMARTHILITEWHOLEWORDONLY);
			if (nppGUI._smartHiliteWordOnly)
			{
				setChecked(IDC_CHECK_SMARTHILITEUSEFINDSETTINGS, false);
				nppGUI._smartHiliteUseFindSettings = false;
			}
			sendToNpp(NPPM_INTERNAL_CLEARINDICATOR);
			return;

		case IDC_CHECK_SMARTHILITEUSEFINDSETTINGS:
			nppGUI._smartHiliteUseFindSettings = isCheckedOrNot(IDC_CHECK_SMARTHILITEUSEFINDSETTINGS);
			if (nppGUI._smartHiliteUseFindSettings)
			{
				setChecked(IDC_CHECK_SMARTHILITECASESENSITIVE, false);
				setChecked(IDC_CHECK_SMARTHILITEWHOLEWORDONLY, false);
				nppGUI._smartHiliteCaseSensitive = false;
				nppGUI._smartHiliteWordOnly = false;
			}
			sendToNpp(NPPM_INTERNAL_CLEARINDICATOR);
			return;

		case IDC_CHECK_SMARTHILITEANOTHERRVIEW:
			nppGUI._smartHiliteOnAnotherView = isCheckedOrNot(IDC_CHECK_SMARTHILITEANOTHERRVIEW);
			sendToNpp(NPPM_INTERNAL_CLEARINDICATOR);
			return;

		case IDC_CHECK_ENABLTAGSMATCHHILITE:
			nppGUI._enableTagsMatchHilite = !nppGUI._enableTagsMatchHilite;
			if (!nppGUI._enableTagsMatchHilite)
				sendToNpp(NPPM_INTERNAL_CLEARINDICATORTAGMATCH);
			enableItem(IDC_CHECK_ENABLTAGATTRHILITE, nppGUI._enableTagsMatchHilite);
			enableItem(IDC_CHECK_HIGHLITENONEHTMLZONE, nppGUI._enableTagsMatchHilite);
			return;

		case IDC_CHECK_ENABLTAGATTRHILITE:
			nppGUI._enableTagAttrsHilite = !nppGUI._enableTagAttrsHilite;
			if (!nppGUI._enableTagAttrsHilite)
				sendToNpp(NPPM_INTERNAL_CLEARINDICATORTAGATTR);
			return;

		case IDC_CHECK_HIGHLITENONEHTMLZONE:
			nppGUI._enableHiliteNonHTMLZone = !nppGUI._enableHiliteNonHTMLZone;
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Print

void PrintSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();
	const auto& ps = nppGUI._printSettings;

	setChecked(IDC_CHECK_PRINTLINENUM, ps._printLineNumber);
	setChecked(IDC_CHECK_PRINTFORMFEED, ps._printFormFeedPageBreak);

	int ID2Check = IDC_RADIO_WYSIWYG;
	switch (ps._printOption)
	{
		case SC_PRINT_NORMAL: ID2Check = IDC_RADIO_WYSIWYG; break;
		case SC_PRINT_INVERTLIGHT: ID2Check = IDC_RADIO_INVERT; break;
		case SC_PRINT_BLACKONWHITE: ID2Check = IDC_RADIO_BW; break;
		case SC_PRINT_COLOURONWHITE: ID2Check = IDC_RADIO_NOBG; break;
		default: break;
	}
	setChecked(ID2Check);

	setItemInt(IDC_EDIT_ML, ps._marge.left);
	setItemInt(IDC_EDIT_MR, ps._marge.right);
	setItemInt(IDC_EDIT_MT, ps._marge.top);
	setItemInt(IDC_EDIT_MB, ps._marge.bottom);

	setItemText(IDC_EDIT_HLEFT, ps._headerLeft);
	setItemText(IDC_EDIT_HMIDDLE, ps._headerMiddle);
	setItemText(IDC_EDIT_HRIGHT, ps._headerRight);
	setItemText(IDC_EDIT_FLEFT, ps._footerLeft);
	setItemText(IDC_EDIT_FMIDDLE, ps._footerMiddle);
	setItemText(IDC_EDIT_FRIGHT, ps._footerRight);

	for (int i = 6; i < 15; ++i)
	{
		comboBox(IDC_COMBO_HFONTSIZE)->addItem(QString::number(i));
		comboBox(IDC_COMBO_FFONTSIZE)->addItem(QString::number(i));
	}

	for (const wstring& font : nppParam.getFontList())
	{
		comboBox(IDC_COMBO_HFONTNAME)->addItem(QString::fromStdWString(font));
		comboBox(IDC_COMBO_FFONTNAME)->addItem(QString::fromStdWString(font));
	}

	int index = comboBox(IDC_COMBO_HFONTNAME)->findText(QString::fromStdWString(ps._headerFontName));
	comboBox(IDC_COMBO_HFONTNAME)->setCurrentIndex(index == -1 ? 0 : index);
	index = comboBox(IDC_COMBO_FFONTNAME)->findText(QString::fromStdWString(ps._footerFontName));
	comboBox(IDC_COMBO_FFONTNAME)->setCurrentIndex(index == -1 ? 0 : index);

	comboBox(IDC_COMBO_HFONTSIZE)->setCurrentIndex(comboBox(IDC_COMBO_HFONTSIZE)->findText(QString::number(ps._headerFontSize)));
	comboBox(IDC_COMBO_FFONTSIZE)->setCurrentIndex(comboBox(IDC_COMBO_FFONTSIZE)->findText(QString::number(ps._footerFontSize)));

	setChecked(IDC_CHECK_HBOLD, ps._headerFontStyle & FONTSTYLE_BOLD);
	setChecked(IDC_CHECK_HITALIC, ps._headerFontStyle & FONTSTYLE_ITALIC);
	setChecked(IDC_CHECK_FBOLD, ps._footerFontStyle & FONTSTYLE_BOLD);
	setChecked(IDC_CHECK_FITALIC, ps._footerFontStyle & FONTSTYLE_ITALIC);

	QComboBox* varCombo = comboBox(IDC_COMBO_VARLIST);
	for (const wchar_t* var : { L"Full file name path", L"File name", L"File directory", L"Page", L"Short date format", L"Long date format", L"Time" })
		varCombo->addItem(QString::fromWCharArray(var));

	varList.push_back(L"$(FULL_CURRENT_PATH)");
	varList.push_back(L"$(FILE_NAME)");
	varList.push_back(L"$(CURRENT_DIRECTORY)");
	varList.push_back(L"$(CURRENT_PRINTING_PAGE)");
	varList.push_back(L"$(SHORT_DATE)");
	varList.push_back(L"$(LONG_DATE)");
	varList.push_back(L"$(TIME)");

	varCombo->setCurrentIndex(0);

	// upstream EN_SETFOCUS of the header and footer edits
	for (int edit : { IDC_EDIT_HLEFT, IDC_EDIT_HMIDDLE, IDC_EDIT_HRIGHT, IDC_EDIT_FLEFT, IDC_EDIT_FMIDDLE, IDC_EDIT_FRIGHT })
		dlgItem(edit)->installEventFilter(this);

}

bool PrintSubDlg::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::FocusIn)
	{
		for (int edit : { IDC_EDIT_HLEFT, IDC_EDIT_HMIDDLE, IDC_EDIT_HRIGHT, IDC_EDIT_FLEFT, IDC_EDIT_FMIDDLE, IDC_EDIT_FRIGHT })
		{
			if (watched == dlgItem(edit))
			{
				editFocused(edit);
				break;
			}
		}
	}
	return PreferenceSubDlg::eventFilter(watched, event);
}

void PrintSubDlg::editFocused(int id)
{
	// upstream EN_SETFOCUS
	_focusedEditCtrl = id;
	int focusedEditStatic = 0;
	int groupStatic = 0;
	switch (_focusedEditCtrl)
	{
		case IDC_EDIT_HLEFT: focusedEditStatic = IDC_HL_STATIC; groupStatic = IDC_HGB_STATIC; break;
		case IDC_EDIT_HMIDDLE: focusedEditStatic = IDC_HM_STATIC; groupStatic = IDC_HGB_STATIC; break;
		case IDC_EDIT_HRIGHT: focusedEditStatic = IDC_HR_STATIC; groupStatic = IDC_HGB_STATIC; break;
		case IDC_EDIT_FLEFT: focusedEditStatic = IDC_FL_STATIC; groupStatic = IDC_FGB_STATIC; break;
		case IDC_EDIT_FMIDDLE: focusedEditStatic = IDC_FM_STATIC; groupStatic = IDC_FGB_STATIC; break;
		case IDC_EDIT_FRIGHT: focusedEditStatic = IDC_FR_STATIC; groupStatic = IDC_FGB_STATIC; break;
		default: return;
	}

	setItemText(IDC_EDIT_VIEWPANEL, getItemText(_focusedEditCtrl));

	wstring title = getItemText(groupStatic);
	title += L" ";
	title += getItemText(focusedEditStatic);
	title = purgeMenuItemString(title.c_str()); // use purgeMenuItemString to clean '&'
	setItemText(IDC_WHICHPART_STATIC, title);
}

void PrintSubDlg::onCommand(int id, int notification)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	auto& ps = nppGUI._printSettings;

	if (notification == RcNotifyEditChange && id != IDC_EDIT_VIEWPANEL)
	{
		switch (id)
		{
			case IDC_EDIT_ML: ps._marge.left = static_cast<int>(getItemInt(IDC_EDIT_ML)); return;
			case IDC_EDIT_MR: ps._marge.right = static_cast<int>(getItemInt(IDC_EDIT_MR)); return;
			case IDC_EDIT_MT: ps._marge.top = static_cast<int>(getItemInt(IDC_EDIT_MT)); return;
			case IDC_EDIT_MB: ps._marge.bottom = static_cast<int>(getItemInt(IDC_EDIT_MB)); return;
			default:
				break;
		}

		wstring* part = nullptr;
		switch (id)
		{
			case IDC_EDIT_HLEFT: part = &ps._headerLeft; break;
			case IDC_EDIT_HMIDDLE: part = &ps._headerMiddle; break;
			case IDC_EDIT_HRIGHT: part = &ps._headerRight; break;
			case IDC_EDIT_FLEFT: part = &ps._footerLeft; break;
			case IDC_EDIT_FMIDDLE: part = &ps._footerMiddle; break;
			case IDC_EDIT_FRIGHT: part = &ps._footerRight; break;
			default: return;
		}
		_focusedEditCtrl = id;
		const wstring str = getItemText(id);
		setItemText(IDC_EDIT_VIEWPANEL, str);
		*part = str;
		trim(*part);
		return;
	}

	if (notification == RcNotifySelChange)
	{
		QComboBox* combo = comboBox(id);
		if (!combo)
			return;
		switch (id)
		{
			case IDC_COMBO_HFONTNAME:
				ps._headerFontName = combo->currentText().toStdWString();
				return;
			case IDC_COMBO_FFONTNAME:
				ps._footerFontName = combo->currentText().toStdWString();
				return;
			case IDC_COMBO_HFONTSIZE:
				ps._headerFontSize = combo->currentText().toInt();
				return;
			case IDC_COMBO_FFONTSIZE:
				ps._footerFontSize = combo->currentText().toInt();
				return;
			default:
				return;
		}
	}

	switch (id)
	{
		case IDC_CHECK_PRINTLINENUM: ps._printLineNumber = isCheckedOrNot(IDC_CHECK_PRINTLINENUM); return;
		case IDC_CHECK_PRINTFORMFEED: ps._printFormFeedPageBreak = isCheckedOrNot(IDC_CHECK_PRINTFORMFEED); return;
		case IDC_RADIO_WYSIWYG: ps._printOption = SC_PRINT_NORMAL; return;
		case IDC_RADIO_INVERT: ps._printOption = SC_PRINT_INVERTLIGHT; return;
		case IDC_RADIO_BW: ps._printOption = SC_PRINT_BLACKONWHITE; return;
		case IDC_RADIO_NOBG: ps._printOption = SC_PRINT_COLOURONWHITE; return;
		case IDC_CHECK_HBOLD: ps._headerFontStyle ^= FONTSTYLE_BOLD; return;
		case IDC_CHECK_HITALIC: ps._headerFontStyle ^= FONTSTYLE_ITALIC; return;
		case IDC_CHECK_FBOLD: ps._footerFontStyle ^= FONTSTYLE_BOLD; return;
		case IDC_CHECK_FITALIC: ps._footerFontStyle ^= FONTSTYLE_ITALIC; return;

		case IDC_BUTTON_ADDVAR:
		{
			if (!_focusedEditCtrl)
				return;
			const int iSel = comboBox(IDC_COMBO_VARLIST)->currentIndex();
			if (iSel < 0 || static_cast<size_t>(iSel) >= varList.size())
				return;

			// the variable replaces the selection of the last focused edit
			if (auto* edit = qobject_cast<QLineEdit*>(dlgItem(_focusedEditCtrl)))
			{
				edit->insert(QString::fromStdWString(varList[iSel]));
				edit->setFocus();
			}
			return;
		}

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Searching

void SearchingSubDlg::initDialog()
{
	NppParameters& nppParams = NppParameters::getInstance();
	NppGUI& nppGUI = nppParams.getNppGUI();

	setChecked(IDC_CHECK_FILL_FIND_FIELD_WITH_SELECTED, nppGUI._fillFindFieldWithSelected);
	setChecked(IDC_CHECK_FILL_FIND_FIELD_SELECT_CARET, nppGUI._fillFindFieldSelectCaret);
	setChecked(IDC_CHECK_MONOSPACEDFONT_FINDDLG, nppGUI._monospacedFontFindDlg);
	setChecked(IDC_CHECK_FINDDLG_ALWAYS_VISIBLE, nppGUI._findDlgAlwaysVisible);
	setItemInt(IDC_FILLFINDWHAT_THRESHOLD_EDIT, nppGUI._fillFindWhatThreshold);
	for (int ctrl : { IDC_FILLFINDWHAT_THRESH_QUESTION_BUTTON, IDC_FILLFINDWHAT_THRESHOLD_EDIT, IDC_FILLFINDWHAT_THRESHOLD_STATIC, IDC_CHECK_FILL_FIND_FIELD_SELECT_CARET })
		enableItem(ctrl, nppGUI._fillFindFieldWithSelected);
	setChecked(IDC_CHECK_SHOWONCEPERFOUNDLINE, nppGUI._finderShowOnlyOneEntryPerFoundLine);
	setChecked(IDC_CHECK_FIF_IGNOREOPENEDFILES, nppGUI._fif_ignoreunsavedChangesInOpenedFiles);
	setItemInt(IDC_INSELECTION_THRESHOLD_EDIT, nppGUI._inSelectionAutocheckThreshold);
	setChecked(IDC_CHECK_FILL_DIR_FIELD_FROM_ACTIVE_DOC, nppGUI._fillDirFieldFromActiveDoc);

	NativeLangSpeaker* pNativeSpeaker = nppParams.getNativeLangSpeaker();
	wstring tipInSelectionText = pNativeSpeaker->getLocalizedStrFromID("searchingInSelThresh-tip", L"Minimum number of selected characters in edit zone to automatically check the \"In selection\" checkbox when the Find dialog is activated. The maximum value is $INT_REPLACE$. Set the value to 0 to disable auto-checking.");
	tipInSelectionText = stringReplace(tipInSelectionText, L"$INT_REPLACE$", std::to_wstring(FINDREPLACE_INSELECTION_THRESHOLD_DEFAULT));
	setTip(IDC_INSELECTION_THRESH_QUESTION_BUTTON, tipInSelectionText);

	wstring tipFillFindWhatText = pNativeSpeaker->getLocalizedStrFromID("searchingFillFindWhat-tip", L"Maximum number of selected characters in edit zone to fill automatically the \"Find what\" field when the Ctrl-F is triggered. The maximum value is $INT_REPLACE$, which is the maximum size of \"Find what\" field, limited by the system.");
	tipFillFindWhatText = stringReplace(tipFillFindWhatText, L"$INT_REPLACE$", std::to_wstring(FINDREPLACE_MAXLENGTH - 1));
	setTip(IDC_FILLFINDWHAT_THRESH_QUESTION_BUTTON, tipFillFindWhatText);
}

void SearchingSubDlg::onCommand(int id, int notification)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	if (notification == RcNotifyEditChange)
	{
		if (id == IDC_INSELECTION_THRESHOLD_EDIT)
		{
			if (getItemText(IDC_INSELECTION_THRESHOLD_EDIT).empty())
				setItemInt(IDC_INSELECTION_THRESHOLD_EDIT, FINDREPLACE_INSELECTION_THRESHOLD_DEFAULT);

			unsigned int newValue = getItemInt(IDC_INSELECTION_THRESHOLD_EDIT);
			if (static_cast<int>(newValue) == nppGUI._inSelectionAutocheckThreshold)
				return;
			if (newValue > FINDREPLACE_INSELECTION_THRESHOLD_DEFAULT)
			{
				setItemInt(IDC_INSELECTION_THRESHOLD_EDIT, FINDREPLACE_INSELECTION_THRESHOLD_DEFAULT);
				newValue = FINDREPLACE_INSELECTION_THRESHOLD_DEFAULT;
			}
			nppGUI._inSelectionAutocheckThreshold = newValue;
		}
		else if (id == IDC_FILLFINDWHAT_THRESHOLD_EDIT)
		{
			if (getItemText(IDC_FILLFINDWHAT_THRESHOLD_EDIT).empty())
				setItemInt(IDC_FILLFINDWHAT_THRESHOLD_EDIT, FILL_FINDWHAT_THRESHOLD_DEFAULT);

			unsigned int newValue = getItemInt(IDC_FILLFINDWHAT_THRESHOLD_EDIT);
			if (static_cast<int>(newValue) == nppGUI._fillFindWhatThreshold)
				return;
			if (newValue >= FINDREPLACE_MAXLENGTH)
			{
				newValue = FINDREPLACE_MAXLENGTH - 1;
				setItemInt(IDC_FILLFINDWHAT_THRESHOLD_EDIT, newValue);
			}
			if (newValue == 0)
			{
				newValue = 1;
				setItemInt(IDC_FILLFINDWHAT_THRESHOLD_EDIT, newValue);
			}
			nppGUI._fillFindWhatThreshold = newValue;
		}
		return;
	}

	switch (id)
	{
		case IDC_CHECK_FILL_FIND_FIELD_WITH_SELECTED:
			nppGUI._fillFindFieldWithSelected = isCheckedOrNot(IDC_CHECK_FILL_FIND_FIELD_WITH_SELECTED);
			for (int ctrl : { IDC_CHECK_FILL_FIND_FIELD_SELECT_CARET, IDC_FILLFINDWHAT_THRESH_QUESTION_BUTTON, IDC_FILLFINDWHAT_THRESHOLD_EDIT, IDC_FILLFINDWHAT_THRESHOLD_STATIC })
				enableItem(ctrl, nppGUI._fillFindFieldWithSelected);
			if (!nppGUI._fillFindFieldWithSelected)
			{
				setChecked(IDC_CHECK_FILL_FIND_FIELD_SELECT_CARET, false);
				nppGUI._fillFindFieldSelectCaret = false;
			}
			return;

		case IDC_CHECK_MONOSPACEDFONT_FINDDLG:
			nppGUI._monospacedFontFindDlg = isCheckedOrNot(IDC_CHECK_MONOSPACEDFONT_FINDDLG);
			return;

		case IDC_CHECK_FINDDLG_ALWAYS_VISIBLE:
			nppGUI._findDlgAlwaysVisible = isCheckedOrNot(IDC_CHECK_FINDDLG_ALWAYS_VISIBLE);
			return;

		case IDC_CHECK_SHOWONCEPERFOUNDLINE:
			nppGUI._finderShowOnlyOneEntryPerFoundLine = isCheckedOrNot(IDC_CHECK_SHOWONCEPERFOUNDLINE);
			return;

		case IDC_CHECK_FIF_IGNOREOPENEDFILES:
			nppGUI._fif_ignoreunsavedChangesInOpenedFiles = isCheckedOrNot(IDC_CHECK_FIF_IGNOREOPENEDFILES);
			return;

		case IDC_CHECK_FILL_FIND_FIELD_SELECT_CARET:
			nppGUI._fillFindFieldSelectCaret = isCheckedOrNot(IDC_CHECK_FILL_FIND_FIELD_SELECT_CARET);
			return;

		case IDC_CHECK_FILL_DIR_FIELD_FROM_ACTIVE_DOC:
			nppGUI._fillDirFieldFromActiveDoc = isCheckedOrNot(IDC_CHECK_FILL_DIR_FIELD_FROM_ACTIVE_DOC);
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Backup

void BackupSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	setChecked(IDC_CHECK_REMEMBERSESSION, nppGUI._rememberLastSession);
	bool snapshotCheck = nppGUI._rememberLastSession && nppGUI.isSnapshotMode();
	setChecked(IDC_BACKUPDIR_RESTORESESSION_CHECK, snapshotCheck);
	setItemInt(IDC_BACKUPDIR_RESTORESESSION_EDIT, static_cast<int>(nppGUI._snapshotBackupTiming / 1000));
	wstring backupFilePath = nppParam.getUserPath();
	pathAppend(backupFilePath, L"backup");
	backupFilePath += L"/";
	setItemText(IDD_BACKUPDIR_RESTORESESSION_PATH_EDIT, backupFilePath);
	setChecked(IDC_CHECK_KEEPABSENTFILESINSESSION, nppGUI._keepSessionAbsentFileEntries);

	int ID2CheckBackupOnSave = 0;
	switch (nppGUI._backup)
	{
		case bak_simple: ID2CheckBackupOnSave = IDC_RADIO_BKSIMPLE; break;
		case bak_verbose: ID2CheckBackupOnSave = IDC_RADIO_BKVERBOSE; break;
		default: ID2CheckBackupOnSave = IDC_RADIO_BKNONE; // bak_none
	}
	setChecked(ID2CheckBackupOnSave);

	setChecked(IDC_BACKUPDIR_CHECK, nppGUI._useDir);
	setItemText(IDC_BACKUPDIR_EDIT, nppGUI._backupDir);

	updateBackupSessionGUI();
	updateBackupOnSaveGUI();
}

void BackupSubDlg::updateBackupSessionGUI()
{
	bool rememberSession = isCheckedOrNot(IDC_CHECK_REMEMBERSESSION);
	bool isSnapshot = isCheckedOrNot(IDC_BACKUPDIR_RESTORESESSION_CHECK);
	enableItem(IDC_BACKUPDIR_RESTORESESSION_CHECK, rememberSession);
	for (int ctrl : { IDC_BACKUPDIR_RESTORESESSION_EDIT, IDD_BACKUPDIR_RESTORESESSION_PATH_EDIT, IDD_BACKUPDIR_RESTORESESSION_STATIC1,
		IDD_BACKUPDIR_RESTORESESSION_STATIC2, IDD_BACKUPDIR_RESTORESESSION_PATHLABEL_STATIC })
		enableItem(ctrl, isSnapshot);
}

void BackupSubDlg::updateBackupOnSaveGUI()
{
	bool noBackup = isCheckedOrNot(IDC_RADIO_BKNONE);
	bool isEnableGlobableCheck = false;
	bool isEnableLocalCheck = false;
	if (!noBackup)
	{
		isEnableGlobableCheck = true;
		isEnableLocalCheck = isCheckedOrNot(IDC_BACKUPDIR_CHECK);
	}
	enableItem(IDC_BACKUPDIR_CHECK, isEnableGlobableCheck);
	enableItem(IDC_BACKUPDIR_EDIT, isEnableLocalCheck);
	enableItem(IDD_BACKUPDIR_BROWSE_BUTTON, isEnableLocalCheck);
	enableItem(IDD_BACKUPDIR_STATIC, isEnableLocalCheck);
	enableItem(IDC_BACKUPDIR_USERCUSTOMDIR_GRPSTATIC, !noBackup);
}

void BackupSubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	if (notification == RcNotifyEditChange)
	{
		if (id == IDC_BACKUPDIR_EDIT)
		{
			nppGUI._backupDir = getItemText(IDC_BACKUPDIR_EDIT);
		}
		else if (id == IDC_BACKUPDIR_RESTORESESSION_EDIT)
		{
			if (getItemText(IDC_BACKUPDIR_RESTORESESSION_EDIT).empty())
				return;
			nppGUI._snapshotBackupTiming = static_cast<size_t>(getItemInt(IDC_BACKUPDIR_RESTORESESSION_EDIT)) * 1000;
			if (!nppGUI._snapshotBackupTiming)
			{
				nppGUI._snapshotBackupTiming = 1000;
				setItemInt(IDC_BACKUPDIR_RESTORESESSION_EDIT, 1);
			}
			// NPLX: the backup timer follows the new period
			if (nppGUI._isSnapshotMode)
				sendToNpp(NPPM_INTERNAL_ENABLESNAPSHOT);
		}
		return;
	}
	if (notification == RcNotifyKillFocus)
	{
		if (id == IDC_BACKUPDIR_RESTORESESSION_EDIT && getItemText(IDC_BACKUPDIR_RESTORESESSION_EDIT).empty())
			setItemInt(IDC_BACKUPDIR_RESTORESESSION_EDIT, static_cast<int>(nppGUI._snapshotBackupTiming / 1000));
		return;
	}

	switch (id)
	{
		case IDC_CHECK_REMEMBERSESSION:
			nppGUI._rememberLastSession = isCheckedOrNot(IDC_CHECK_REMEMBERSESSION);
			if (!nppGUI._rememberLastSession)
			{
				setChecked(IDC_BACKUPDIR_RESTORESESSION_CHECK, false);
				onCommand(IDC_BACKUPDIR_RESTORESESSION_CHECK, RcNotifyClicked);
			}
			updateBackupSessionGUI();
			return;

		case IDC_CHECK_KEEPABSENTFILESINSESSION:
			nppGUI._keepSessionAbsentFileEntries = isCheckedOrNot(IDC_CHECK_KEEPABSENTFILESINSESSION);
			return;

		case IDC_BACKUPDIR_RESTORESESSION_CHECK:
			nppGUI._isSnapshotMode = isCheckedOrNot(IDC_BACKUPDIR_RESTORESESSION_CHECK);
			updateBackupSessionGUI();
			// Launch thread (NPLX: the backup timer; it is stopped when the snapshot mode is off)
			sendToNpp(NPPM_INTERNAL_ENABLESNAPSHOT);
			return;

		case IDC_RADIO_BKSIMPLE:
			nppGUI._backup = bak_simple;
			updateBackupOnSaveGUI();
			return;

		case IDC_RADIO_BKVERBOSE:
			nppGUI._backup = bak_verbose;
			updateBackupOnSaveGUI();
			return;

		case IDC_RADIO_BKNONE:
			nppGUI._backup = bak_none;
			updateBackupOnSaveGUI();
			return;

		case IDC_BACKUPDIR_CHECK:
			nppGUI._useDir = !nppGUI._useDir;
			updateBackupOnSaveGUI();
			return;

		case IDD_BACKUPDIR_BROWSE_BUTTON:
		{
			const wstring title = nppParam.getNativeLangSpeaker()->getLocalizedStrFromID("backup-select-folder",
				L"Select a folder as backup directory");
			const wstring dir = nplx::uiServices() ? nplx::uiServices()->getExistingDirectory(title) : wstring();
			if (!dir.empty())
			{
				setItemText(IDC_BACKUPDIR_EDIT, dir);
				onCommand(IDC_BACKUPDIR_EDIT, RcNotifyEditChange);
			}
			return;
		}

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Auto-Completion

void AutoCompletionSubDlg::enableAutoCompletionCtrls(bool isEnableAutoC)
{
	for (int ctrl : { IDD_AUTOC_FUNCRADIO, IDD_AUTOC_WORDRADIO, IDD_AUTOC_BOTHRADIO, IDD_AUTOC_BRIEF_CHECK, IDD_AUTOC_USEKEY_GRP_STATIC,
		IDD_AUTOC_USEENTER, IDD_AUTOC_USETAB, IDD_AUTOC_IGNORENUMBERS, IDC_AUTOC_CHAR_SLIDER, IDD_AUTOC_STATIC_FROM, IDD_AUTOC_STATIC_N,
		IDD_AUTOC_STATIC_CHAR, IDD_AUTOC_SLIDER_MIN_STATIC, IDD_AUTOC_SLIDER_MAX_STATIC })
		enableItem(ctrl, isEnableAutoC);
}

void AutoCompletionSubDlg::initDialog()
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	setItemInt(IDD_AUTOC_STATIC_N, static_cast<int>(nppGUI._autocFromLen));
	setSliderRange(this, IDC_AUTOC_CHAR_SLIDER, AUTOCOMPLETEFROMCHAR_SMALLEST, AUTOCOMPLETEFROMCHAR_LARGEST, AUTOCOMPLETEFROMCHAR_INTERVAL, static_cast<int>(nppGUI._autocFromLen));

	bool isEnableAutoC = nppGUI._autocStatus != nppGUI.autoc_none;
	setChecked(IDD_AUTOC_ENABLECHECK, isEnableAutoC);

	int selectedID = IDD_AUTOC_BOTHRADIO;
	if (nppGUI._autocStatus == nppGUI.autoc_func)
		selectedID = IDD_AUTOC_FUNCRADIO;
	else if (nppGUI._autocStatus == nppGUI.autoc_word)
		selectedID = IDD_AUTOC_WORDRADIO;
	else if (nppGUI._autocStatus == nppGUI.autoc_both)
		selectedID = IDD_AUTOC_BOTHRADIO;
	setChecked(selectedID);

	if (nppGUI._autocStatus == nppGUI.autoc_word || nppGUI._autocStatus == nppGUI.autoc_both)
		setChecked(IDD_AUTOC_IGNORENUMBERS, nppGUI._autocIgnoreNumbers);

	setChecked(IDD_AUTOC_USEENTER, nppGUI._autocInsertSelectedUseENTER);
	setChecked(IDD_AUTOC_USETAB, nppGUI._autocInsertSelectedUseTAB);

	if (!isEnableAutoC)
		enableAutoCompletionCtrls(false);

	setChecked(IDD_AUTOC_BRIEF_CHECK, nppGUI._autocBrief);
	setChecked(IDD_FUNC_CHECK, nppGUI._funcParams);

	const auto& mpc = nppGUI._matchedPairConf;
	setChecked(IDD_AUTOCPARENTHESES_CHECK, mpc._doParentheses);
	if (mpc._doParentheses)
		setItemText(IDD_AUTOCPARENTHESES_CHECK, L" (  )");
	setChecked(IDD_AUTOCBRACKET_CHECK, mpc._doBrackets);
	if (mpc._doBrackets)
		setItemText(IDD_AUTOCBRACKET_CHECK, L" [  ]");
	setChecked(IDD_AUTOCCURLYBRACKET_CHECK, mpc._doCurlyBrackets);
	if (mpc._doCurlyBrackets)
		setItemText(IDD_AUTOCCURLYBRACKET_CHECK, L" {  }");
	setChecked(IDD_AUTOC_QUOTESCHECK, mpc._doQuotes);
	if (mpc._doQuotes)
		setItemText(IDD_AUTOC_QUOTESCHECK, L" '  '");
	setChecked(IDD_AUTOC_DOUBLEQUOTESCHECK, mpc._doDoubleQuotes);
	if (mpc._doDoubleQuotes)
		setItemText(IDD_AUTOC_DOUBLEQUOTESCHECK, L" \"  \"");
	setChecked(IDD_AUTOCTAG_CHECK, mpc._doHtmlXmlTag);

	const int pairEdits[3][2] = {
		{ IDC_MACHEDPAIROPEN_EDIT1, IDC_MACHEDPAIRCLOSE_EDIT1 },
		{ IDC_MACHEDPAIROPEN_EDIT2, IDC_MACHEDPAIRCLOSE_EDIT2 },
		{ IDC_MACHEDPAIROPEN_EDIT3, IDC_MACHEDPAIRCLOSE_EDIT3 },
	};
	for (const auto& edits : pairEdits)
		for (int edit : edits)
			if (auto* lineEdit = qobject_cast<QLineEdit*>(dlgItem(edit)))
				lineEdit->setMaxLength(1); // EM_LIMITTEXT

	const size_t nbMatchedPair = std::min<size_t>(mpc._matchedPairs.size(), 3);
	for (size_t i = 0; i < nbMatchedPair; ++i)
	{
		setItemText(pairEdits[i][0], wstring(1, static_cast<wchar_t>(mpc._matchedPairs[i].first)));
		setItemText(pairEdits[i][1], wstring(1, static_cast<wchar_t>(mpc._matchedPairs[i].second)));
	}
}

void AutoCompletionSubDlg::onSliderChanged(int id, int value)
{
	if (id == IDC_AUTOC_CHAR_SLIDER)
	{
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		nppGUI._autocFromLen = static_cast<UINT>(value);
		setItemInt(IDD_AUTOC_STATIC_N, value);
	}
}

void AutoCompletionSubDlg::onCommand(int id, int notification)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	if (notification == RcNotifyEditChange)
	{
		switch (id)
		{
			case IDC_MACHEDPAIROPEN_EDIT1:
			case IDC_MACHEDPAIRCLOSE_EDIT1:
			case IDC_MACHEDPAIROPEN_EDIT2:
			case IDC_MACHEDPAIRCLOSE_EDIT2:
			case IDC_MACHEDPAIROPEN_EDIT3:
			case IDC_MACHEDPAIRCLOSE_EDIT3:
			{
				nppGUI._matchedPairConf._matchedPairs.clear();
				const int pairEdits[3][2] = {
					{ IDC_MACHEDPAIROPEN_EDIT1, IDC_MACHEDPAIRCLOSE_EDIT1 },
					{ IDC_MACHEDPAIROPEN_EDIT2, IDC_MACHEDPAIRCLOSE_EDIT2 },
					{ IDC_MACHEDPAIROPEN_EDIT3, IDC_MACHEDPAIRCLOSE_EDIT3 },
				};
				for (const auto& edits : pairEdits)
				{
					const wstring opener = getItemText(edits[0]);
					const wstring closer = getItemText(edits[1]);
					if (!opener.empty() && opener[0] < 0x80 && !closer.empty() && closer[0] < 0x80)
						nppGUI._matchedPairConf._matchedPairs.push_back(pair<char, char>(static_cast<char>(opener[0]), static_cast<char>(closer[0])));
				}
				return;
			}
			default:
				return;
		}
	}

	switch (id)
	{
		case IDD_AUTOC_ENABLECHECK:
		{
			bool isEnableAutoC = isCheckedOrNot(IDD_AUTOC_ENABLECHECK);

			if (isEnableAutoC)
			{
				setChecked(IDD_AUTOC_BOTHRADIO, true);
				nppGUI._autocStatus = nppGUI.autoc_both;
				setChecked(IDD_AUTOC_IGNORENUMBERS, false);
				nppGUI._autocIgnoreNumbers = false;
			}
			else
			{
				setChecked(IDD_AUTOC_FUNCRADIO, false);
				setChecked(IDD_AUTOC_WORDRADIO, false);
				setChecked(IDD_AUTOC_BOTHRADIO, false);
				setChecked(IDD_AUTOC_BRIEF_CHECK, false);
				nppGUI._autocBrief = false;
				nppGUI._autocStatus = nppGUI.autoc_none;
				setChecked(IDD_AUTOC_IGNORENUMBERS, false);
				nppGUI._autocIgnoreNumbers = false;
			}
			enableAutoCompletionCtrls(isEnableAutoC);
			return;
		}

		case IDD_AUTOC_FUNCRADIO:
			nppGUI._autocStatus = nppGUI.autoc_func;
			setChecked(IDD_AUTOC_IGNORENUMBERS, false);
			enableItem(IDD_AUTOC_IGNORENUMBERS, false);
			nppGUI._autocIgnoreNumbers = false;
			setChecked(IDD_AUTOC_BRIEF_CHECK, false);
			nppGUI._autocBrief = false;
			return;

		case IDD_AUTOC_WORDRADIO:
			nppGUI._autocStatus = nppGUI.autoc_word;
			enableItem(IDD_AUTOC_IGNORENUMBERS, true);
			return;

		case IDD_AUTOC_BOTHRADIO:
			nppGUI._autocStatus = nppGUI.autoc_both;
			enableItem(IDD_AUTOC_IGNORENUMBERS, true);
			return;

		case IDD_AUTOC_USEENTER:
			nppGUI._autocInsertSelectedUseENTER = isCheckedOrNot(id);
			return;

		case IDD_AUTOC_USETAB:
			nppGUI._autocInsertSelectedUseTAB = isCheckedOrNot(id);
			return;

		case IDD_AUTOC_IGNORENUMBERS:
			nppGUI._autocIgnoreNumbers = isCheckedOrNot(id);
			return;

		case IDD_AUTOC_BRIEF_CHECK:
			nppGUI._autocBrief = isCheckedOrNot(id);
			return;

		case IDD_FUNC_CHECK:
			nppGUI._funcParams = isCheckedOrNot(id);
			return;

		case IDD_AUTOCPARENTHESES_CHECK:
		case IDD_AUTOCBRACKET_CHECK:
		case IDD_AUTOCCURLYBRACKET_CHECK:
		case IDD_AUTOC_DOUBLEQUOTESCHECK:
		case IDD_AUTOC_QUOTESCHECK:
		{
			const bool isChecked = isCheckedOrNot(id);
			const wchar_t* label = nullptr;
			auto& mpc = nppGUI._matchedPairConf;
			if (id == IDD_AUTOCPARENTHESES_CHECK)
			{
				mpc._doParentheses = isChecked;
				label = isChecked ? L" (  )" : L" (";
			}
			else if (id == IDD_AUTOCBRACKET_CHECK)
			{
				mpc._doBrackets = isChecked;
				label = isChecked ? L" [  ]" : L" [";
			}
			else if (id == IDD_AUTOCCURLYBRACKET_CHECK)
			{
				mpc._doCurlyBrackets = isChecked;
				label = isChecked ? L" {  }" : L" {";
			}
			else if (id == IDD_AUTOC_DOUBLEQUOTESCHECK)
			{
				mpc._doDoubleQuotes = isChecked;
				label = isChecked ? L" \"  \"" : L" \"";
			}
			else // if (id == IDD_AUTOC_QUOTESCHECK)
			{
				mpc._doQuotes = isChecked;
				label = isChecked ? L" '  '" : L" '";
			}
			setItemText(id, label);
			return;
		}

		case IDD_AUTOCTAG_CHECK:
			nppGUI._matchedPairConf._doHtmlXmlTag = isCheckedOrNot(IDD_AUTOCTAG_CHECK);
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Multi-Instance & Date

namespace
{
	// upstream MultiInstanceSubDlg::_BTTF_time: 1985-10-26 16:24:42, a Saturday (NPLX: std::tm for SYSTEMTIME)
	std::tm bttfTime()
	{
		std::tm t{};
		t.tm_year = 1985 - 1900;
		t.tm_mon = 10 - 1;
		t.tm_wday = 6;
		t.tm_mday = 26;
		t.tm_hour = 16;
		t.tm_min = 24;
		t.tm_sec = 42;
		return t;
	}
}

void MultiInstanceSubDlg::initDialog()
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	setChecked(IDC_CHECK_CLIPBOARDHISTORY, nppGUI._clipboardHistoryPanelKeepState);
	setChecked(IDC_CHECK_DOCLIST, nppGUI._docListKeepState);
	setChecked(IDC_CHECK_CHARPANEL, nppGUI._charPanelKeepState);
	setChecked(IDC_CHECK_FILEBROWSER, nppGUI._fileBrowserKeepState);
	setChecked(IDC_CHECK_PROJECTPANEL, nppGUI._projectPanelKeepState);
	setChecked(IDC_CHECK_DOCMAP, nppGUI._docMapKeepState);
	setChecked(IDC_CHECK_FUNCLIST, nppGUI._funcListKeepState);
	setChecked(IDC_CHECK_PLUGINPANEL, nppGUI._pluginPanelKeepState);

	MultiInstSetting multiInstSetting = nppGUI._multiInstSetting;
	setChecked(IDC_SESSIONININST_RADIO, multiInstSetting == multiInstOnSession);
	setChecked(IDC_MULTIINST_RADIO, multiInstSetting == multiInst);
	setChecked(IDC_MONOINST_RADIO, multiInstSetting == monoInst);

	setChecked(IDD_DATETIMEFORMAT_REVERSEORDER_CHECK, nppGUI._dateTimeReverseDefaultOrder);

	setItemText(IDC_DATETIMEFORMAT_EDIT, nppGUI._dateTimeFormat);
	setItemText(IDD_DATETIMEFORMAT_RESULT_STATIC, getDateTimeStrFrom(nppGUI._dateTimeFormat, bttfTime()));
}

void MultiInstanceSubDlg::onCommand(int id, int notification)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	if (notification == RcNotifyEditChange && id == IDC_DATETIMEFORMAT_EDIT)
	{
		nppGUI._dateTimeFormat = getItemText(IDC_DATETIMEFORMAT_EDIT);
		setItemText(IDD_DATETIMEFORMAT_RESULT_STATIC, getDateTimeStrFrom(nppGUI._dateTimeFormat, bttfTime()));
		return;
	}

	switch (id)
	{
		case IDC_SESSIONININST_RADIO: nppGUI._multiInstSetting = multiInstOnSession; return;
		case IDC_MULTIINST_RADIO: nppGUI._multiInstSetting = multiInst; return;
		case IDC_MONOINST_RADIO: nppGUI._multiInstSetting = monoInst; return;
		case IDD_DATETIMEFORMAT_REVERSEORDER_CHECK: nppGUI._dateTimeReverseDefaultOrder = isCheckedOrNot(id); return;
		case IDC_CHECK_CLIPBOARDHISTORY: nppGUI._clipboardHistoryPanelKeepState = isCheckedOrNot(id); return;
		case IDC_CHECK_DOCLIST: nppGUI._docListKeepState = isCheckedOrNot(id); return;
		case IDC_CHECK_CHARPANEL: nppGUI._charPanelKeepState = isCheckedOrNot(id); return;
		case IDC_CHECK_FILEBROWSER: nppGUI._fileBrowserKeepState = isCheckedOrNot(id); return;
		case IDC_CHECK_PROJECTPANEL: nppGUI._projectPanelKeepState = isCheckedOrNot(id); return;
		case IDC_CHECK_DOCMAP: nppGUI._docMapKeepState = isCheckedOrNot(id); return;
		case IDC_CHECK_FUNCLIST: nppGUI._funcListKeepState = isCheckedOrNot(id); return;
		case IDC_CHECK_PLUGINPANEL: nppGUI._pluginPanelKeepState = isCheckedOrNot(id); return;
		default: return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Delimiter

void DelimiterSubDlg::detectSpace(const char* text2Check, int& nbSp, int& nbTab) const
{
	nbSp = nbTab = 0;
	for (size_t i = 0; i < strlen(text2Check); ++i)
	{
		if (text2Check[i] == ' ')
			++nbSp;
		else if (text2Check[i] == '\t')
			++nbTab;
	}
}

wstring DelimiterSubDlg::getWarningText(size_t nbSp, size_t nbTab) const
{
	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();

	wstring msg;
	if (nbSp && nbTab)
	{
		wstring nbSpStr = std::to_wstring(nbSp);
		wstring nbTabStr = std::to_wstring(nbTab);
		wstring warnBegin = pNativeSpeaker->getLocalizedStrFromID("word-chars-list-warning-begin", L"");
		wstring space = pNativeSpeaker->getLocalizedStrFromID("word-chars-list-space-warning", L"");
		wstring tab = pNativeSpeaker->getLocalizedStrFromID("word-chars-list-tab-warning", L"");
		wstring warnEnd = pNativeSpeaker->getLocalizedStrFromID("word-chars-list-warning-end", L"");

		// half translation is not allowed
		if (!warnBegin.empty() && !space.empty() && !tab.empty() && !warnEnd.empty())
		{
			space = stringReplace(space, L"$INT_REPLACE$", nbSpStr);
			tab = stringReplace(tab, L"$INT_REPLACE$", nbTabStr);
			msg = warnBegin + space + L" && " + tab + warnEnd;
		}
		else
		{
			msg = L"Be aware: " + nbSpStr + L" space(s) && " + nbTabStr + L" TAB(s) in your character list.";
		}
	}
	else if (nbSp && !nbTab)
	{
		wstring nbSpStr = std::to_wstring(nbSp);
		wstring warnBegin = pNativeSpeaker->getLocalizedStrFromID("word-chars-list-warning-begin", L"");
		wstring space = pNativeSpeaker->getLocalizedStrFromID("word-chars-list-space-warning", L"");
		wstring warnEnd = pNativeSpeaker->getLocalizedStrFromID("word-chars-list-warning-end", L"");

		// half translation is not allowed
		if (!warnBegin.empty() && !space.empty() && !warnEnd.empty())
		{
			space = stringReplace(space, L"$INT_REPLACE$", nbSpStr);
			msg = warnBegin + space + warnEnd;
		}
		else
		{
			msg = L"Be aware: " + nbSpStr + L" space(s) in your character list.";
		}
	}
	else if (!nbSp && nbTab)
	{
		wstring nbTabStr = std::to_wstring(nbTab);
		wstring warnBegin = pNativeSpeaker->getLocalizedStrFromID("word-chars-list-warning-begin", L"");
		wstring tab = pNativeSpeaker->getLocalizedStrFromID("word-chars-list-tab-warning", L"");
		wstring warnEnd = pNativeSpeaker->getLocalizedStrFromID("word-chars-list-warning-end", L"");

		// half translation is not allowed
		if (!warnBegin.empty() && !tab.empty() && !warnEnd.empty())
		{
			tab = stringReplace(tab, L"$INT_REPLACE$", nbTabStr);
			msg = warnBegin + tab + warnEnd;
		}
		else
		{
			msg = L"Be aware: " + nbTabStr + L" TAB(s) in your character list.";
		}
	}
	// else (!nbSp && !nbTab): do nothing

	// NPLX: "&&" is the Win32 static text escape of "&"
	return stringReplace(msg, L"&&", L"&");
}

void DelimiterSubDlg::setWarningIfNeed() const
{
	wstring msg;
	NppGUI& nppGUI = (NppParameters::getInstance()).getNppGUI();
	if (!nppGUI._isWordCharDefault)
	{
		int nbSp = 0;
		int nbTab = 0;
		detectSpace(nppGUI._customWordChars.c_str(), nbSp, nbTab);
		msg = getWarningText(nbSp, nbTab);
	}
	setItemText(IDD_STATIC_WORDCHAR_WARNING, msg);
}

void DelimiterSubDlg::setCtrlsPos(bool isMultiline)
{
	// upstream calcCtrlsPos / setCtrlsPos: "Close" follows the end of the 1st or the 2nd line of the example
	showItem(IDD_STATIC_BLABLA2NDLINE, isMultiline);
	if (isMultiline)
	{
		dlgItem(IDC_EDIT_CLOSEDELIMITER)->move(_closeEditGeometry.topLeft() + QPoint(-_secondLineShift, dlgItem(IDD_STATIC_BLABLA2NDLINE)->y() - dlgItem(IDD_STATIC_BLABLA)->y()));
		dlgItem(IDD_STATIC_CLOSEDELIMITER)->move(_closeTextGeometry.topLeft() + QPoint(-_secondLineShift, dlgItem(IDD_STATIC_BLABLA2NDLINE)->y() - dlgItem(IDD_STATIC_BLABLA)->y()));
	}
	else
	{
		dlgItem(IDC_EDIT_CLOSEDELIMITER)->move(_closeEditGeometry.topLeft());
		dlgItem(IDD_STATIC_CLOSEDELIMITER)->move(_closeTextGeometry.topLeft());
	}
}

void DelimiterSubDlg::initDialog()
{
	NppGUI& nppGUI = (NppParameters::getInstance()).getNppGUI();

	//
	// Delimiter
	//
	bool onSeveralLines = nppGUI._delimiterSelectionOnEntireDocument;

	for (int edit : { IDC_EDIT_OPENDELIMITER, IDC_EDIT_CLOSEDELIMITER })
		if (auto* lineEdit = qobject_cast<QLineEdit*>(dlgItem(edit)))
			lineEdit->setMaxLength(1); // EM_LIMITTEXT
	setItemText(IDC_EDIT_OPENDELIMITER, nppGUI._leftmostDelimiter ? wstring(1, static_cast<wchar_t>(nppGUI._leftmostDelimiter)) : wstring());
	setItemText(IDC_EDIT_CLOSEDELIMITER, nppGUI._rightmostDelimiter ? wstring(1, static_cast<wchar_t>(nppGUI._rightmostDelimiter)) : wstring());
	setChecked(IDD_SEVERALLINEMODEON_CHECK, onSeveralLines);

	// the example text is drawn on a darker background (upstream WM_CTLCOLORSTATIC)
	for (int blabla : { IDD_STATIC_BLABLA, IDD_STATIC_BLABLA2NDLINE })
	{
		auto* label = qobject_cast<QLabel*>(dlgItem(blabla));
		label->setAutoFillBackground(true);
		label->setBackgroundRole(QPalette::Mid);
		label->setForegroundRole(QPalette::Text);
	}

	_closeEditGeometry = dlgItem(IDC_EDIT_CLOSEDELIMITER)->geometry();
	_closeTextGeometry = dlgItem(IDD_STATIC_CLOSEDELIMITER)->geometry();
	_secondLineShift = dlgItem(IDD_STATIC_BLABLA)->geometry().right() - dlgItem(IDD_STATIC_BLABLA2NDLINE)->geometry().right();
	setCtrlsPos(onSeveralLines);

	//
	// Word Char List
	//
	setItemText(IDC_WORDCHAR_CUSTOM_EDIT, string2wstring(nppGUI._customWordChars, CP_UTF8));
	setChecked(IDC_RADIO_WORDCHAR_DEFAULT, nppGUI._isWordCharDefault);
	setChecked(IDC_RADIO_WORDCHAR_CUSTOM, !nppGUI._isWordCharDefault);
	enableItem(IDC_WORDCHAR_CUSTOM_EDIT, !nppGUI._isWordCharDefault);

	setWarningIfNeed();

	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	setTip(IDD_WORDCHAR_QUESTION_BUTTON, pNativeSpeaker->getLocalizedStrFromID("word-chars-list-tip",
		L"This allows you to include additional character into current word characters while double clicking for selection or searching with \"Match whole word only\" option checked."));
}

void DelimiterSubDlg::onCommand(int id, int notification)
{
	NppGUI& nppGUI = (NppParameters::getInstance()).getNppGUI();

	if (notification == RcNotifyEditChange)
	{
		switch (id)
		{
			case IDC_EDIT_OPENDELIMITER:
			{
				const wstring opener = getItemText(IDC_EDIT_OPENDELIMITER);
				nppGUI._leftmostDelimiter = opener.empty() ? '\0' : static_cast<char>(opener[0]);
				return;
			}

			case IDC_EDIT_CLOSEDELIMITER:
			{
				const wstring closer = getItemText(IDC_EDIT_CLOSEDELIMITER);
				nppGUI._rightmostDelimiter = closer.empty() ? '\0' : static_cast<char>(closer[0]);
				return;
			}

			case IDC_WORDCHAR_CUSTOM_EDIT:
				nppGUI._customWordChars = wstring2string(getItemText(IDC_WORDCHAR_CUSTOM_EDIT), CP_UTF8);
				setWarningIfNeed();
				sendToNpp(NPPM_INTERNAL_SETWORDCHARS);
				return;

			default:
				return;
		}
	}

	switch (id)
	{
		case IDD_SEVERALLINEMODEON_CHECK:
		{
			bool isChecked = isCheckedOrNot(IDD_SEVERALLINEMODEON_CHECK);
			nppGUI._delimiterSelectionOnEntireDocument = isChecked;
			setCtrlsPos(isChecked);
			return;
		}

		case IDC_RADIO_WORDCHAR_DEFAULT:
			nppGUI._isWordCharDefault = true;
			sendToNpp(NPPM_INTERNAL_SETWORDCHARS);
			enableItem(IDC_WORDCHAR_CUSTOM_EDIT, !nppGUI._isWordCharDefault);
			setItemText(IDD_STATIC_WORDCHAR_WARNING, L"");
			return;

		case IDC_RADIO_WORDCHAR_CUSTOM:
			nppGUI._isWordCharDefault = false;
			sendToNpp(NPPM_INTERNAL_SETWORDCHARS);
			enableItem(IDC_WORDCHAR_CUSTOM_EDIT, !nppGUI._isWordCharDefault);
			setWarningIfNeed();
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Cloud & Link

void CloudAndLinkSubDlg::initDialog()
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	wstring errMsg;
	bool withCloud = !nppGUI._cloudPath.empty();
	if (withCloud)
	{
		// detect validation of path
		if (!doesDirectoryExist(nppGUI._cloudPath.c_str()))
			errMsg = L"Invalid path";
	}

	setItemText(IDC_SETTINGSONCLOUD_WARNING_STATIC, errMsg);

	setChecked(IDC_NOCLOUD_RADIO, !withCloud);
	setChecked(IDC_WITHCLOUD_RADIO, withCloud);
	setItemText(IDC_CLOUDPATH_EDIT, nppGUI._cloudPath);
	enableItem(IDC_CLOUDPATH_EDIT, withCloud);
	enableItem(IDD_CLOUDPATH_BROWSE_BUTTON, withCloud);

	bool linkEnable = nppGUI._styleURL != urlDisable;
	bool dontUnderline = (nppGUI._styleURL == urlNoUnderLineFg) || (nppGUI._styleURL == urlNoUnderLineBg);
	bool roundBoxMode = (nppGUI._styleURL == urlNoUnderLineBg) || (nppGUI._styleURL == urlUnderLineBg);

	setItemText(IDC_URISCHEMES_EDIT, nppGUI._uriSchemes);
	setChecked(IDC_CHECK_CLICKABLELINK_ENABLE, linkEnable);
	setChecked(IDC_CHECK_CLICKABLELINK_NOUNDERLINE, dontUnderline);
	setChecked(IDC_CHECK_CLICKABLELINK_FULLBOXMODE, roundBoxMode);

	for (int ctrl : { IDC_CHECK_CLICKABLELINK_NOUNDERLINE, IDC_CHECK_CLICKABLELINK_FULLBOXMODE, IDC_URISCHEMES_EDIT, IDC_URISCHEMES_STATIC })
		enableItem(ctrl, linkEnable);
}

void CloudAndLinkSubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParams = NppParameters::getInstance();
	NppGUI& nppGUI = nppParams.getNppGUI();
	NativeLangSpeaker* pNativeSpeaker = nppParams.getNativeLangSpeaker();

	if (notification == RcNotifyEditChange)
	{
		switch (id)
		{
			case IDC_CLOUDPATH_EDIT:
			{
				wstring inputDirExpanded = getItemText(IDC_CLOUDPATH_EDIT);
				expandEnv(inputDirExpanded); // NPLX: ExpandEnvironmentStrings, also $VAR and ~
				if (doesDirectoryExist(inputDirExpanded.c_str()))
				{
					nppGUI._cloudPath = inputDirExpanded;
					nppParams.setCloudChoice(inputDirExpanded.c_str());

					wstring warningMsg;
					if (nppParams.isCloudPathChanged())
						warningMsg = pNativeSpeaker->getLocalizedStrFromID("cloud-restart-warning", L"Please restart NotePadLX to take effect.");
					setItemText(IDC_SETTINGSONCLOUD_WARNING_STATIC, warningMsg);
				}
				else
				{
					if (isCheckedOrNot(IDC_WITHCLOUD_RADIO))
					{
						setItemText(IDC_SETTINGSONCLOUD_WARNING_STATIC, pNativeSpeaker->getLocalizedStrFromID("cloud-invalid-warning", L"Invalid path."));
						nppParams.removeCloudChoice();
					}
				}
				return;
			}

			case IDC_URISCHEMES_EDIT:
				nppGUI._uriSchemes = getItemText(IDC_URISCHEMES_EDIT);
				sendToNpp(NPPM_INTERNAL_UPDATECLICKABLELINKS);
				return;

			default:
				return;
		}
	}

	switch (id)
	{
		case IDC_NOCLOUD_RADIO:
		{
			nppGUI._cloudPath = L"";
			nppParams.removeCloudChoice();

			wstring warningMsg;
			if (nppParams.isCloudPathChanged())
				warningMsg = pNativeSpeaker->getLocalizedStrFromID("cloud-restart-warning", L"Please restart NotePadLX to take effect.");
			// else set empty string
			setItemText(IDC_SETTINGSONCLOUD_WARNING_STATIC, warningMsg);
			setItemText(IDC_CLOUDPATH_EDIT, nppGUI._cloudPath);
			enableItem(IDC_CLOUDPATH_EDIT, false);
			enableItem(IDD_CLOUDPATH_BROWSE_BUTTON, false);
			return;
		}

		case IDC_WITHCLOUD_RADIO:
			setItemText(IDC_SETTINGSONCLOUD_WARNING_STATIC, pNativeSpeaker->getLocalizedStrFromID("cloud-invalid-warning", L"Invalid path."));
			enableItem(IDC_CLOUDPATH_EDIT, true);
			enableItem(IDD_CLOUDPATH_BROWSE_BUTTON, true);
			return;

		case IDD_CLOUDPATH_BROWSE_BUTTON:
		{
			const wstring title = pNativeSpeaker->getLocalizedStrFromID("cloud-select-folder", L"Select a folder from/to where NotePadLX reads/writes its settings");
			const wstring dir = nplx::uiServices() ? nplx::uiServices()->getExistingDirectory(title) : wstring();
			if (!dir.empty())
			{
				setItemText(IDC_CLOUDPATH_EDIT, dir);
				onCommand(IDC_CLOUDPATH_EDIT, RcNotifyEditChange);
			}
			return;
		}

		case IDC_CHECK_CLICKABLELINK_ENABLE:
		{
			bool isChecked = isCheckedOrNot(IDC_CHECK_CLICKABLELINK_ENABLE);
			if (!isChecked)
			{
				setChecked(IDC_CHECK_CLICKABLELINK_NOUNDERLINE, false);
				setChecked(IDC_CHECK_CLICKABLELINK_FULLBOXMODE, false);
			}
			for (int ctrl : { IDC_CHECK_CLICKABLELINK_NOUNDERLINE, IDC_CHECK_CLICKABLELINK_FULLBOXMODE, IDC_URISCHEMES_EDIT, IDC_URISCHEMES_STATIC })
				enableItem(ctrl, isChecked);

			nppGUI._styleURL = isChecked ? urlUnderLineFg : urlDisable;
			sendToNpp(NPPM_INTERNAL_UPDATECLICKABLELINKS, isChecked ? 0 : 1, 0);
			return;
		}

		case IDC_CHECK_CLICKABLELINK_NOUNDERLINE:
		case IDC_CHECK_CLICKABLELINK_FULLBOXMODE:
		{
			bool isNoUnderline = isCheckedOrNot(IDC_CHECK_CLICKABLELINK_NOUNDERLINE);
			bool isRoundBoxMode = isCheckedOrNot(IDC_CHECK_CLICKABLELINK_FULLBOXMODE);
			if (isRoundBoxMode)
				nppGUI._styleURL = isNoUnderline ? urlNoUnderLineBg : urlUnderLineBg;
			else
				nppGUI._styleURL = isNoUnderline ? urlNoUnderLineFg : urlUnderLineFg;
			sendToNpp(NPPM_INTERNAL_UPDATECLICKABLELINKS);
			return;
		}

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Search Engine

void SearchEngineSubDlg::initDialog()
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	if (nppGUI._searchEngineChoice == nppGUI.se_custom)
	{
		if (nppGUI._searchEngineCustom.empty())
			nppGUI._searchEngineChoice = nppGUI.se_google;
	}
	if (nppGUI._searchEngineChoice == nppGUI.se_bing)
		nppGUI._searchEngineChoice = nppGUI.se_duckDuckGo;

	setChecked(IDC_SEARCHENGINE_CUSTOM_RADIO, nppGUI._searchEngineChoice == nppGUI.se_custom);
	setChecked(IDC_SEARCHENGINE_DUCKDUCKGO_RADIO, nppGUI._searchEngineChoice == nppGUI.se_duckDuckGo);
	setChecked(IDC_SEARCHENGINE_GOOGLE_RADIO, nppGUI._searchEngineChoice == nppGUI.se_google);
	setChecked(IDC_SEARCHENGINE_YAHOO_RADIO, nppGUI._searchEngineChoice == nppGUI.se_yahoo);
	setChecked(IDC_SEARCHENGINE_STACKOVERFLOW_RADIO, nppGUI._searchEngineChoice == nppGUI.se_stackoverflow);
	setItemText(IDC_SEARCHENGINE_EDIT, nppGUI._searchEngineCustom);
	enableItem(IDC_SEARCHENGINE_EDIT, nppGUI._searchEngineChoice == nppGUI.se_custom);
}

void SearchEngineSubDlg::onCommand(int id, int notification)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	if (notification == RcNotifyEditChange && id == IDC_SEARCHENGINE_EDIT)
	{
		nppGUI._searchEngineCustom = getItemText(IDC_SEARCHENGINE_EDIT);
		return;
	}

	switch (id)
	{
		case IDC_SEARCHENGINE_DUCKDUCKGO_RADIO:
			nppGUI._searchEngineChoice = nppGUI.se_duckDuckGo;
			enableItem(IDC_SEARCHENGINE_EDIT, false);
			return;

		case IDC_SEARCHENGINE_GOOGLE_RADIO:
			nppGUI._searchEngineChoice = nppGUI.se_google;
			enableItem(IDC_SEARCHENGINE_EDIT, false);
			return;

		case IDC_SEARCHENGINE_YAHOO_RADIO:
			nppGUI._searchEngineChoice = nppGUI.se_yahoo;
			enableItem(IDC_SEARCHENGINE_EDIT, false);
			return;

		case IDC_SEARCHENGINE_STACKOVERFLOW_RADIO:
			nppGUI._searchEngineChoice = nppGUI.se_stackoverflow;
			enableItem(IDC_SEARCHENGINE_EDIT, false);
			return;

		case IDC_SEARCHENGINE_CUSTOM_RADIO:
			nppGUI._searchEngineChoice = nppGUI.se_custom;
			enableItem(IDC_SEARCHENGINE_EDIT, true);
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Performance

void PerformanceSubDlg::enableRestrictionCtrls(bool enable)
{
	for (int ctrl : { IDC_EDIT_PERFORMANCE_FILESIZE, IDC_CHECK_PERFORMANCE_ALLOWBRACEMATCH, IDC_CHECK_PERFORMANCE_ALLOWAUTOCOMPLETION,
		IDC_CHECK_PERFORMANCE_ALLOWSMARTHILITE, IDC_CHECK_PERFORMANCE_ALLOWCLICKABLELINK, IDC_CHECK_PERFORMANCE_DEACTIVATEWORDWRAP,
		IDC_STATIC_PERFORMANCE_FILESIZE, IDC_STATIC_PERFORMANCE_MB })
		enableItem(ctrl, enable);
}

void PerformanceSubDlg::initDialog()
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	const auto& lfr = nppGUI._largeFileRestriction;

	int64_t fileLenInMB = (lfr._largeFileSizeDefInByte / 1024) / 1024;
	setItemInt(IDC_EDIT_PERFORMANCE_FILESIZE, static_cast<int>(fileLenInMB));
	setChecked(IDC_CHECK_PERFORMANCE_ENABLE, lfr._isEnabled);
	setChecked(IDC_CHECK_PERFORMANCE_ALLOWBRACEMATCH, lfr._allowBraceMatch);
	setChecked(IDC_CHECK_PERFORMANCE_ALLOWAUTOCOMPLETION, lfr._allowAutoCompletion);
	setChecked(IDC_CHECK_PERFORMANCE_ALLOWSMARTHILITE, lfr._allowSmartHilite);
	setChecked(IDC_CHECK_PERFORMANCE_ALLOWCLICKABLELINK, lfr._allowClickableLink);
	setChecked(IDC_CHECK_PERFORMANCE_DEACTIVATEWORDWRAP, lfr._deactivateWordWrap);
	setChecked(IDC_CHECK_PERFORMANCE_SUPPRESS2GBWARNING, lfr._suppress2GBWarning);
	// upstream: hidden in the 32-bit build; NotePadLX is 64-bit

	enableRestrictionCtrls(isCheckedOrNot(IDC_CHECK_PERFORMANCE_ENABLE));

	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	setTip(IDD_PERFORMANCE_TIP_QUESTION_BUTTON, pNativeSpeaker->getLocalizedStrFromID("largeFileRestriction-tip",
		L"Some features may slow performance in large files. These features can be auto-disabled on opening a large file. You can customize them here.\n\nNOTE:\n1. Modifying options here requires re-open currently opened large files to get proper behavior.\n\n2. If \"Deactivate Word Wrap globally\" is checked and you open a large file, \"Word Wrap\" will be disabled for all files. You can re-enable it via menu \"View->Word Wrap\""));
}

void PerformanceSubDlg::onCommand(int id, int notification)
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	auto& lfr = nppGUI._largeFileRestriction;

	if (id == IDC_EDIT_PERFORMANCE_FILESIZE)
	{
		if (notification == RcNotifyEditChange)
		{
			if (getItemText(IDC_EDIT_PERFORMANCE_FILESIZE).empty())
				return;

			static constexpr int fileLenInMBMax = (INT32_MAX - 1024 * 1024) / 1024 / 1024; // -1MB ... have to to consider also the bufferSizeRequested algo in FileManager::loadFileData
			int64_t fileLenInMB = getItemInt(IDC_EDIT_PERFORMANCE_FILESIZE);
			if (fileLenInMB > fileLenInMBMax)
			{
				fileLenInMB = fileLenInMBMax;
				setItemInt(IDC_EDIT_PERFORMANCE_FILESIZE, static_cast<int>(fileLenInMB));
			}
			lfr._largeFileSizeDefInByte = fileLenInMB * 1024 * 1024;
		}
		else if (notification == RcNotifyKillFocus)
		{
			if (getItemText(IDC_EDIT_PERFORMANCE_FILESIZE).empty())
				setItemInt(IDC_EDIT_PERFORMANCE_FILESIZE, (NPP_STYLING_FILESIZE_LIMIT_DEFAULT / 1024) / 1024);

			int64_t fileLenInMB = getItemInt(IDC_EDIT_PERFORMANCE_FILESIZE);
			if (fileLenInMB == 0)
			{
				fileLenInMB = (NPP_STYLING_FILESIZE_LIMIT_DEFAULT / 1024) / 1024;
				setItemInt(IDC_EDIT_PERFORMANCE_FILESIZE, static_cast<int>(fileLenInMB));
				lfr._largeFileSizeDefInByte = fileLenInMB * 1024 * 1024;
			}
		}
		return;
	}

	switch (id)
	{
		case IDC_CHECK_PERFORMANCE_ENABLE:
		{
			bool largeFileRestrictionEnabled = isCheckedOrNot(IDC_CHECK_PERFORMANCE_ENABLE);
			lfr._isEnabled = largeFileRestrictionEnabled;
			enableRestrictionCtrls(largeFileRestrictionEnabled);

			if (largeFileRestrictionEnabled)
			{
				sendToNpp(NPPM_INTERNAL_CLEANSMARTHILITING);
				sendToNpp(NPPM_INTERNAL_CLEANBRACEMATCH);
			}
			else
			{
				sendToNpp(NPPM_INTERNAL_UPDATECLICKABLELINKS);
			}
			return;
		}

		case IDC_CHECK_PERFORMANCE_ALLOWBRACEMATCH:
			lfr._allowBraceMatch = isCheckedOrNot(id);
			if (!lfr._allowBraceMatch)
				sendToNpp(NPPM_INTERNAL_CLEANBRACEMATCH);
			return;

		case IDC_CHECK_PERFORMANCE_ALLOWAUTOCOMPLETION:
			lfr._allowAutoCompletion = isCheckedOrNot(id);
			return;

		case IDC_CHECK_PERFORMANCE_ALLOWSMARTHILITE:
			lfr._allowSmartHilite = isCheckedOrNot(id);
			if (!lfr._allowSmartHilite)
				sendToNpp(NPPM_INTERNAL_CLEANSMARTHILITING);
			return;

		case IDC_CHECK_PERFORMANCE_ALLOWCLICKABLELINK:
			lfr._allowClickableLink = isCheckedOrNot(id);
			sendToNpp(NPPM_INTERNAL_UPDATECLICKABLELINKS);
			return;

		case IDC_CHECK_PERFORMANCE_DEACTIVATEWORDWRAP:
			lfr._deactivateWordWrap = isCheckedOrNot(id);
			return;

		case IDC_CHECK_PERFORMANCE_SUPPRESS2GBWARNING:
			lfr._suppress2GBWarning = isCheckedOrNot(id);
			return;

		default:
			return;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// MISC.

static constexpr int fileUpdateChoiceEnable = 0;
static constexpr int fileUpdateChoiceEnable4All = 1;
static constexpr int fileUpdateChoiceDisable = 2;

void MiscSubDlg::initDialog()
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	QComboBox* fileUpdate = comboBox(IDC_COMBO_FILEUPDATECHOICE);
	for (const wchar_t* choice : { L"Enable for current file(s)", L"Enable for all opened files", L"Disable" })
		fileUpdate->addItem(QString::fromWCharArray(choice));

	int selIndex = -1;
	if (nppGUI._fileAutoDetection & cdEnabled4AllFiles)
	{
		selIndex = fileUpdateChoiceEnable4All;
	}
	else if (nppGUI._fileAutoDetection & cdEnabled4CurrentFile)
	{
		selIndex = fileUpdateChoiceEnable;
	}
	else //cdDisabled
	{
		enableItem(IDC_CHECK_UPDATESILENTLY, false);
		enableItem(IDC_CHECK_UPDATEGOTOEOF, false);
		selIndex = fileUpdateChoiceDisable;
	}
	fileUpdate->setCurrentIndex(selIndex);

	setChecked(IDC_CHECK_UPDATESILENTLY, nppGUI._fileAutoDetection & cdAutoUpdate);
	setChecked(IDC_CHECK_UPDATEGOTOEOF, nppGUI._fileAutoDetection & cdGo2end);

	QComboBox* trayCombo = comboBox(IDC_COMBO_SYSTRAY_ACTION_CHOICE);
	for (const wchar_t* choice : { L"No action on", L"Minimize to", L"Close to", L"Minimize / Close to" })
		trayCombo->addItem(QString::fromWCharArray(choice));
	if (nppGUI._isMinimizedToTray < 0 || nppGUI._isMinimizedToTray > sta_minimize_close)
		nppGUI._isMinimizedToTray = sta_none;
	trayCombo->setCurrentIndex(nppGUI._isMinimizedToTray);
	// NPLX: some desktops (GNOME without an extension) have no system tray
	if (!QSystemTrayIcon::isSystemTrayAvailable())
	{
		for (int ctrl : { IDC_COMBO_SYSTRAY_ACTION_CHOICE, IDC_SYSTRAY_STATIC })
		{
			enableItem(ctrl, false);
			dlgItem(ctrl)->setToolTip(QStringLiteral("This desktop has no system tray."));
		}
	}

	// NPLX: GDI / DirectWrite are Windows-only (Scintilla's Qt backend draws with QPainter)
	showItem(IDC_COMBO_SC_TECHNOLOGY_CHOICE, false);
	showItem(IDC_SC_TECHNOLOGY_STATIC, false);

	setChecked(IDC_CHECK_DETECTENCODING, nppGUI._detectEncoding);
	setChecked(IDC_CHECK_SAVEALLCONFIRM, nppGUI._saveAllConfirm);
	setChecked(IDC_CHECK_ALOOWSIMLINKFAW, nppGUI._isFawSymlinkAllowed);

	// upstream: the auto-updater choice is shown only with WinGUp; NotePadLX has ? > Check for Updates
	showItem(IDC_AUTOUPDATE_STATIC, false);
	showItem(IDC_COMBO_AUTOUPDATE, false);

	setChecked(IDC_CHECK_ENABLEDOCPEEKER, nppGUI._isDocPeekOnTab);
	setChecked(IDC_CHECK_ENABLEDOCPEEKONMAP, nppGUI._isDocPeekOnMap);
	// NPLX: the document peeker is not ported yet
	for (int ctrl : { IDC_CHECK_ENABLEDOCPEEKER, IDC_CHECK_ENABLEDOCPEEKONMAP, IDC_DOCUMENTPEEK_STATIC })
	{
		enableItem(ctrl, false);
		dlgItem(ctrl)->setToolTip(QStringLiteral("The document peeker is not ported to NotePadLX yet."));
	}

	setChecked(IDC_CHECK_MUTE_SOUNDS, nppGUI._muteSounds);

	setItemText(IDC_EDIT_SESSIONFILEEXT, nppGUI._definedSessionExt);
	setItemText(IDC_EDIT_WORKSPACEFILEEXT, nppGUI._definedWorkspaceExt);

	setChecked(IDC_CHECK_ENABLEDOCSWITCHER, nppGUI._doTaskList);
	setChecked(IDC_CHECK_STYLEMRU, nppGUI._styleMRU);
	enableItem(IDC_CHECK_STYLEMRU, nppGUI._doTaskList);
	setChecked(IDC_CHECK_SHORTTITLE, nppGUI._shortTitlebar);
}

void MiscSubDlg::updateFileAutoDetection()
{
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	const int index = comboBox(IDC_COMBO_FILEUPDATECHOICE)->currentIndex();
	int cd = cdDisabled;
	if (index == fileUpdateChoiceEnable || index == fileUpdateChoiceEnable4All)
	{
		cd |= (index == fileUpdateChoiceEnable4All) ? cdEnabled4AllFiles : cdEnabled4CurrentFile;
		if (isCheckedOrNot(IDC_CHECK_UPDATESILENTLY))
			cd |= cdAutoUpdate;
		if (isCheckedOrNot(IDC_CHECK_UPDATEGOTOEOF))
			cd |= cdGo2end;
	}
	nppGUI._fileAutoDetection = cd;
}

void MiscSubDlg::onCommand(int id, int notification)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();

	if (notification == RcNotifyEditChange)
	{
		switch (id)
		{
			case IDC_EDIT_SESSIONFILEEXT:
				nppGUI._definedSessionExt = getItemText(IDC_EDIT_SESSIONFILEEXT);
				return;

			case IDC_EDIT_WORKSPACEFILEEXT:
				nppGUI._definedWorkspaceExt = getItemText(IDC_EDIT_WORKSPACEFILEEXT);
				return;

			default:
				return;
		}
	}

	if (notification == RcNotifySelChange)
	{
		if (id == IDC_COMBO_FILEUPDATECHOICE)
		{
			const int index = comboBox(IDC_COMBO_FILEUPDATECHOICE)->currentIndex();
			const bool isEnabled = (index == fileUpdateChoiceEnable || index == fileUpdateChoiceEnable4All);
			enableItem(IDC_CHECK_UPDATESILENTLY, isEnabled);
			enableItem(IDC_CHECK_UPDATEGOTOEOF, isEnabled);
			updateFileAutoDetection();
		}
		else if (id == IDC_COMBO_SYSTRAY_ACTION_CHOICE)
		{
			nppGUI._isMinimizedToTray = comboBox(IDC_COMBO_SYSTRAY_ACTION_CHOICE)->currentIndex();
		}
		return;
	}

	switch (id)
	{
		case IDC_CHECK_UPDATESILENTLY:
		case IDC_CHECK_UPDATEGOTOEOF:
			updateFileAutoDetection();
			return;

		case IDC_CHECK_DETECTENCODING:
			nppGUI._detectEncoding = isCheckedOrNot(id);
			return;

		case IDC_CHECK_ENABLEDOCSWITCHER:
			nppGUI._doTaskList = !nppGUI._doTaskList;
			if (nppGUI._doTaskList)
			{
				enableItem(IDC_CHECK_STYLEMRU, true);
			}
			else
			{
				nppGUI._styleMRU = false;
				setChecked(IDC_CHECK_STYLEMRU, false);
				enableItem(IDC_CHECK_STYLEMRU, false);
			}
			return;

		case IDC_CHECK_STYLEMRU:
			nppGUI._styleMRU = isCheckedOrNot(IDC_CHECK_STYLEMRU);
			return;

		case IDC_CHECK_SHORTTITLE:
			nppGUI._shortTitlebar = isCheckedOrNot(IDC_CHECK_SHORTTITLE);
			sendToNpp(NPPM_INTERNAL_UPDATETITLEBAR);
			return;

		case IDC_CHECK_ENABLEDOCPEEKER:
			nppGUI._isDocPeekOnTab = isCheckedOrNot(IDC_CHECK_ENABLEDOCPEEKER);
			return;

		case IDC_CHECK_ENABLEDOCPEEKONMAP:
			nppGUI._isDocPeekOnMap = isCheckedOrNot(IDC_CHECK_ENABLEDOCPEEKONMAP);
			return;

		case IDC_CHECK_MUTE_SOUNDS:
			nppGUI._muteSounds = isCheckedOrNot(IDC_CHECK_MUTE_SOUNDS);
			return;

		case IDC_CHECK_SAVEALLCONFIRM:
			nppGUI._saveAllConfirm = isCheckedOrNot(IDC_CHECK_SAVEALLCONFIRM);
			return;

		case IDC_CHECK_ALOOWSIMLINKFAW:
		{
			static bool isFirstTime = true;
			nppGUI._isFawSymlinkAllowed = isCheckedOrNot(IDC_CHECK_ALOOWSIMLINKFAW);
			if (nppGUI._isFawSymlinkAllowed && isFirstTime)
			{
				NativeLangSpeaker* pNativeSpeaker = nppParam.getNativeLangSpeaker();
				pNativeSpeaker->messageBox("SymlinkLoadingWarning",
					L"Enabling symlink loading in Folder as Workspace may cause NotePadLX to enter an infinite loop and freeze.\rThis can happen when directory symlinks resolve to parent folders or form circular references.\rProceed only if you understand the risks.",
					L"Warning: Symlink Loading May Cause Freeze",
					MB_OK | MB_ICONWARNING | MB_APPLMODAL);
				isFirstTime = false;
			}
			return;
		}

		default:
			return;
	}
}
