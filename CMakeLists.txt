# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.21)
project(eudaimonia VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

# MruTabWidget and QxFileDialog (github.com/siplasplas/qt-extra). An installed
# qt-extra 2.7 or later is used; otherwise CMake fetches it.
include(FetchContent)
FetchContent_Declare(qt-extra
    GIT_REPOSITORY https://github.com/siplasplas/qt-extra.git
    GIT_TAG v2.7.0
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS 2.7)
FetchContent_MakeAvailable(qt-extra)

include(GNUInstallDirs)

# Plugins are loaded from lib/eudaimonia/plugins next to bin when installed,
# from <build>/plugins in the build tree, and from EUDAIMONIA_PLUGIN_PATH.
set(EUDAIMONIA_PLUGIN_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/eudaimonia/plugins")
set(EUDAIMONIA_PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/plugins")
file(RELATIVE_PATH EUDAIMONIA_PLUGIN_DIR_FROM_BIN
     "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
     "${CMAKE_INSTALL_PREFIX}/${EUDAIMONIA_PLUGIN_INSTALL_DIR}")

# Shared plugins (github.com/siplasplas/siplasplas-plugins, LGPL-3.0-only):
# the editor, spp-editor, which builds qcodeedit in (fetched, with cpg and
# DiffMerge's diff library; needs ICU, libicu-dev). Built into the plugin
# directories above. FETCHCONTENT_SOURCE_DIR_SIPLASPLAS_PLUGINS may name a
# local checkout.
set(SPP_PLUGIN_BUILD_DIR "${EUDAIMONIA_PLUGIN_BUILD_DIR}")
set(SPP_PLUGIN_INSTALL_DIR "${EUDAIMONIA_PLUGIN_INSTALL_DIR}")
set(SPP_BUILD_EDITOR ON)
set(SPP_BUILD_ARCHIVE OFF)
FetchContent_Declare(siplasplas_plugins
    GIT_REPOSITORY https://github.com/siplasplas/siplasplas-plugins.git
    GIT_TAG v0.5.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(siplasplas_plugins)

add_executable(eudaimonia
    src/app/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/PluginManager.cpp
    src/app/PluginManager.h
    src/app/RecentProjects.cpp
    src/app/RecentProjects.h
    resources/eudaimonia.qrc)
target_compile_definitions(eudaimonia PRIVATE
    EUDAIMONIA_VERSION="${PROJECT_VERSION}"
    EUDAIMONIA_PLUGIN_DIR_FROM_BIN="${EUDAIMONIA_PLUGIN_DIR_FROM_BIN}")
target_link_libraries(eudaimonia PRIVATE Qt6::Widgets qt-extra siplasplas::plugin-api)

# Help → About names the commit built and its time, and those of the
# components built in, so that it is plain which versions a build uses:
# build_info.h, written at every build (cmake/build_info.cmake).
set(EUDAIMONIA_COMPONENTS)
get_target_property(qt_extra_imported qt-extra IMPORTED)
if(qt_extra_imported)
    list(APPEND EUDAIMONIA_COMPONENTS "qt-extra=@${qt-extra_VERSION} (installed)")
else()
    list(APPEND EUDAIMONIA_COMPONENTS "qt-extra=${qt-extra_SOURCE_DIR}")
endif()
list(APPEND EUDAIMONIA_COMPONENTS "siplasplas-plugins=${siplasplas_plugins_SOURCE_DIR}")
# Fetched by the editor plugin, which builds them in.
foreach(component qcodeedit cpg)
    FetchContent_GetProperties(${component} SOURCE_DIR component_dir)
    if(component_dir)
        list(APPEND EUDAIMONIA_COMPONENTS "${component}=${component_dir}")
    endif()
endforeach()
# '|' between the components: ';' would split the command's argument.
string(REPLACE ";" "|" EUDAIMONIA_COMPONENTS "${EUDAIMONIA_COMPONENTS}")
add_custom_target(eudaimonia_build_info ALL
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        "-DCOMPONENTS=${EUDAIMONIA_COMPONENTS}"
        -DOUTPUT=${CMAKE_BINARY_DIR}/generated/build_info.h
        -P ${CMAKE_SOURCE_DIR}/cmake/build_info.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/build_info.h
    COMMENT "Naming the commits built"
    VERBATIM)
add_dependencies(eudaimonia eudaimonia_build_info)
target_include_directories(eudaimonia PRIVATE ${CMAKE_BINARY_DIR}/generated)
# Without the editor plugin no file opens: build it with the program.
add_dependencies(eudaimonia spp-editor)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(eudaimonia PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS eudaimonia RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# The launcher and its icon (resources/ATTRIBUTIONS.md).
install(FILES resources/eudaimonia.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icons/eudaimonia.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
# Refresh the icon and desktop file caches, so that a running desktop (e.g.
# GNOME on Wayland, whose shell cannot be restarted without logging out)
# shows the launcher and its icon at once, as ducat does. Skipped for
# packaging (DESTDIR) and when the tools are missing.
install(CODE "
    if(NOT \"\$ENV{DESTDIR}\")
        set(data \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}\")
        find_program(gtk_update_icon_cache gtk-update-icon-cache)
        find_program(update_desktop_database update-desktop-database)
        if(gtk_update_icon_cache)
            message(STATUS \"Updating the icon cache of \${data}/icons/hicolor\")
            execute_process(COMMAND \"\${gtk_update_icon_cache}\" -f -t
                            \"\${data}/icons/hicolor\" OUTPUT_QUIET ERROR_QUIET)
        endif()
        if(update_desktop_database)
            message(STATUS \"Updating the desktop file database of \${data}/applications\")
            execute_process(COMMAND \"\${update_desktop_database}\"
                            \"\${data}/applications\" OUTPUT_QUIET ERROR_QUIET)
        endif()
    endif()
")

option(EUDAIMONIA_BUILD_TESTS "Build fast offline tests" ON)
if(EUDAIMONIA_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(test_recent_projects tests/test_recent_projects.cpp src/app/RecentProjects.cpp)
    target_include_directories(test_recent_projects PRIVATE src/app)
    target_link_libraries(test_recent_projects PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME recent_projects COMMAND test_recent_projects)
    set_tests_properties(recent_projects PROPERTIES TIMEOUT 20)

    # Two plugins with the same IID, in their own directories.
    foreach(name first second)
        add_library(test_plugin_${name} MODULE tests/plugin/TestPlugin.cpp tests/plugin/TestInterface.h)
        target_compile_definitions(test_plugin_${name} PRIVATE TEST_PLUGIN_NAME="${name}")
        target_link_libraries(test_plugin_${name} PRIVATE Qt6::Core)
        set_target_properties(test_plugin_${name} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-plugins/${name}")
    endforeach()
    add_executable(test_plugin_manager tests/test_plugin_manager.cpp src/app/PluginManager.cpp)
    target_include_directories(test_plugin_manager PRIVATE src/app tests)
    target_compile_definitions(test_plugin_manager PRIVATE
        EUDAIMONIA_PLUGIN_DIR_FROM_BIN="${EUDAIMONIA_PLUGIN_DIR_FROM_BIN}"
        TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/test-plugins")
    target_link_libraries(test_plugin_manager PRIVATE Qt6::Core Qt6::Test)
    add_dependencies(test_plugin_manager test_plugin_first test_plugin_second)
    add_test(NAME plugin_manager COMMAND test_plugin_manager)
    set_tests_properties(plugin_manager PROPERTIES TIMEOUT 20)
endif()
