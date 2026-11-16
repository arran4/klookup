# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Authoritative version determination:
# 1. Explicit user/packager override: -DKLOOKUP_VERSION=x.y.z
# 2. Exact git tag if building in a git tree (must win over static VERSION on release tags)
# 3. VERSION file in source root (for source archives / Gentoo tarballs / untagged builds)
# 4. Fallback default version
set(KLOOKUP_DEFAULT_VERSION "0.1.0")

if(DEFINED KLOOKUP_VERSION AND NOT KLOOKUP_VERSION STREQUAL "")
    set(_KLOOKUP_DETECTED_VERSION "${KLOOKUP_VERSION}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _GIT_EXACT_TAG
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_GIT_EXACT_TAG MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+.*)$")
            set(_KLOOKUP_DETECTED_VERSION "${CMAKE_MATCH_1}")
        endif()
    endif()
endif()

if(NOT DEFINED _KLOOKUP_DETECTED_VERSION AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _KLOOKUP_FILE_VERSION)
    list(GET _KLOOKUP_FILE_VERSION 0 _KLOOKUP_FIRST_LINE)
    string(STRIP "${_KLOOKUP_FIRST_LINE}" _KLOOKUP_FILE_VERSION)
    if(NOT _KLOOKUP_FILE_VERSION STREQUAL "")
        set(_KLOOKUP_DETECTED_VERSION "${_KLOOKUP_FILE_VERSION}")
    endif()
endif()

if(NOT DEFINED _KLOOKUP_DETECTED_VERSION)
    set(_KLOOKUP_DETECTED_VERSION "${KLOOKUP_DEFAULT_VERSION}")
endif()

string(REGEX REPLACE "^v" "" KLOOKUP_VERSION_STRING "${_KLOOKUP_DETECTED_VERSION}")

if(KLOOKUP_VERSION_STRING MATCHES "^([0-9]+(\\.[0-9]+(\\.[0-9]+)?)?)")
    set(KLOOKUP_NUMERIC_VERSION "${CMAKE_MATCH_1}")
else()
    set(KLOOKUP_NUMERIC_VERSION "${KLOOKUP_DEFAULT_VERSION}")
endif()

set(KLOOKUP_VERSION_STRING "${KLOOKUP_VERSION_STRING}" CACHE STRING "Authoritative KLookup version string" FORCE)
