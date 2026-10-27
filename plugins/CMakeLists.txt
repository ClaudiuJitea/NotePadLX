# The NotePadLX plugin SDK (the headers of src/plugins, installed in <prefix>/include/notepadlx) and the samples.
#
# An out-of-tree plugin needs only those headers (and Qt 6 Widgets for a docking panel):
#     add_library(MyPlugin MODULE MyPlugin.cpp)
#     set_target_properties(MyPlugin PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
#     target_include_directories(MyPlugin PRIVATE /usr/include/notepadlx)
# and is installed as ~/.local/share/notepadlx/plugins/MyPlugin/MyPlugin.so (see docs/PLUGINS.md).

add_library(notepadlx_plugin_sdk INTERFACE)
target_include_directories(notepadlx_plugin_sdk INTERFACE
    ${PROJECT_SOURCE_DIR}/src/plugins
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/core/platform
    ${PROJECT_SOURCE_DIR}/third_party/scintilla/include)

# <build>/sample-plugins/<Name>/<Name>.so, the layout NotePadLX loads (NPLX_PLUGIN_PATH=<build>/sample-plugins)
function(nplx_add_sample_plugin name)
    add_library(${name} MODULE ${ARGN})
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sample-plugins/${name})
    target_link_libraries(${name} PRIVATE notepadlx_plugin_sdk ${CMAKE_DL_LIBS})
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror=return-type)
endfunction()

if(NPLX_BUILD_SAMPLE_PLUGINS)
    nplx_add_sample_plugin(HelloWorld samples/HelloWorld/HelloWorld.cpp samples/HelloWorld/HelloWorld.h)
    foreach(icon HelloWorld.svg HelloWorld-dark.svg)
        configure_file(samples/HelloWorld/${icon} ${CMAKE_BINARY_DIR}/sample-plugins/HelloWorld/${icon} COPYONLY)
    endforeach()

    nplx_add_sample_plugin(DockingSample samples/DockingSample/DockingSample.cpp)
    target_link_libraries(DockingSample PRIVATE Qt6::Widgets)
    set_target_properties(DockingSample PROPERTIES AUTOMOC OFF)
endif()
