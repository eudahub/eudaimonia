# SPDX-License-Identifier: GPL-3.0-only
# The interfaces of Eudaimonia's own plugins: headers only, for the IDE and
# the plugins. Shared plugins' interfaces are siplasplas::plugin-api's.
add_library(eudaimonia-plugin-api INTERFACE)
add_library(eudaimonia::plugin-api ALIAS eudaimonia-plugin-api)
target_include_directories(eudaimonia-plugin-api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(eudaimonia-plugin-api INTERFACE Qt6::Core)
