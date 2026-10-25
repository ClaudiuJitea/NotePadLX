cmake_minimum_required(VERSION 3.24)

project(NotePadLX
    VERSION 0.3
    DESCRIPTION "Linux port of Notepad++"
    HOMEPAGE_URL "https://github.com/ClaudiuJitea/NotePadLX"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(NPLX_BUILD_TESTS "Build NotePadLX tests" ON)
# A development build also finds data/ in the source tree; packages use only <prefix>/share/notepadlx
option(NPLX_USE_SOURCE_DATA_DIR "Fall back to the source tree's data/ directory" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Core5Compat DBus Network)

add_subdirectory(third_party)
add_subdirectory(src)

if(NPLX_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

# ---- Packaging (Phase 10): cpack -G DEB  /  packaging/build-appimage.sh
set(CPACK_PACKAGE_NAME "notepadlx")
set(CPACK_PACKAGE_VENDOR "Claudiu Jitea")
set(CPACK_PACKAGE_CONTACT "Claudiu Jitea <51474241+ClaudiuJitea@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Source code editor, a Linux port of Notepad++")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "notepadlx_${PROJECT_VERSION}_amd64")
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# not a link dependency: the toolbar and dialog icons are SVG (Qt image format plugin)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-svg-plugins")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
# the extended description (CPack puts CPACK_PACKAGE_DESCRIPTION_SUMMARY on the first line and indents the rest)
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "NotePadLX brings Notepad++ to Linux as a native Qt 6 application: Scintilla and
Lexilla editing with syntax highlighting for 80+ languages, Notepad++ themes,
user defined languages, Find/Replace with Find in Files and the Finder, macros,
the Run menu, the function list, document map, folder as workspace, project
panels, and Notepad++ compatible configuration files.")
include(CPack)
