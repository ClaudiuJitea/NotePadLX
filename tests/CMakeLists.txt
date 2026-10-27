add_executable(tst_phase0 tst_phase0.cpp)
set_target_properties(tst_phase0 PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_phase0 PRIVATE NPLX_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(tst_phase0 PRIVATE nplx_editor Qt6::Test)

add_test(NAME phase0 COMMAND tst_phase0)
set_tests_properties(phase0 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 1: toolkit-free core
add_executable(tst_core tst_core.cpp)
set_target_properties(tst_core PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_core PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_core PRIVATE nplx_core Qt6::Test)

add_test(NAME core COMMAND tst_core)

# Phase 1: FileManager / Buffer (needs a Scintilla view, so Qt widgets offscreen)
add_executable(tst_filemanager tst_filemanager.cpp)
set_target_properties(tst_filemanager PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_filemanager PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_filemanager PRIVATE nplx_editor Qt6::Test)

add_test(NAME filemanager COMMAND tst_filemanager)
set_tests_properties(filemanager PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 2: main window (Qt port of Notepad_plus)
add_executable(tst_mainwindow tst_mainwindow.cpp)
set_target_properties(tst_mainwindow PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_mainwindow PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_mainwindow PRIVATE nplx_ui Qt6::Test)

add_test(NAME mainwindow COMMAND tst_mainwindow)
set_tests_properties(mainwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Start-up: files from the command line opened before the window shows
add_executable(tst_launch tst_launch.cpp)
set_target_properties(tst_launch PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_launch PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_launch PRIVATE nplx_ui Qt6::Test)

add_test(NAME launch COMMAND tst_launch)
set_tests_properties(launch PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 4: Find / Replace dialog, search engine, Finder, Find in Files
add_executable(tst_findreplace tst_findreplace.cpp)
set_target_properties(tst_findreplace PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_findreplace PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_findreplace PRIVATE nplx_ui Qt6::Test)

add_test(NAME findreplace COMMAND tst_findreplace)
set_tests_properties(findreplace PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Ported menu commands (NppCommands.cpp, NppEdit.cpp)
add_executable(tst_commands tst_commands.cpp)
set_target_properties(tst_commands PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_commands PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_commands PRIVATE nplx_ui Qt6::Test)

add_test(NAME commands COMMAND tst_commands)
set_tests_properties(commands PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 5: URL detection (upstream Test/UrlDetection data)
add_executable(tst_urldetection tst_urldetection.cpp)
set_target_properties(tst_urldetection PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_urldetection PRIVATE NPLX_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(tst_urldetection PRIVATE nplx_editor Qt6::Test)
add_test(NAME urldetection COMMAND tst_urldetection)

# Phase 5: Style Configurator, themes
add_executable(tst_styles tst_styles.cpp)
set_target_properties(tst_styles PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_styles PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_styles PRIVATE nplx_ui Qt6::Test)
add_test(NAME styles COMMAND tst_styles)
set_tests_properties(styles PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# NotePadLX look: icons, style, stylesheet
add_executable(tst_look tst_look.cpp)
set_target_properties(tst_look PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_look PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_look PRIVATE nplx_ui Qt6::Test)
add_test(NAME look COMMAND tst_look)
set_tests_properties(look PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 6: Function List (upstream Test/FunctionList unit tests), docking layout
add_executable(tst_functionlist tst_functionlist.cpp)
set_target_properties(tst_functionlist PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_functionlist PRIVATE
    NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
    NPLX_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    NPLX_APP_PATH="$<TARGET_FILE:notepadlx>")
target_link_libraries(tst_functionlist PRIVATE nplx_ui Qt6::Test)
add_dependencies(tst_functionlist notepadlx)
add_test(NAME functionlist COMMAND tst_functionlist)
set_tests_properties(functionlist PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 6: docking panels
add_executable(tst_panels tst_panels.cpp)
set_target_properties(tst_panels PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_panels PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_panels PRIVATE nplx_ui Qt6::Test)
add_test(NAME panels COMMAND tst_panels)
set_tests_properties(panels PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 7: macros, Run dialog, Shortcut dialog
add_executable(tst_macro tst_macro.cpp)
set_target_properties(tst_macro PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_macro PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_macro PRIVATE nplx_ui Qt6::Test)
add_test(NAME macro COMMAND tst_macro)
set_tests_properties(macro PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 7: Tools (hashes) and Edit tools
add_executable(tst_tools tst_tools.cpp)
set_target_properties(tst_tools PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_tools PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_tools PRIVATE nplx_ui Qt6::Test)
add_test(NAME tools COMMAND tst_tools)
set_tests_properties(tools PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# README screenshots (docs/screenshots); see the header of tst_screenshots.cpp
add_executable(tst_screenshots tst_screenshots.cpp)
set_target_properties(tst_screenshots PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_screenshots PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
    NPLX_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(tst_screenshots PRIVATE nplx_ui Qt6::Test)
add_test(NAME screenshots COMMAND tst_screenshots)
set_tests_properties(screenshots PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 7: Shortcut Mapper
add_executable(tst_shortcuts tst_shortcuts.cpp)
set_target_properties(tst_shortcuts PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_shortcuts PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_shortcuts PRIVATE nplx_ui Qt6::Test)
add_test(NAME shortcuts COMMAND tst_shortcuts)
set_tests_properties(shortcuts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# the Notepad++ default keys, end to end
add_executable(tst_keys tst_keys.cpp)
set_target_properties(tst_keys PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_keys PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_keys PRIVATE nplx_ui Qt6::Test)
add_test(NAME keys COMMAND tst_keys)
set_tests_properties(keys PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_preferences tst_preferences.cpp)
set_target_properties(tst_preferences PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_preferences PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_preferences PRIVATE nplx_ui Qt6::Test)
add_test(NAME preferences COMMAND tst_preferences)
set_tests_properties(preferences PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 8: the interface language, switched at runtime
add_executable(tst_localization tst_localization.cpp)
set_target_properties(tst_localization PROPERTIES AUTOMOC ON)
target_compile_definitions(tst_localization PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_link_libraries(tst_localization PRIVATE nplx_ui Qt6::Test)
add_test(NAME localization COMMAND tst_localization)
set_tests_properties(localization PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Phase 9: the plugin host, with the sample plugins (plugins/samples)
if(NPLX_BUILD_SAMPLE_PLUGINS)
    add_executable(tst_plugins tst_plugins.cpp)
    set_target_properties(tst_plugins PROPERTIES AUTOMOC ON)
    target_compile_definitions(tst_plugins PRIVATE NPLX_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
        NPLX_SAMPLE_PLUGINS_DIR="${CMAKE_BINARY_DIR}/sample-plugins")
    target_include_directories(tst_plugins PRIVATE ${PROJECT_SOURCE_DIR}/plugins/samples/HelloWorld)
    target_link_libraries(tst_plugins PRIVATE nplx_ui Qt6::Test)
    add_dependencies(tst_plugins HelloWorld DockingSample)
    add_test(NAME plugins COMMAND tst_plugins)
    set_tests_properties(plugins PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
