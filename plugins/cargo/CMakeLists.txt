# SPDX-License-Identifier: GPL-3.0-only
# The Cargo build system plugin, for Rust projects. Only Qt Core; cargo itself
# is run by the plugin for what it knows of a project, and by the IDE to build.
add_library(eudaimonia-cargo MODULE CargoPlugin.cpp)
set_target_properties(eudaimonia-cargo PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${EUDAIMONIA_PLUGIN_BUILD_DIR}")
target_link_libraries(eudaimonia-cargo PRIVATE eudaimonia::plugin-api Qt6::Core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(eudaimonia-cargo PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS eudaimonia-cargo LIBRARY DESTINATION ${EUDAIMONIA_PLUGIN_INSTALL_DIR})
