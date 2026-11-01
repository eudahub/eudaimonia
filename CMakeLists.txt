# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.21)
project(eudaimonia VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
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

add_executable(eudaimonia
    src/app/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/RecentProjects.cpp
    src/app/RecentProjects.h)
target_compile_definitions(eudaimonia PRIVATE
    EUDAIMONIA_VERSION="${PROJECT_VERSION}")
target_link_libraries(eudaimonia PRIVATE Qt6::Widgets qt-extra)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(eudaimonia PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS eudaimonia RUNTIME DESTINATION bin)

option(EUDAIMONIA_BUILD_TESTS "Build fast offline tests" ON)
if(EUDAIMONIA_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(test_recent_projects tests/test_recent_projects.cpp src/app/RecentProjects.cpp)
    target_include_directories(test_recent_projects PRIVATE src/app)
    target_link_libraries(test_recent_projects PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME recent_projects COMMAND test_recent_projects)
    set_tests_properties(recent_projects PROPERTIES TIMEOUT 20)
endif()
