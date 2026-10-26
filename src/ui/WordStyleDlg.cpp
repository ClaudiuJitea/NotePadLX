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

#include "WordStyleDlg.h"

#include "DocumentMap.h" // ViewZoneDlg

#include <cwchar>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "ColourPicker.h"
#include "Common.h"
#include "DocTabView.h"
#include "LocalizationQt.h"
#include "NppConstants.h"
#include "NppDarkMode.h"
#include "NppDarkModeOptions.h"
#include "WordStyleDlgRes.h"
#include "preference_rc.h"
#include "localization.h"
#include "resource.h"

// The following parameters are for apply() method which will re-initialize the followings GUI with modified styler:
// 2 Scintilla edit zones, Search result (displayed by Sintilla), Notepad++ GUI & components concerning theme
enum ApplyChange
{
	NO_VISUAL_CHANGE    = 0x00, // No need to apply visual effect - User ext.
	GENERAL_CHANGE      = 0x01, // For Sintilla zones & Notepad++ GUI (Tabbar, Find dialog, etc...)
	THEME_CHANGE        = 0x02, // For the components concerning theme, for example the background color of dockable panels
	COLOR_CHANGE_4_MENU = 0x04, // For the color items displayed on the menu
};

static constexpr bool C_FOREGROUND = false;
static constexpr bool C_BACKGROUND = true;

using namespace std;

// NPLX: indexes of upstream TabBarPlus::tabColourIndex (= DocTabView::tabColourIndex) / individualTabColourId
namespace TabBarPlusIndex
{
	enum tabColourIndex { activeText, activeFocusedTop, activeUnfocusedTop, inactiveText, inactiveBg };
	enum individualTabColourId { id0, id1, id2, id3, id4, id5, id6, id7, id8, id9 };
}

WordStyleDlg::WordStyleDlg(QWidget* parent)
	: QDialog(parent)
{
}

// ============================================================================
// Controls by upstream ID
// ============================================================================

QWidget* WordStyleDlg::dlgItem(int id) const
{
	auto it = _ctrls.find(id);
	return it == _ctrls.end() ? nullptr : it->second;
}

bool WordStyleDlg::isCheckedOrNot(int id) const
{
	auto* b = qobject_cast<QAbstractButton*>(dlgItem(id));
	return b && b->isChecked();
}

void WordStyleDlg::setChecked(int id, bool checked) const
{
	if (auto* b = qobject_cast<QAbstractButton*>(dlgItem(id)))
	{
		QSignalBlocker blocker(b);
		b->setChecked(checked);
	}
}

void WordStyleDlg::showItem(int id, bool show) const
{
	if (QWidget* w = dlgItem(id))
		w->setVisible(show);
}

void WordStyleDlg::enableItem(int id, bool enable) const
{
	if (QWidget* w = dlgItem(id))
		w->setEnabled(enable);
}

QComboBox* WordStyleDlg::languagesCombo() const
{
	return qobject_cast<QComboBox*>(dlgItem(IDC_LANGUAGES_COMBO));
}

QListWidget* WordStyleDlg::stylesList() const
{
	return qobject_cast<QListWidget*>(dlgItem(IDC_STYLES_LIST));
}

// ============================================================================
// Dialog (upstream WordStyleDlg.rc + WM_INITDIALOG)
// ============================================================================

void WordStyleDlg::create()
{
	if (_isCreated)
		return;

	NppParameters& nppParamInst = NppParameters::getInstance();
	setWindowTitle(QStringLiteral("Style Configurator"));
	setWindowFlag(Qt::WindowContextHelpButtonHint, false);
	setModal(false);

	auto* mainLayout = new QVBoxLayout(this);

	auto addLabel = [this](int id, const QString& text) {
		auto* l = new QLabel(text, this);
		_ctrls[id] = l;
		return l;
	};
	auto addCheck = [this](int id, const QString& text) {
		auto* c = new QCheckBox(text, this);
		_ctrls[id] = c;
		connect(c, &QCheckBox::toggled, this, [this, id]() { command(id); });
		return c;
	};

	// theme
	auto* themeRow = new QHBoxLayout;
	themeRow->addStretch(1);
	themeRow->addWidget(addLabel(IDC_SWITCH2THEME_STATIC, QStringLiteral("Select theme:")));
	_hSwitch2ThemeCombo = new QComboBox(this);
	_hSwitch2ThemeCombo->setMinimumWidth(200);
	_ctrls[IDC_SWITCH2THEME_COMBO] = _hSwitch2ThemeCombo;
	themeRow->addWidget(_hSwitch2ThemeCombo);
	themeRow->addStretch(1);
	mainLayout->addLayout(themeRow);

	auto* body = new QHBoxLayout;
	mainLayout->addLayout(body, 1);

	// left: language, styles, extensions
	auto* langGroup = new QGroupBox(this);
	auto* langLayout = new QVBoxLayout(langGroup);
	langLayout->addWidget(addLabel(IDC_LANGDESC_STATIC, QStringLiteral("Language:")));
	auto* languages = new QComboBox(this);
	_ctrls[IDC_LANGUAGES_COMBO] = languages;
	langLayout->addWidget(languages);
	langLayout->addWidget(addLabel(IDC_STYLEDESC_STATIC, QStringLiteral("Style:")));
	auto* styles = new QListWidget(this);
	styles->setMinimumHeight(220);
	_ctrls[IDC_STYLES_LIST] = styles;
	langLayout->addWidget(styles, 1);
	auto* extGrid = new QGridLayout;
	extGrid->addWidget(addLabel(IDC_DEF_EXT_STATIC, QStringLiteral("Default ext.:")), 0, 0);
	extGrid->addWidget(addLabel(IDC_USER_EXT_STATIC, QStringLiteral("User ext.:")), 0, 2);
	auto* defExt = new QLineEdit(this);
	defExt->setReadOnly(true);
	_ctrls[IDC_DEF_EXT_EDIT] = defExt;
	auto* userExt = new QLineEdit(this);
	_ctrls[IDC_USER_EXT_EDIT] = userExt;
	extGrid->addWidget(defExt, 1, 0);
	extGrid->addWidget(addLabel(IDC_PLUSSYMBOL2_STATIC, QStringLiteral("+")), 1, 1);
	extGrid->addWidget(userExt, 1, 2);
	langLayout->addLayout(extGrid);
	body->addWidget(langGroup);

	// right: style description, colours, font, keywords / global override
	auto* styleGroup = new QGroupBox(this);
	auto* styleLayout = new QVBoxLayout(styleGroup);
	_hStyleInfoStaticText = addLabel(IDC_STYLEDESCRIPTION_STATIC, QString());
	QFont boldFont = _hStyleInfoStaticText->font();
	boldFont.setBold(true);
	_hStyleInfoStaticText->setFont(boldFont);
	styleLayout->addWidget(_hStyleInfoStaticText);

	auto* groups = new QHBoxLayout;
	auto* colourGroup = new QGroupBox(QStringLiteral("Colour Style"), this);
	colourGroup->setAlignment(Qt::AlignHCenter);
	_ctrls[IDC_COLOURGROUP_STATIC] = colourGroup;
	auto* colourGrid = new QGridLayout(colourGroup);
	_pFgColour = new ColourPicker(this);
	_pBgColour = new ColourPicker(this);
	colourGrid->addWidget(addLabel(IDC_FG_STATIC, QStringLiteral("Foreground colour")), 0, 0, Qt::AlignRight);
	colourGrid->addWidget(_pFgColour, 0, 1);
	colourGrid->addWidget(addLabel(IDC_BG_STATIC, QStringLiteral("Background colour")), 1, 0, Qt::AlignRight);
	colourGrid->addWidget(_pBgColour, 1, 1);
	groups->addWidget(colourGroup);

	auto* fontGroup = new QGroupBox(QStringLiteral("Font Style"), this);
	fontGroup->setAlignment(Qt::AlignHCenter);
	_ctrls[IDC_FONTGROUP_STATIC] = fontGroup;
	auto* fontGrid = new QGridLayout(fontGroup);
	fontGrid->addWidget(addLabel(IDC_FONTNAME_STATIC, QStringLiteral("Font name:")), 0, 0, Qt::AlignRight);
	_hFontNameCombo = new QComboBox(this);
	_hFontNameCombo->setMinimumWidth(170);
	_ctrls[IDC_FONT_COMBO] = _hFontNameCombo;
	fontGrid->addWidget(_hFontNameCombo, 0, 1, 1, 3);
	fontGrid->addWidget(addCheck(IDC_BOLD_CHECK, QStringLiteral("Bold")), 1, 0);
	fontGrid->addWidget(addLabel(IDC_FONTSIZE_STATIC, QStringLiteral("Font size:")), 1, 2, Qt::AlignRight);
	_hFontSizeCombo = new QComboBox(this);
	_ctrls[IDC_FONTSIZE_COMBO] = _hFontSizeCombo;
	fontGrid->addWidget(_hFontSizeCombo, 1, 3);
	fontGrid->addWidget(addCheck(IDC_ITALIC_CHECK, QStringLiteral("Italic")), 2, 0);
	fontGrid->addWidget(addCheck(IDC_UNDERLINE_CHECK, QStringLiteral("Underline")), 3, 0);
	groups->addWidget(fontGroup, 1);
	styleLayout->addLayout(groups);

	auto* keywordsGrid = new QGridLayout;
	keywordsGrid->addWidget(addLabel(IDC_DEF_KEYWORDS_STATIC, QStringLiteral("Default keywords")), 0, 0);
	keywordsGrid->addWidget(addLabel(IDC_USER_KEYWORDS_STATIC, QStringLiteral("User-defined keywords")), 0, 2);
	auto* defKeywords = new QPlainTextEdit(this);
	defKeywords->setReadOnly(true);
	_ctrls[IDC_DEF_KEYWORDS_EDIT] = defKeywords;
	auto* userKeywords = new QPlainTextEdit(this);
	_ctrls[IDC_USER_KEYWORDS_EDIT] = userKeywords;
	keywordsGrid->addWidget(defKeywords, 1, 0);
	keywordsGrid->addWidget(addLabel(IDC_PLUSSYMBOL_STATIC, QStringLiteral("+")), 1, 1);
	keywordsGrid->addWidget(userKeywords, 1, 2);

	// global override controls share the keywords area (upstream: same position, shown instead)
	auto* globalGrid = new QGridLayout;
	globalGrid->addWidget(addCheck(IDC_GLOBAL_FG_CHECK, QStringLiteral("Force foreground color for all styles")), 0, 0);
	globalGrid->addWidget(addCheck(IDC_GLOBAL_BG_CHECK, QStringLiteral("Force background color for all styles")), 1, 0);
	globalGrid->addWidget(addCheck(IDC_GLOBAL_FONT_CHECK, QStringLiteral("Force font choice for all styles")), 0, 1);
	globalGrid->addWidget(addCheck(IDC_GLOBAL_FONTSIZE_CHECK, QStringLiteral("Force font size choice for all styles")), 1, 1);
	globalGrid->addWidget(addCheck(IDC_GLOBAL_BOLD_CHECK, QStringLiteral("Force bold choice for all styles")), 2, 1);
	globalGrid->addWidget(addCheck(IDC_GLOBAL_ITALIC_CHECK, QStringLiteral("Force italic choice for all styles")), 3, 1);
	globalGrid->addWidget(addCheck(IDC_GLOBAL_UNDERLINE_CHECK, QStringLiteral("Force underline choice for all styles")), 4, 1);
	auto* whatIsGlobalOverride = addLabel(IDC_GLOBAL_WHATISGLOBALOVERRIDE_LINK, QStringLiteral("<a href=\"#\">What is Global override?</a>"));
	whatIsGlobalOverride->setToolTip(QString::fromStdWString(nppParamInst.getNativeLangSpeaker()->getLocalizedStrFromID("global-override-tip",
		L"Enabling \"Global override\" here will override that parameter in all language styles. What you probably really want is to use the \"Default Style\" settings instead")));
	globalGrid->addWidget(whatIsGlobalOverride, 3, 0);
	auto* goToSettings = addLabel(IDC_GLOBAL_GOTOSETTINGS_LINK, QStringLiteral("<a href=\"#\">Go to settings</a>"));
	connect(goToSettings, &QLabel::linkActivated, this, [this]() { command(IDC_GLOBAL_GOTOSETTINGS_LINK); });
	keywordsGrid->addWidget(goToSettings, 2, 0);
	styleLayout->addLayout(keywordsGrid);
	styleLayout->addLayout(globalGrid);
	styleLayout->addStretch(1);
	body->addWidget(styleGroup, 1);

	// buttons and transparency
	auto* buttons = new QHBoxLayout;
	buttons->addStretch(1);
	auto* saveClose = new QPushButton(QStringLiteral("Save && Close"), this);
	_ctrls[IDC_SAVECLOSE_BUTTON] = saveClose;
	auto* cancel = new QPushButton(QStringLiteral("Cancel"), this);
	_ctrls[IDCANCEL] = cancel;
	buttons->addWidget(saveClose);
	buttons->addWidget(cancel);
	buttons->addStretch(1);
	buttons->addWidget(addCheck(IDC_SC_TRANSPARENT_CHECK, QStringLiteral("Transparency")));
	auto* slider = new QSlider(Qt::Horizontal, this);
	slider->setRange(20, 200);
	slider->setValue(150);
	slider->setMaximumWidth(100);
	_ctrls[IDC_SC_PERCENTAGE_SLIDER] = slider;
	buttons->addWidget(slider);
	mainLayout->addLayout(buttons);

	connect(saveClose, &QPushButton::clicked, this, [this]() { command(IDC_SAVECLOSE_BUTTON); });
	connect(cancel, &QPushButton::clicked, this, [this]() { command(IDCANCEL); });
	connect(slider, &QSlider::valueChanged, this, [this](int percent) {
		// upstream WM_HSCROLL
		setWindowOpacity(1.0 - percent / 255.0); // NPLX: was NppParameters::setTransparent
	});
	connect(_hSwitch2ThemeCombo, &QComboBox::activated, this, [this]() { command(IDC_SWITCH2THEME_COMBO); });
	connect(languages, &QComboBox::currentIndexChanged, this, [this]() { if (!_isUpdatingUi) command(IDC_LANGUAGES_COMBO); });
	connect(styles, &QListWidget::currentRowChanged, this, [this]() { if (!_isUpdatingUi) command(IDC_STYLES_LIST); });
	connect(_hFontNameCombo, &QComboBox::activated, this, [this]() { command(IDC_FONT_COMBO); });
	connect(_hFontSizeCombo, &QComboBox::activated, this, [this]() { command(IDC_FONTSIZE_COMBO); });
	connect(userKeywords, &QPlainTextEdit::textChanged, this, [this]() { if (!_isUpdatingUi) command(IDC_USER_KEYWORDS_EDIT); });
	connect(userExt, &QLineEdit::textEdited, this, [this]() { command(IDC_USER_EXT_EDIT); });
	connect(_pFgColour, &ColourPicker::colourPicked, this, [this]() { colourPicked(_pFgColour); });
	connect(_pBgColour, &ColourPicker::colourPicked, this, [this]() { colourPicked(_pBgColour); });

	// upstream WM_INITDIALOG
	_currentThemeIndex = -1;
	int defaultThemeIndex = 0;
	ThemeSwitcher & themeSwitcher = nppParamInst.getThemeSwitcher();
	for (size_t i = 0 ; i < themeSwitcher.size() ; ++i)
	{
		pair<wstring, wstring> & themeInfo = themeSwitcher.getElementFromIndex(i);
		_hSwitch2ThemeCombo->addItem(QString::fromStdWString(themeInfo.first));
		const int j = _hSwitch2ThemeCombo->count() - 1;
		if (! themeInfo.second.compare( nppParamInst.getNppGUI()._themeName ) )
		{
			_currentThemeIndex = j;
			_themeName.assign(themeInfo.second);
		}
		if (! themeInfo.first.compare(L"Default") )
		{
			defaultThemeIndex = j;
		}
	}
	if (_currentThemeIndex == -1)
	{
		_currentThemeIndex = defaultThemeIndex;
	}
	_hSwitch2ThemeCombo->setCurrentIndex(_currentThemeIndex);

	for (size_t i = 0 ; i < sizeof(fontSizeStrs)/(3*sizeof(wchar_t)) ; ++i)
		_hFontSizeCombo->addItem(QString::fromWCharArray(fontSizeStrs[i]));

	const std::vector<wstring> & fontlist = nppParamInst.getFontList();
	for (size_t i = 0, len = fontlist.size() ; i < len ; ++i)
		_hFontNameCombo->addItem(QString::fromStdWString(fontlist[i]));

	enableItem(IDC_SAVECLOSE_BUTTON, false/*!_isSync*/);

	_isCreated = true;

	loadLangListFromNppParam();
	updateGlobalOverrideCtrls();
	setVisualFromStyleList();

	// upstream create(): transparency controls
	if (nppParamInst.isTransparentAvailable())
	{
		showItem(IDC_SC_TRANSPARENT_CHECK, true);
		showItem(IDC_SC_PERCENTAGE_SLIDER, true);
		enableItem(IDC_SC_PERCENTAGE_SLIDER, isCheckedOrNot(IDC_SC_TRANSPARENT_CHECK));
	}
}

void WordStyleDlg::doDialog(bool isRTL)
{
	if (!isCreated())
	{
		if (isRTL)
			setLayoutDirection(Qt::RightToLeft);
		create();
		prepare2Cancel();
		// upstream IDM_LANGSTYLE_CONFIG_DLG: changeConfigLang on first use
		nplx::changeConfigLang(*this);
	}

	if (!isVisible())
	{
		prepare2Cancel();
	}
	show();
	raise();
	activateWindow();
}

void WordStyleDlg::reject()
{
	command(IDCANCEL);
}

void WordStyleDlg::updateGlobalOverrideCtrls()
{
	const NppGUI & nppGUI = (NppParameters::getInstance()).getNppGUI();
	setChecked(IDC_GLOBAL_FG_CHECK, nppGUI._globalOverride.enableFg);
	setChecked(IDC_GLOBAL_BG_CHECK, nppGUI._globalOverride.enableBg);
	setChecked(IDC_GLOBAL_FONT_CHECK, nppGUI._globalOverride.enableFont);
	setChecked(IDC_GLOBAL_FONTSIZE_CHECK, nppGUI._globalOverride.enableFontSize);
	setChecked(IDC_GLOBAL_BOLD_CHECK, nppGUI._globalOverride.enableBold);
	setChecked(IDC_GLOBAL_ITALIC_CHECK, nppGUI._globalOverride.enableItalic);
	setChecked(IDC_GLOBAL_UNDERLINE_CHECK, nppGUI._globalOverride.enableUnderLine);
}

// ============================================================================
// upstream run_dlgProc WM_COMMAND
// ============================================================================

void WordStyleDlg::command(int id)
{
	switch (id)
	{
		// EN_CHANGE
		case IDC_USER_KEYWORDS_EDIT:
			updateUserKeywords();
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;

		case IDC_USER_EXT_EDIT:
			updateExtension();
			notifyDataModified();
			apply(NO_VISUAL_CHANGE);
			return;

		case IDC_BOLD_CHECK :
			updateFontStyleStatus(BOLD_STATUS);
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;

		case IDC_ITALIC_CHECK :
			updateFontStyleStatus(ITALIC_STATUS);
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;

		case IDC_UNDERLINE_CHECK :
			updateFontStyleStatus(UNDERLINE_STATUS);
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;

		case IDCANCEL :
			if (_isDirty)
			{
				NppParameters& nppParamInst = NppParameters::getInstance();
				if (_restoreInvalid)
				{
					wstring str(nppParamInst.getNppGUI()._themeName);
					nppParamInst.reloadStylers(str.c_str());
					loadLangListFromNppParam();
				}

				LexerStylerArray & lsArray = nppParamInst.getLStylerArray();
				StyleArray & globalStyles = nppParamInst.getGlobalStylers();

				if (_restoreInvalid)
				{
					_lsArray = _styles2restored = lsArray;
					_globalStyles = _gstyles2restored = globalStyles;
				}
				else
				{
					globalStyles = _globalStyles = _gstyles2restored;
					lsArray = _lsArray = _styles2restored;
				}

				restoreGlobalOverrideValues();
				nppParamInst.initTabCustomColors();
				nppParamInst.initFindDlgStatusMsgCustomColors();

				_restoreInvalid = false;
				_isDirty = false;
				_isThemeDirty = false;
				setVisualFromStyleList();

				_hSwitch2ThemeCombo->setCurrentIndex(_currentThemeIndex);
				_sendToParent(WM_UPDATESCINTILLAS, _isThemeChanged, 0);
				_sendToParent(WM_UPDATEMAINMENUBITMAPS, 0, 0);

				_isThemeChanged = false;
			}
			enableItem(IDC_SAVECLOSE_BUTTON, false);
			hide();
			return;

		case IDC_SAVECLOSE_BUTTON :
		{
			if (_isDirty)
			{
				const LexerStylerArray & lsa = (NppParameters::getInstance()).getLStylerArray();
				const StyleArray & globalStyles = (NppParameters::getInstance()).getGlobalStylers();

				_lsArray = lsa;
				_globalStyles = globalStyles;
				updateThemeName(_themeName);
				_restoreInvalid = false;

				_currentThemeIndex = _hSwitch2ThemeCombo->currentIndex();
				_isDirty = false;
				_isThemeChanged = false;
			}
			_isThemeDirty = false;
			auto newSavedFilePath = (NppParameters::getInstance()).writeStyles(_lsArray, _globalStyles);
			if (!newSavedFilePath.empty())
				updateThemeName(newSavedFilePath);

			enableItem(IDC_SAVECLOSE_BUTTON, false);
			hide();

			// upstream: the theme is the one of the current mode (Preferences > Dark Mode switches between the two)
			const std::wstring fn = PathFindFileName(_themeName.c_str());
			NppDarkMode::setThemeName((!NppDarkMode::isEnabled() && fn == L"stylers.xml") ? L"" : fn);
			return;
		}

		case IDC_GLOBAL_GOTOSETTINGS_LINK :
		{
			std::pair<intptr_t, intptr_t> pageAndCtrlID = goToPreferencesSettings();

			if (pageAndCtrlID.first != -1)
				_sendToParent(NPPM_INTERNAL_LAUNCHPREFERENCES, pageAndCtrlID.first, pageAndCtrlID.second);
			return;
		}

		case IDC_SC_TRANSPARENT_CHECK :
		{
			bool isChecked = isCheckedOrNot(IDC_SC_TRANSPARENT_CHECK);
			if (isChecked)
			{
				auto* slider = qobject_cast<QSlider*>(dlgItem(IDC_SC_PERCENTAGE_SLIDER));
				const int percent = slider ? slider->value() : 150;
				setWindowOpacity(1.0 - percent / 255.0); // NPLX: was NppParameters::setTransparent
			}
			else
				setWindowOpacity(1.0); // NPLX: was NppParameters::removeTransparent

			enableItem(IDC_SC_PERCENTAGE_SLIDER, isChecked);
			return;
		}

		case IDC_GLOBAL_FG_CHECK :
		{
			GlobalOverride & glo = (NppParameters::getInstance()).getGlobalOverrideStyle();
			glo.enableFg = isCheckedOrNot(id);
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;
		}

		case IDC_GLOBAL_BG_CHECK:
		{
			GlobalOverride & glo = (NppParameters::getInstance()).getGlobalOverrideStyle();
			glo.enableBg = isCheckedOrNot(id);
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;
		}

		case IDC_GLOBAL_FONT_CHECK :
		{
			GlobalOverride & glo = (NppParameters::getInstance()).getGlobalOverrideStyle();
			glo.enableFont = isCheckedOrNot(id);
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;
		}
		case IDC_GLOBAL_FONTSIZE_CHECK :
		{
			GlobalOverride & glo = (NppParameters::getInstance()).getGlobalOverrideStyle();
			glo.enableFontSize = isCheckedOrNot(id);
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;
		}
		case IDC_GLOBAL_BOLD_CHECK :
		{
			GlobalOverride & glo = (NppParameters::getInstance()).getGlobalOverrideStyle();
			glo.enableBold = isCheckedOrNot(id);
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;
		}

		case IDC_GLOBAL_ITALIC_CHECK :
		{
			GlobalOverride & glo = (NppParameters::getInstance()).getGlobalOverrideStyle();
			glo.enableItalic = isCheckedOrNot(id);
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;
		}
		case IDC_GLOBAL_UNDERLINE_CHECK :
		{
			GlobalOverride & glo = (NppParameters::getInstance()).getGlobalOverrideStyle();
			glo.enableUnderLine = isCheckedOrNot(id);
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;
		}

		// CBN_SELCHANGE / LBN_SELCHANGE
		case IDC_FONT_COMBO :
			updateFontName();
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;
		case IDC_FONTSIZE_COMBO :
			updateFontSize();
			notifyDataModified();
			apply(GENERAL_CHANGE);
			return;
		case IDC_LANGUAGES_COMBO :
		{
			const int i = languagesCombo()->currentIndex();
			if (i != -1)
			{
				bool prevThemeState = _isThemeDirty;
				setStyleListFromLexer(i);
				_isThemeDirty = prevThemeState;
			}
			return;
		}
		case IDC_STYLES_LIST :
			setVisualFromStyleList();
			return;

		case IDC_SWITCH2THEME_COMBO :
			applyCurrentSelectedThemeAndUpdateUI();
			return;

		default:
			return;
	}
}

void WordStyleDlg::colourPicked(ColourPicker* picker)
{
	// upstream CPN_COLOURPICKED
	int applicationInfo = getApplicationInfo();

	int tabColourIndex = whichTabColourIndex();

	if (picker == _pFgColour)
	{
		updateColour(C_FOREGROUND);
		notifyDataModified();

		if (tabColourIndex != -1)
		{
			DocTabView::setColour(_pFgColour->getColour(), static_cast<DocTabView::tabColourIndex>(tabColourIndex));
		}
		else
		{
			int findDlgStatusMsgIndex = whichFindDlgStatusMsgColourIndex();
			if (findDlgStatusMsgIndex != -1)
			{
				NppParameters& nppParamInst = NppParameters::getInstance();
				nppParamInst.setFindDlgStatusMsgIndexColor(_pFgColour->getColour(), findDlgStatusMsgIndex);
			}
			else if (isDocumentMapStyle())
			{
				ViewZoneDlg::setColour(_pFgColour->getColour(), ViewZoneDlg::ViewZoneColorIndex::focus);
			}
		}
		apply(applicationInfo);
	}
	else if (picker == _pBgColour)
	{
		updateColour(C_BACKGROUND);
		notifyDataModified();

		if (tabColourIndex != -1)
		{
			tabColourIndex = (tabColourIndex == TabBarPlusIndex::inactiveText ? TabBarPlusIndex::inactiveBg : tabColourIndex);
			DocTabView::setColour(_pBgColour->getColour(), static_cast<DocTabView::tabColourIndex>(tabColourIndex));
		}

		if (tabColourIndex == -1 && isDocumentMapStyle())
		{
			ViewZoneDlg::setColour(_pBgColour->getColour(), ViewZoneDlg::ViewZoneColorIndex::frost);
		}

		if (tabColourIndex == -1 && !isDocumentMapStyle())
		{
			int colourIndex = whichIndividualTabColourId();

			if (colourIndex != -1)
			{
				if (colourIndex >= TabBarPlusIndex::id5)
					colourIndex -= TabBarPlusIndex::id5;

				NppParameters& nppParamInst = NppParameters::getInstance();
				nppParamInst.setIndividualTabColor(_pBgColour->getColour(), colourIndex, NppDarkMode::isEnabled());
			}
		}

		apply(applicationInfo);
	}
}

// ============================================================================
// upstream helpers
// ============================================================================

void WordStyleDlg::loadLangListFromNppParam()
{
	NppParameters& nppParamInst = NppParameters::getInstance();
	_lsArray = nppParamInst.getLStylerArray();
	_globalStyles = nppParamInst.getGlobalStylers();

	// Clean up Language List
	QComboBox* languages = languagesCombo();
	{
		QSignalBlocker blocker(languages);
		languages->clear();

		languages->addItem(QStringLiteral("Global Styles"));
		// All the lexers
		for (size_t i = 0, nb = _lsArray.getNbLexer() ; i < nb ; ++i)
		{
			const std::wstring langName = (_lsArray.getLexerDescFromIndex(i));
			languages->addItem(QString::fromStdWString(langName));
		}

		static constexpr int index2Begin = 0;
		languages->setCurrentIndex(index2Begin);
	}
	setStyleListFromLexer(0);
}

void WordStyleDlg::updateThemeName(const wstring& themeName)
{
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGUI = nppParam.getNppGUI();
	nppGUI._themeName.assign( themeName );
}

bool WordStyleDlg::getStyleName(std::wstring& styleName, const size_t styleNameLenLimit) const
{
	const QListWidgetItem* item = stylesList()->currentItem();
	if (!item)
		return false;

	const std::wstring text = item->text().toStdWString();
	if (text.size() > styleNameLenLimit)
		return false;

	styleName = text;
	return true;
}

int WordStyleDlg::getApplicationInfo() const
{
	std::wstring styleName;
	if (!WordStyleDlg::getStyleName(styleName))
	{
		return NO_VISUAL_CHANGE;
	}

	if (styleName == L"Default Style")
	{
		return (GENERAL_CHANGE | THEME_CHANGE);
	}

	if ((styleName == L"Mark Style 1")
		|| (styleName == L"Mark Style 2")
		|| (styleName == L"Mark Style 3")
		|| (styleName == L"Mark Style 4")
		|| (styleName == L"Mark Style 5")
		|| (styleName == TABBAR_INDIVIDUALCOLOR_1)
		|| (styleName == TABBAR_INDIVIDUALCOLOR_2)
		|| (styleName == TABBAR_INDIVIDUALCOLOR_3)
		|| (styleName == TABBAR_INDIVIDUALCOLOR_4)
		|| (styleName == TABBAR_INDIVIDUALCOLOR_5)
		|| (styleName == TABBAR_INDIVIDUALCOLOR_DM_1)
		|| (styleName == TABBAR_INDIVIDUALCOLOR_DM_2)
		|| (styleName == TABBAR_INDIVIDUALCOLOR_DM_3)
		|| (styleName == TABBAR_INDIVIDUALCOLOR_DM_4)
		|| (styleName == TABBAR_INDIVIDUALCOLOR_DM_5))
	{
		return (GENERAL_CHANGE | COLOR_CHANGE_4_MENU);
	}
	return GENERAL_CHANGE;
}

int WordStyleDlg::whichTabColourIndex() const
{
	std::wstring styleName;
	if (!WordStyleDlg::getStyleName(styleName))
	{
		return -1;
	}

	using enum TabBarPlusIndex::tabColourIndex;

	if (styleName == TABBAR_ACTIVEFOCUSEDINDCATOR)
		return activeFocusedTop;

	if (styleName == TABBAR_ACTIVEUNFOCUSEDINDCATOR)
		return activeUnfocusedTop;

	if (styleName == TABBAR_ACTIVETEXT)
		return activeText;

	if (styleName == TABBAR_INACTIVETEXT)
		return inactiveText;

	return -1;
}

int WordStyleDlg::whichIndividualTabColourId()
{
	std::wstring styleName;
	if (!WordStyleDlg::getStyleName(styleName))
	{
		return -1;
	}

	using enum TabBarPlusIndex::individualTabColourId;

	if (styleName == TABBAR_INDIVIDUALCOLOR_1)
		return id0;

	if (styleName == TABBAR_INDIVIDUALCOLOR_2)
		return id1;

	if (styleName == TABBAR_INDIVIDUALCOLOR_3)
		return id2;

	if (styleName == TABBAR_INDIVIDUALCOLOR_4)
		return id3;

	if (styleName == TABBAR_INDIVIDUALCOLOR_5)
		return id4;


	if (styleName == TABBAR_INDIVIDUALCOLOR_DM_1)
		return id5;

	if (styleName == TABBAR_INDIVIDUALCOLOR_DM_2)
		return id6;

	if (styleName == TABBAR_INDIVIDUALCOLOR_DM_3)
		return id7;

	if (styleName == TABBAR_INDIVIDUALCOLOR_DM_4)
		return id8;

	if (styleName == TABBAR_INDIVIDUALCOLOR_DM_5)
		return id9;


	return -1;
}

int WordStyleDlg::whichFindDlgStatusMsgColourIndex()
{
	std::wstring styleName;
	if (!WordStyleDlg::getStyleName(styleName))
	{
		return -1;
	}

	using enum TabBarPlusIndex::individualTabColourId;

	if (styleName == FINDDLG_STAUSNOTFOUND_COLOR)
		return id0;

	if (styleName == FINDDLG_STAUSMESSAGE_COLOR)
		return id1;

	if (styleName == FINDDLG_STAUSREACHED_COLOR)
		return id2;

	return -1;
}

bool WordStyleDlg::isDocumentMapStyle()
{
	std::wstring styleName;
	return WordStyleDlg::getStyleName(styleName) && (styleName == VIEWZONE_DOCUMENTMAP);
}

void WordStyleDlg::updateColour(bool which)
{
	Style & style = getCurrentStyler();
	if (which == C_FOREGROUND)
	{
		style._fgColor = _pFgColour->getColour();
		if (_pFgColour->isEnabled())
			style._colorStyle |= COLORSTYLE_FOREGROUND;
		else
			style._colorStyle &= ~COLORSTYLE_FOREGROUND;
	}
	else //(which == C_BACKGROUND)
	{
		style._bgColor = _pBgColour->getColour();
		if (_pBgColour->isEnabled())
			style._colorStyle |= COLORSTYLE_BACKGROUND;
		else
			style._colorStyle &= ~COLORSTYLE_BACKGROUND;
	}
}

void WordStyleDlg::updateFontSize()
{
	Style & style = getCurrentStyler();
	auto iFontSizeSel = _hFontSizeCombo->currentIndex();

	if (iFontSizeSel != 0)
	{
		const std::wstring intStr = _hFontSizeCombo->itemText(iFontSizeSel).toStdWString();
		if (intStr.size() >= 3)
			return;

		if (intStr.empty())
			style._fontSize = STYLE_NOT_USED;
		else
		{
			wchar_t *finStr = nullptr;
			style._fontSize = wcstol(intStr.c_str(), &finStr, 10);
			if (*finStr != '\0')
				style._fontSize = STYLE_NOT_USED;
		}
	}
	else
		style._fontSize = 0;
}

void WordStyleDlg::updateExtension()
{
	const std::wstring ext = qobject_cast<QLineEdit*>(dlgItem(IDC_USER_EXT_EDIT))->text().left(255).toStdWString();
	_lsArray.getLexerFromIndex(static_cast<size_t>(_currentLexerIndex) - 1).setLexerUserExt(ext.c_str());
}

void WordStyleDlg::updateUserKeywords()
{
	Style & style = getCurrentStyler();
	const std::wstring kw = qobject_cast<QPlainTextEdit*>(dlgItem(IDC_USER_KEYWORDS_EDIT))->toPlainText().toStdWString();
	style._keywords = wstring2string(kw);
}

void WordStyleDlg::updateFontName()
{
	Style & style = getCurrentStyler();
	auto iFontSel = _hFontNameCombo->currentIndex();
	// NPLX: item 0 is the empty entry ("no font"), like upstream's first font list item
	style._fontName = iFontSel <= 0 ? std::wstring() : _hFontNameCombo->itemText(iFontSel).toStdWString();
}

void WordStyleDlg::updateFontStyleStatus(fontStyleType whitchStyle)
{
	Style & style = getCurrentStyler();
	if (style._fontStyle == STYLE_NOT_USED)
		style._fontStyle = FONTSTYLE_NONE;

	int fontStyle = FONTSTYLE_UNDERLINE;
	int checkID = IDC_UNDERLINE_CHECK;

	if (whitchStyle == BOLD_STATUS)
	{
		fontStyle = FONTSTYLE_BOLD;
		checkID = IDC_BOLD_CHECK;
	}
	if (whitchStyle == ITALIC_STATUS)
	{
		fontStyle = FONTSTYLE_ITALIC;
		checkID = IDC_ITALIC_CHECK;
	}

	if (isCheckedOrNot(checkID))
		style._fontStyle |= fontStyle;
	else
		style._fontStyle &= ~fontStyle;
}

void WordStyleDlg::switchToTheme()
{
	auto iSel = _hSwitch2ThemeCombo->currentIndex();

	wstring prevThemeName(_themeName);
	_themeName.clear();

	NppParameters& nppParamInst = NppParameters::getInstance();
	ThemeSwitcher & themeSwitcher = nppParamInst.getThemeSwitcher();
	pair<wstring, wstring> & themeInfo = themeSwitcher.getElementFromIndex(iSel);
	_themeName = themeInfo.second;

	if (_isThemeDirty)
	{
		wstring themeFileName = prevThemeName;
		themeFileName = PathFindFileName(themeFileName.c_str());
		PathRemoveExtension(themeFileName);
		NativeLangSpeaker *pNativeSpeaker = nppParamInst.getNativeLangSpeaker();
		int mb_response = pNativeSpeaker->messageBox("SwitchUnsavedThemeWarning",
			L"Unsaved changes are about to be discarded!\nDo you want to save your changes before switching themes?",
			L"$STR_REPLACE$",
			MB_ICONWARNING | MB_YESNO | MB_APPLMODAL | MB_SETFOREGROUND,
			0,
			themeFileName.c_str());
		if ( mb_response == IDYES )
			(NppParameters::getInstance()).writeStyles(_lsArray, _globalStyles);
	}
	nppParamInst.reloadStylers(_themeName.c_str());

	loadLangListFromNppParam();
	_restoreInvalid = true;

}

void WordStyleDlg::applyCurrentSelectedThemeAndUpdateUI()
{
	switchToTheme();
	setVisualFromStyleList();
	notifyDataModified();
	_isThemeDirty = false;
	apply(GENERAL_CHANGE | THEME_CHANGE);
}

bool WordStyleDlg::selectThemeByName(const wchar_t* themeName)
{
	const int iTheme = _hSwitch2ThemeCombo->findText(QString::fromWCharArray(themeName), Qt::MatchStartsWith);
	if (iTheme == -1)
		return false;

	_hSwitch2ThemeCombo->setCurrentIndex(iTheme);

	applyCurrentSelectedThemeAndUpdateUI();

	return true;
}

bool WordStyleDlg::goToSection(const wchar_t* sectionNames)
{
	if (!sectionNames || !sectionNames[0])
		return false;

	std::vector<std::wstring> sections = tokenizeString(sectionNames, ':');

	if (sections.size() == 0 || sections.size() >= 3)
		return false;

	auto i = languagesCombo()->findText(QString::fromStdWString(sections[0]), Qt::MatchStartsWith);
	if (i == -1)
		return false;
	{
		QSignalBlocker blocker(languagesCombo());
		languagesCombo()->setCurrentIndex(i);
	}
	setStyleListFromLexer(static_cast<int>(i));

	if (sections.size() == 1)
		return true;

	const QList<QListWidgetItem*> found = stylesList()->findItems(QString::fromStdWString(sections[1]), Qt::MatchStartsWith);
	if (found.isEmpty())
		return false;
	{
		QSignalBlocker blocker(stylesList());
		stylesList()->setCurrentItem(found.first());
	}
	setVisualFromStyleList();

	activateWindow();

	return true;
}

void WordStyleDlg::setStyleListFromLexer(int index)
{
	_currentLexerIndex = index;

	// Fill out Styles listbox
	// Before filling out, we clean it
	_isUpdatingUi = true;
	stylesList()->clear();

	if (index)
	{
		const wchar_t* langName = _lsArray.getLexerNameFromIndex(static_cast<size_t>(index) - 1);
		const wchar_t *ext = NppParameters::getInstance().getLangExtFromName(langName);
		const wchar_t *userExt = (_lsArray.getLexerStylerByName(langName))->getLexerUserExt();

		if (ext)
			qobject_cast<QLineEdit*>(dlgItem(IDC_DEF_EXT_EDIT))->setText(QString::fromWCharArray(ext));

		// NPLX: QLineEdit::setText doesn't emit textEdited, so the dirty state stays as it is
		auto* userExtEdit = qobject_cast<QLineEdit*>(dlgItem(IDC_USER_EXT_EDIT));
		if (userExtEdit->text() != QString::fromWCharArray(userExt))
			userExtEdit->setText(QString::fromWCharArray(userExt));
		enableItem(IDC_SAVECLOSE_BUTTON, _isDirty || _isThemeDirty);
	}
	showItem(IDC_DEF_EXT_EDIT, index != 0);
	showItem(IDC_DEF_EXT_STATIC, index != 0);
	showItem(IDC_USER_EXT_EDIT, index != 0);
	showItem(IDC_USER_EXT_STATIC, index != 0);
	showItem(IDC_PLUSSYMBOL2_STATIC, index != 0);

	const StyleArray& lexerStyler = index ? _lsArray.getLexerFromIndex(static_cast<size_t>(index) - 1) : _globalStyles;

	for (const auto& style : lexerStyler)
	{
		const std::wstring styleDesc = (style._styleDesc);
		stylesList()->addItem(QString::fromStdWString(styleDesc));
	}
	stylesList()->setCurrentRow(0);
	_isUpdatingUi = false;
	setVisualFromStyleList();
}

std::pair<intptr_t, intptr_t> WordStyleDlg::goToPreferencesSettings()
{
	std::pair<intptr_t, intptr_t> result;
	result.first = -1;  // Page
	result.second = -1; // Control

	// NPLX: the order of the Preferences pages, without File Association (Windows only)
	enum preferencesSectionPage {
		general = 0,
		toolbar,
		tabbar,
		edit1,
		edit2,
		darkMode,
		margins,
		newDocument,
		defaultDirectory,
		recentFileHistory,
		language,
		indentation,
		highlighting,
		print,
		searching,
		backup,
		autoCompletion,
		multiInstance,
		delimiter,
		performance,
		cloudAndLink,
		searchEngine,
		misc
	};

	const Style& style = getCurrentStyler();

	// Check if it's one of following Global Styles:
	if (style._styleDesc == L"Current line background colour")
	{
		result.first = edit1;
		result.second = IDC_RADIO_CLM_HILITE;
	}
	else if (style._styleDesc == L"Multi-edit carets color" || style._styleDesc == L"Multi-selected text color")
	{
		result.first = edit2;
		result.second = IDC_CHECK_MULTISELECTION;
	}
	else if (style._styleDesc == L"Caret colour")
	{
		result.first = edit1;
		result.second = IDC_CARETSETTING_STATIC;
	}
	else if (style._styleDesc == L"Edge colour")
	{
		result.first = margins;
		result.second = IDC_COLUMNPOS_EDIT;
	}
	else if (style._styleDesc == L"Line number margin")
	{
		result.first = margins;
		result.second = IDC_CHECK_LINENUMBERMARGE;
	}
	else if (style._styleDesc == L"Bookmark margin")
	{
		result.first = margins;
		result.second = IDC_CHECK_BOOKMARKMARGE;
	}
	else if (style._styleDesc == L"Change History margin" || style._styleDesc == L"Change History modified"
		|| style._styleDesc == L"Change History revert modified" || style._styleDesc == L"Change History revert origin"
		|| style._styleDesc == L"Change History saved")
	{
		result.first = margins;
		result.second = IDC_GB_CHANGHISTORY;
	}
	else if (style._styleDesc == L"Fold" || style._styleDesc == L"Fold active" || style._styleDesc == L"Fold margin")
	{
		result.first = margins;
		result.second = IDC_FMS_GB_STATIC;
	}
	else if (style._styleDesc == L"Smart Highlighting")
	{
		result.first = highlighting;
		result.second = IDC_CHECK_ENABLSMARTHILITE;
	}
	else if (style._styleDesc == L"Tags match highlighting")
	{
		result.first = highlighting;
		result.second = IDC_CHECK_ENABLTAGSMATCHHILITE;
	}
	else if (style._styleDesc == L"Tags attribute")
	{
		result.first = highlighting;
		result.second = IDC_CHECK_ENABLTAGATTRHILITE;
	}
	else if (style._styleDesc == L"Mark Style 1" || style._styleDesc == L"Mark Style 2" || style._styleDesc == L"Mark Style 3"
		|| style._styleDesc == L"Mark Style 4" || style._styleDesc == L"Mark Style 5")
	{
		result.first = highlighting;
		result.second = IDC_MARKALL_STATIC;
	}
	else if (style._styleDesc == L"URL hovered")
	{
		result.first = cloudAndLink;
		result.second = IDC_CHECK_CLICKABLELINK_ENABLE;
	}
	else if (style._styleDesc == L"EOL custom color")
	{
		result.first = edit2;
		result.second = IDC_CHECK_WITHCUSTOMCOLOR_CRLF;
	}
	else if (style._styleDesc == L"Active tab focused indicator" || style._styleDesc == L"Active tab unfocused indicator")
	{
		result.first = tabbar;
		result.second = IDC_CHECK_ORANGE;
	}
	else if (style._styleDesc == L"Inactive tabs")
	{
		result.first = tabbar;
		result.second = IDC_CHECK_DRAWINACTIVE;
	}
	else if (style._styleDesc == g_npcStyleName)
	{
		result.first = edit2;
		result.second = IDC_CHECK_NPC_COLOR;
	}

	return result;
}

void WordStyleDlg::syncWithSelFgSingleColorCtrl()
{
	const Style& style = getCurrentStyler();

	// Selected text colour style
	if (style._styleDesc == L"Selected text colour")
	{
		// Only in case that dialog is on "Selected text colour":
		// Switch to a section then switch back for refresh current state of "Selected text colour"
		goToSection(L"Global Styles:Default Style");
		goToSection(L"Global Styles:Selected text colour");
	}
}

void WordStyleDlg::setVisualFromStyleList()
{
	_isUpdatingUi = true;
	showGlobalOverrideCtrls(false);

	Style & style = getCurrentStyler();

	// Global override style
	if (style._styleDesc == L"Global override")
	{
		showGlobalOverrideCtrls(true);
	}

	std::pair<intptr_t, intptr_t> pageAndCtrlID = goToPreferencesSettings();
	showItem(IDC_GLOBAL_GOTOSETTINGS_LINK, pageAndCtrlID.first != -1);

	const QListWidgetItem* styleItem = stylesList()->currentItem();
	if (languagesCombo()->currentIndex() == -1 || !styleItem)
	{
		_isUpdatingUi = false;
		return;
	}
	_hStyleInfoStaticText->setText(languagesCombo()->currentText() + QStringLiteral(": ") + styleItem->text());

	//-- 2 couleurs : fg et bg
	bool isEnable = false;
	if (HIBYTE(HIWORD(style._fgColor)) != 0xFF)
	{
		_pFgColour->setColour(style._fgColor);
		_pFgColour->setEnabled((style._colorStyle & COLORSTYLE_FOREGROUND) != 0);
		isEnable = true;
	}

	// Selected text colour style
	if (style._styleDesc == L"Selected text colour")
	{
		isEnable = false; // disable by default for "Selected text colour" style

		if (NppParameters::getInstance().getSVP()._selectedTextForegroundSingleColor)
			isEnable = true;
	}
	_pFgColour->QWidget::setEnabled(isEnable);

	isEnable = false;
	if (HIBYTE(HIWORD(style._bgColor)) != 0xFF)
	{
		_pBgColour->setColour(style._bgColor);
		_pBgColour->setEnabled((style._colorStyle & COLORSTYLE_BACKGROUND) != 0);
		isEnable = true;
	}
	_pBgColour->QWidget::setEnabled(isEnable);

	//-- font name
	int iFontName = 0;
	if (!style._fontName.empty())
	{
		iFontName = _hFontNameCombo->findText(QString::fromStdWString(style._fontName), Qt::MatchFixedString);
		if (iFontName == -1)
			iFontName = 0;
	}

	_hFontNameCombo->setCurrentIndex(iFontName);
	_hFontNameCombo->setEnabled(style._isFontEnabled);

	//-- font size
	int iFontSize = 0;
	if (style._fontSize != STYLE_NOT_USED && style._fontSize < 100) // style._fontSize has only 2 digits
	{
		iFontSize = _hFontSizeCombo->findText(QString::number(style._fontSize));
		if (iFontSize == -1)
			iFontSize = 0;
	}
	_hFontSizeCombo->setCurrentIndex(iFontSize);
	_hFontSizeCombo->setEnabled(style._isFontEnabled);

	//-- font style : bold & italic
	if (style._fontStyle != STYLE_NOT_USED)
	{
		setChecked(IDC_BOLD_CHECK, (style._fontStyle & FONTSTYLE_BOLD) != 0);
		setChecked(IDC_ITALIC_CHECK, (style._fontStyle & FONTSTYLE_ITALIC) != 0);
		setChecked(IDC_UNDERLINE_CHECK, (style._fontStyle & FONTSTYLE_UNDERLINE) != 0);
	}
	else // STYLE_NOT_USED : reset them all
	{
		setChecked(IDC_BOLD_CHECK, false);
		setChecked(IDC_ITALIC_CHECK, false);
		setChecked(IDC_UNDERLINE_CHECK, false);
	}

	enableFontStyle(style._isFontEnabled);


	//-- Default Keywords
	bool shouldBeDisplayed = style._keywordClass != STYLE_NOT_USED;
	if (shouldBeDisplayed)
	{
		LexerStyler & lexerStyler = _lsArray.getLexerFromIndex(static_cast<size_t>(_currentLexerIndex) - 1);

		NppParameters& nppParams = NppParameters::getInstance();
		LangType lType = nppParams.getLangIDFromStr(lexerStyler.getLexerName());
		const char* kws = nppParams.getWordList(lType, style._keywordClass);
		if (!kws)
			kws = "";
		qobject_cast<QPlainTextEdit*>(dlgItem(IDC_DEF_KEYWORDS_EDIT))->setPlainText(QString::fromStdWString(string2wstring(kws, CP_UTF8)));

		const std::wstring ckwStr = string2wstring(style._keywords, CP_UTF8);
		qobject_cast<QPlainTextEdit*>(dlgItem(IDC_USER_KEYWORDS_EDIT))->setPlainText(QString::fromStdWString(ckwStr));
	}

	showItem(IDC_DEF_KEYWORDS_EDIT, shouldBeDisplayed);
	showItem(IDC_USER_KEYWORDS_EDIT, shouldBeDisplayed);
	showItem(IDC_DEF_KEYWORDS_STATIC, shouldBeDisplayed);
	showItem(IDC_USER_KEYWORDS_STATIC, shouldBeDisplayed);
	showItem(IDC_PLUSSYMBOL_STATIC, shouldBeDisplayed);

	_isUpdatingUi = false;
}

void WordStyleDlg::prepare2Cancel()
{
	_styles2restored = (NppParameters::getInstance()).getLStylerArray();
	_gstyles2restored = (NppParameters::getInstance()).getGlobalStylers();
	_gOverride2restored = (NppParameters::getInstance()).getGlobalOverrideStyle();
}

void WordStyleDlg::restoreGlobalOverrideValues() const
{
	GlobalOverride& gOverride = (NppParameters::getInstance()).getGlobalOverrideStyle();
	gOverride = _gOverride2restored;
}

void WordStyleDlg::apply(int applicationInfo)
{
	LexerStylerArray & lsa = (NppParameters::getInstance()).getLStylerArray();
	lsa = _lsArray;

	StyleArray & globalStyles = (NppParameters::getInstance()).getGlobalStylers();
	globalStyles = _globalStyles;

	if ((applicationInfo & THEME_CHANGE) != 0)
		_isThemeChanged = true;

	if (applicationInfo & GENERAL_CHANGE || applicationInfo & THEME_CHANGE)
		_sendToParent(WM_UPDATESCINTILLAS, (applicationInfo & THEME_CHANGE) != 0, 0);
	if (applicationInfo & COLOR_CHANGE_4_MENU)
		_sendToParent(WM_UPDATEMAINMENUBITMAPS, 0, 0);
}

void WordStyleDlg::addLastThemeEntry() const
{
	NppParameters& nppParamInst = NppParameters::getInstance();
	ThemeSwitcher& themeSwitcher = nppParamInst.getThemeSwitcher();
	std::pair<wstring, wstring>& themeInfo = themeSwitcher.getElementFromIndex(themeSwitcher.size() - 1);
	_hSwitch2ThemeCombo->addItem(QString::fromStdWString(themeInfo.first));
}

Style& WordStyleDlg::getCurrentStyler()
{
	int styleIndex = stylesList() ? stylesList()->currentRow() : -1;
	if (styleIndex == -1)
		styleIndex = 0;

	try {
		if (_currentLexerIndex == 0)
		{
			return _globalStyles.getStyler(styleIndex);
		}
		else
		{
			LexerStyler& lexerStyler = _lsArray.getLexerFromIndex(static_cast<size_t>(_currentLexerIndex) - 1);
			return lexerStyler.getStyler(styleIndex);
		}
	}
	catch (...)
	{
		return _globalStyles.getStyler(0);
	}
}

void WordStyleDlg::enableFontStyle(bool isEnable) const
{
	enableItem(IDC_BOLD_CHECK, isEnable);
	enableItem(IDC_ITALIC_CHECK, isEnable);
	enableItem(IDC_UNDERLINE_CHECK, isEnable);
}

long WordStyleDlg::notifyDataModified()
{
	_isDirty = true;
	_isThemeDirty = true;
	enableItem(IDC_SAVECLOSE_BUTTON, true);
	return TRUE;
}

void WordStyleDlg::showGlobalOverrideCtrls(bool show)
{
	if (show)
	{
		updateGlobalOverrideCtrls();
	}
	showItem(IDC_GLOBAL_FG_CHECK, show);
	showItem(IDC_GLOBAL_BG_CHECK, show);
	showItem(IDC_GLOBAL_FONT_CHECK, show);
	showItem(IDC_GLOBAL_FONTSIZE_CHECK, show);
	showItem(IDC_GLOBAL_BOLD_CHECK, show);
	showItem(IDC_GLOBAL_ITALIC_CHECK, show);
	showItem(IDC_GLOBAL_UNDERLINE_CHECK, show);
	showItem(IDC_GLOBAL_WHATISGLOBALOVERRIDE_LINK, show);
	_isShownGOCtrls = show;
}
