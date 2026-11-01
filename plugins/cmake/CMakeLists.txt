# SPDX-License-Identifier: GPL-3.0-only
# The CMake build system plugin: configure, build and the targets from
# CMake's file API. Only Qt Core; cmake itself is run by the IDE.
add_library(eudaimonia-cmake MODULE CMakePlugin.cpp)
set_target_properties(eudaimonia-cmake PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${EUDAIMONIA_PLUGIN_BUILD_DIR}")
target_link_libraries(eudaimonia-cmake PRIVATE eudaimonia::plugin-api Qt6::Core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(eudaimonia-cmake PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS eudaimonia-cmake LIBRARY DESTINATION ${EUDAIMONIA_PLUGIN_INSTALL_DIR})
