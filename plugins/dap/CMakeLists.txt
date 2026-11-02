# SPDX-License-Identifier: GPL-3.0-only
# The debugger plugin: the Debug Adapter Protocol with gdb and with CMake's own
# debugger. Qt Core and Network (a local socket); the debuggers are separate
# programs.
find_package(Qt6 6.2 REQUIRED COMPONENTS Network)
add_library(eudaimonia-dap MODULE DapPlugin.cpp DapSession.cpp DapSession.h ../common/ContentLengthMessages.h)
set_target_properties(eudaimonia-dap PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${EUDAIMONIA_PLUGIN_BUILD_DIR}")
target_link_libraries(eudaimonia-dap PRIVATE eudaimonia::plugin-api Qt6::Core Qt6::Network)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(eudaimonia-dap PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS eudaimonia-dap LIBRARY DESTINATION ${EUDAIMONIA_PLUGIN_INSTALL_DIR})
