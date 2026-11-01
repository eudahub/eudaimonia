# SPDX-License-Identifier: GPL-3.0-only
# Writes OUTPUT, a header with the Git commit and its time of Eudaimonia
# (SOURCE_DIR) and of the components built in (COMPONENTS: name=directory
# pairs separated by '|', or name=@text for one known otherwise, e.g. an
# installed version).
# Run at every build; the header changes only when they do, so that only what
# includes it is rebuilt then.

find_package(Git QUIET)

# Sets <prefix>_COMMIT ("1a2b3c4", "+changes" when built with changes not
# committed), <prefix>_TIME and <prefix>_TAG (the tag of the commit, if any).
function(commit_info dir prefix)
    set(commit "unknown")
    set(time "")
    set(tag "")
    if(GIT_FOUND AND EXISTS "${dir}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" log -1
                                "--format=%h%x09%cd" "--date=format:%Y-%m-%d %H:%M"
                        OUTPUT_VARIABLE log RESULT_VARIABLE result
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(result EQUAL 0 AND log MATCHES "^([^\t]+)\t(.*)$")
            set(commit "${CMAKE_MATCH_1}")
            set(time "${CMAKE_MATCH_2}")
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" status --porcelain
                                    --untracked-files=no
                            OUTPUT_VARIABLE changes ERROR_QUIET)
            if(NOT changes STREQUAL "")
                string(APPEND commit "+changes")
            endif()
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" describe --tags --exact-match
                            OUTPUT_VARIABLE tag OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        endif()
    endif()
    set(${prefix}_COMMIT "${commit}" PARENT_SCOPE)
    set(${prefix}_TIME "${time}" PARENT_SCOPE)
    set(${prefix}_TAG "${tag}" PARENT_SCOPE)
endfunction()

commit_info("${SOURCE_DIR}" APP)
string(CONCAT content "#pragma once\n\n"
            "#define EUDAIMONIA_COMMIT \"${APP_COMMIT}\"\n"
            "#define EUDAIMONIA_COMMIT_TIME \"${APP_TIME}\"\n\n"
            "// name, tag or version, commit, commit time\n"
            "#define EUDAIMONIA_COMPONENTS \\\n")
string(REPLACE "|" ";" COMPONENTS "${COMPONENTS}")
foreach(component IN LISTS COMPONENTS)
    if(NOT component MATCHES "^([^=]+)=(.*)$")
        continue()
    endif()
    set(name "${CMAKE_MATCH_1}")
    set(where "${CMAKE_MATCH_2}")
    if(where MATCHES "^@(.*)$")
        set(C_TAG "${CMAKE_MATCH_1}")
        set(C_COMMIT "")
        set(C_TIME "")
    else()
        commit_info("${where}" C)
    endif()
    string(APPEND content "    {\"${name}\", \"${C_TAG}\", \"${C_COMMIT}\", \"${C_TIME}\"}, \\\n")
endforeach()
string(APPEND content "\n")

file(WRITE "${OUTPUT}.tmp" "${content}")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
