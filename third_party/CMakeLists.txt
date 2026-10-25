# NotePadLX third-party libraries.
# Scintilla and Lexilla are the Notepad++-patched copies (see UPSTREAM.md); never replace
# them with distro packages.

# ---------------------------------------------------------------------------
# Lexilla (static), including N++ custom lexers: LexUser ("user"),
# LexSearchResult ("searchResult"), LexObjC ("objc")
# ---------------------------------------------------------------------------
file(GLOB LEXILLA_LEXLIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lexilla/lexlib/*.cxx)
file(GLOB LEXILLA_LEXER_SOURCES  CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lexilla/lexers/*.cxx)

add_library(lexilla STATIC
    ${LEXILLA_LEXLIB_SOURCES}
    ${LEXILLA_LEXER_SOURCES}
    lexilla/src/Lexilla.cxx
)
target_include_directories(lexilla
    PUBLIC  lexilla/include
    PRIVATE lexilla/lexlib scintilla/include
)
target_compile_definitions(lexilla PRIVATE NDEBUG_LEXILLA_ASSERT)
set_target_properties(lexilla PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Upstream code: do not let our warning flags break the build
target_compile_options(lexilla PRIVATE -w)

# ---------------------------------------------------------------------------
# Scintilla core + Qt backend (ScintillaEditBase) + Boost.Regex search engine
# ---------------------------------------------------------------------------
file(GLOB SCINTILLA_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scintilla/src/*.cxx)

add_library(scintilla_qt STATIC
    ${SCINTILLA_CORE_SOURCES}
    scintilla/qt/ScintillaEditBase/PlatQt.cpp
    scintilla/qt/ScintillaEditBase/ScintillaQt.cpp
    scintilla/qt/ScintillaEditBase/ScintillaQt.h
    scintilla/qt/ScintillaEditBase/ScintillaEditBase.cpp
    scintilla/qt/ScintillaEditBase/ScintillaEditBase.h
    boostregex/BoostRegExSearch.cxx
    boostregex/UTF8DocumentIterator.cxx
)
set_target_properties(scintilla_qt PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(scintilla_qt
    PUBLIC  scintilla/include scintilla/qt/ScintillaEditBase scintilla/src
    PRIVATE boostregex
)
target_compile_definitions(scintilla_qt
    PUBLIC  SCINTILLA_QT=1 SCI_OWNREGEX
    PRIVATE MAKING_LIBRARY=1 BOOST_REGEX_STANDALONE
)
target_link_libraries(scintilla_qt PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Core5Compat)
target_compile_options(scintilla_qt PRIVATE -w)

# ---------------------------------------------------------------------------
# pugixml (XML for config/langs/stylers/session...), nlohmann json, uchardet
# ---------------------------------------------------------------------------
add_library(pugixml STATIC pugixml/pugixml.cpp)
target_include_directories(pugixml PUBLIC pugixml)
set_target_properties(pugixml PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(pugixml PRIVATE -w)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE json)

file(GLOB UCHARDET_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/uchardet/*.cpp)
add_library(uchardet STATIC ${UCHARDET_SOURCES})
target_include_directories(uchardet PUBLIC uchardet)
set_target_properties(uchardet PROPERTIES POSITION_INDEPENDENT_CODE ON)
# NPLX: upstream relies on MSVC including <cstdint> implicitly
target_compile_options(uchardet PRIVATE -w -include cstdint)
