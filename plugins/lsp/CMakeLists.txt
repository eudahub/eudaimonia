# SPDX-License-Identifier: GPL-3.0-only
# The language support plugin: a Language Server Protocol client for any
# number of servers (clangd, neocmakelsp, rust-analyzer, pylsp, and those of
# language-servers.json). Only Qt Core; the servers are separate programs.
add_library(eudaimonia-lsp MODULE LspPlugin.cpp LspSession.cpp LspSession.h ServerSpec.cpp ServerSpec.h)
set_target_properties(eudaimonia-lsp PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${EUDAIMONIA_PLUGIN_BUILD_DIR}")
target_link_libraries(eudaimonia-lsp PRIVATE eudaimonia::plugin-api Qt6::Core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(eudaimonia-lsp PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS eudaimonia-lsp LIBRARY DESTINATION ${EUDAIMONIA_PLUGIN_INSTALL_DIR})
