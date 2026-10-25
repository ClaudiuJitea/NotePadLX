set(NPLX_WARNINGS -Wall -Wextra -Werror=return-type)

# Core: toolkit-free ported logic (no Qt, no windows.h)
add_library(nplx_core STATIC
    core/platform/Encoding.cpp
    core/platform/Paths.cpp
    core/hash/sha-256.cpp
    core/hash/sha512.cpp
    core/hash/sha1.cpp
    core/hash/calc_sha1.cpp
    core/Common.cpp
    core/FileInterface.cpp
    core/EncodingMapper.cpp
    core/Utf8_16.cpp
    core/UiServices.cpp
    core/NppDarkMode.cpp
    core/localization.cpp
    core/shortcut.cpp
    core/UdlMappers.cpp
    core/LangNameInfo.cpp
    core/Parameters.cpp
)
target_include_directories(nplx_core PUBLIC core core/platform core/hash)
target_link_libraries(nplx_core PUBLIC pugixml uchardet nlohmann_json)
# Scintilla/Lexilla headers only (Scintilla.h, SciLexer.h, ILexer.h)
target_include_directories(nplx_core PUBLIC
    ${PROJECT_SOURCE_DIR}/third_party/scintilla/include
    ${PROJECT_SOURCE_DIR}/third_party/lexilla/include)
if(NPLX_USE_SOURCE_DATA_DIR)
    target_compile_definitions(nplx_core PRIVATE NPLX_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
endif()
# -Wno-bidi-chars: upstream UI strings (localizationString.h) contain intentional RTL marks
target_compile_options(nplx_core PRIVATE ${NPLX_WARNINGS} -Wno-bidi-chars)

# Editor component: ScintillaEditView on top of ScintillaEditBase (Qt) + Lexilla
add_library(nplx_editor STATIC
    editor/ScintillaEditView.cpp
    editor/ScintillaEditView.h
    editor/ScintillaEditViewNplx.cpp
    editor/xmlMatchedTagsHighlighter.cpp
    editor/SortLocale.cpp
    editor/UrlDetection.cpp
    editor/UrlDetection.h
    editor/AutoCompletion.cpp
    editor/AutoCompletion.h
    editor/FunctionCallTip.cpp
    editor/functionParser.cpp
    editor/functionParser.h
    editor/FunctionCallTip.h
    editor/SortLocale.h
    editor/xmlMatchedTagsHighlighter.h
    editor/Sorters.h
    editor/Buffer.cpp
    editor/Buffer.h
    editor/NppBufferHost.h
)
set_target_properties(nplx_editor PROPERTIES AUTOMOC ON)
target_include_directories(nplx_editor PUBLIC editor ${PROJECT_SOURCE_DIR}/third_party/boostregex)
target_link_libraries(nplx_editor PUBLIC nplx_core scintilla_qt lexilla)
target_compile_options(nplx_editor PRIVATE ${NPLX_WARNINGS})

# UI: Qt port of Notepad_plus (main window, menus, tabs, commands)
add_library(nplx_ui STATIC
    ui/Notepad_plus.cpp
    ui/Notepad_plus.h
    ui/NppIO.cpp
    ui/NppCommands.cpp
    ui/NppSession.cpp
    ui/LastRecentFileList.cpp
    ui/LastRecentFileList.h
    ui/DocTabView.cpp
    ui/DocTabView.h
    ui/FindReplaceDlg.cpp
    ui/FindReplaceDlg.h
    ui/NppEdit.cpp
    ui/NppSearch.cpp
    ui/SmartHighlighter.cpp
    ui/ContextMenuQt.cpp
    ui/ContextMenuQt.h
    ui/QtNppMenu.cpp
    ui/GoToLineDlg.cpp
    ui/ColourPicker.cpp
    ui/ColourPicker.h
    ui/WordStyleDlg.cpp
    ui/WordStyleDlg.h
    ui/GoToLineDlg.h
    ui/AnsiCharPanel.cpp
    ui/AnsiCharPanel.h
    ui/ClipboardHistoryPanel.cpp
    ui/ClipboardHistoryPanel.h
    ui/DesktopIntegration.cpp
    ui/DesktopIntegration.h
    ui/DockingPanel.cpp
    ui/DockingPanel.h
    ui/DocumentMap.cpp
    ui/DocumentMap.h
    ui/FileBrowser.cpp
    ui/FileBrowser.h
    ui/fileBrowser_rc.h
    ui/FunctionListPanel.cpp
    ui/FunctionListPanel.h
    ui/NplxStyle.cpp
    ui/NplxStyle.h
    ui/NppPanels.cpp
    ui/ProjectPanel.cpp
    ui/ProjectPanel.h
    ui/ProjectPanel_rc.h
    ui/RcDialog.cpp
    ui/RcDialog.h
    ui/RcDialogs.inc
    ui/TreeView.cpp
    ui/TreeView.h
    ui/UserDefineDialog.cpp
    ui/VerticalFileSwitcher.cpp
    ui/VerticalFileSwitcher.h
    ui/TaskListInfo.h
    ui/TaskListDlg.cpp
    ui/TaskListDlg.h
    ui/WindowsDlg.cpp
    ui/WindowsDlg.h
    ui/UserDefineDialog.h
    ui/QtNppMenu.h
    ui/SmartHighlighter.h
    ui/KeyMap.cpp
    ui/KeyMap.h
    ui/QtUiServices.cpp
    ui/QtUiServices.h
    ui/MainMenuDef.inc
    ui/NppMacro.cpp
    ui/ParentMessenger.h
    ui/RcShortcutRunDialogs.inc
    ui/RunDlg.cpp
    ui/RunDlg.h
    ui/RunDlg_rc.h
    ui/RunMacroDlg.cpp
    ui/RunMacroDlg.h
    ui/RunMacroDlg_rc.h
    ui/ShortcutDlg.cpp
    ui/md5Dlgs.cpp
    ui/columnEditor.cpp
    ui/columnEditor.h
    ui/columnEditor_rc.h
    ui/RcColumnEditor.inc
    ui/md5Dlgs.h
    ui/md5Dlgs_rc.h
    ui/RcHashDialogs.inc
    ui/ShortcutDlg.h
    ui/shortcutRc.h
)
set_target_properties(nplx_ui PROPERTIES AUTOMOC ON)

# Ported menu commands, from the case labels of Notepad_plus::command (see cmake/GenPortedCommands.cmake)
set(NPLX_PORTED_COMMANDS_INC ${CMAKE_CURRENT_BINARY_DIR}/generated/PortedCommands.inc)
add_custom_command(
    OUTPUT ${NPLX_PORTED_COMMANDS_INC}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/ui/NppCommands.cpp
            -DOUTPUT=${NPLX_PORTED_COMMANDS_INC} -P ${PROJECT_SOURCE_DIR}/cmake/GenPortedCommands.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/NppCommands.cpp ${PROJECT_SOURCE_DIR}/cmake/GenPortedCommands.cmake
    COMMENT "Generating the list of ported commands"
    VERBATIM)
target_sources(nplx_ui PRIVATE ${NPLX_PORTED_COMMANDS_INC})
target_include_directories(nplx_ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(nplx_ui PUBLIC ui)
target_link_libraries(nplx_ui PUBLIC nplx_editor Qt6::Widgets Qt6::DBus Qt6::Network)
target_compile_options(nplx_ui PRIVATE ${NPLX_WARNINGS})
target_compile_definitions(nplx_ui PUBLIC NPLX_HOMEPAGE_URL="${PROJECT_HOMEPAGE_URL}")

# Resources: application icon (:/icons/...) and upstream icons (:/npp/tabbar, :/npp/toolbar-*)
file(GLOB NPLX_APP_ICONS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/icons/notepadlx-*.png)
list(FILTER NPLX_APP_ICONS EXCLUDE REGEX "1024")
file(GLOB NPLX_NPP_ICONS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/npp/*/*.ico)
# NotePadLX icons (SVG templates, see resources/nplx-icons/README.md); rendered by the qt6-svg image format plugin
file(GLOB NPLX_SVG_ICONS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/nplx-icons/*.svg)
qt_add_resources(nplx_ui "nplx_icons" PREFIX "/" BASE ${PROJECT_SOURCE_DIR}/resources
    FILES ${NPLX_APP_ICONS} ${NPLX_NPP_ICONS} ${NPLX_SVG_ICONS})

# Application
add_executable(notepadlx
    app/main.cpp
    app/CmdLine.cpp
    app/CmdLine.h
    app/SingleInstance.cpp
    app/SingleInstance.h
)
set_target_properties(notepadlx PROPERTIES AUTOMOC ON)
target_compile_definitions(notepadlx PRIVATE NPLX_VERSION="${PROJECT_VERSION}")
target_link_libraries(notepadlx PRIVATE nplx_ui)
target_compile_options(notepadlx PRIVATE ${NPLX_WARNINGS})


include(GNUInstallDirs)
install(TARGETS notepadlx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/notepadlx)
install(FILES ${PROJECT_SOURCE_DIR}/packaging/notepadlx.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${PROJECT_SOURCE_DIR}/packaging/io.github.ClaudiuJitea.NotePadLX.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES ${PROJECT_SOURCE_DIR}/packaging/notepadlx.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES ${PROJECT_SOURCE_DIR}/README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/notepadlx)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/notepadlx RENAME copyright)
foreach(size 16 22 24 32 48 64 128 256 512)
    install(FILES ${PROJECT_SOURCE_DIR}/resources/icons/notepadlx-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME notepadlx.png)
endforeach()
