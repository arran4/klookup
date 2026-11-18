# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Git is the source of truth. The explicit packager/CI override is for an
# externally known release version; no tracked VERSION or moving baseline exists.
set(_KLOOKUP_VERSION_PATTERN "^v?([0-9]+\\.[0-9]+\\.[0-9]+([-+][0-9A-Za-z.+-]+)?)$")
set(_KLOOKUP_DETECTED_VERSION "")
set(_KLOOKUP_REVISION "")

if(DEFINED KLOOKUP_VERSION AND NOT KLOOKUP_VERSION STREQUAL "")
    if(NOT KLOOKUP_VERSION MATCHES "${_KLOOKUP_VERSION_PATTERN}")
        message(FATAL_ERROR "Invalid KLOOKUP_VERSION: ${KLOOKUP_VERSION}")
    endif()
    set(_KLOOKUP_DETECTED_VERSION "${CMAKE_MATCH_1}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    # A .git file also covers linked worktrees and submodules. Never discover
    # an enclosing unrelated repository when configuring an extracted archive.
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*" --abbrev=12 HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _KLOOKUP_DESCRIPTION
            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _KLOOKUP_GIT_RESULT
        )
        if(_KLOOKUP_GIT_RESULT EQUAL 0 AND _KLOOKUP_DESCRIPTION MATCHES "${_KLOOKUP_VERSION_PATTERN}")
            set(_KLOOKUP_DETECTED_VERSION "${CMAKE_MATCH_1}")
        else()
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE _KLOOKUP_REVISION
                ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        endif()
    endif()
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/KLookupArchiveVersion.cmake")
    # These fields are expanded by git archive, including GitHub source archives.
    # The release workflow stamps the exact Git ref in its generated source asset.
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KLookupArchiveVersion.cmake")
    if(DEFINED _KLOOKUP_GIT_ARCHIVE_DESCRIPTION AND _KLOOKUP_GIT_ARCHIVE_DESCRIPTION MATCHES "${_KLOOKUP_VERSION_PATTERN}")
        set(_KLOOKUP_DETECTED_VERSION "${CMAKE_MATCH_1}")
    elseif(DEFINED _KLOOKUP_GIT_ARCHIVE_REFS AND _KLOOKUP_GIT_ARCHIVE_REFS MATCHES "tag:[ ]*v([0-9]+\\.[0-9]+\\.[0-9]+[-+0-9A-Za-z.]*)")
        set(_KLOOKUP_DETECTED_VERSION "${CMAKE_MATCH_1}")
    elseif(DEFINED _KLOOKUP_GIT_ARCHIVE_REVISION AND _KLOOKUP_GIT_ARCHIVE_REVISION MATCHES "^[0-9a-f]+$")
        string(SUBSTRING "${_KLOOKUP_GIT_ARCHIVE_REVISION}" 0 12 _KLOOKUP_REVISION)
    endif()
endif()

if(_KLOOKUP_DETECTED_VERSION STREQUAL "")
    if(_KLOOKUP_REVISION MATCHES "^[0-9a-f]+$")
        set(_KLOOKUP_DETECTED_VERSION "0.0.0-dev+g${_KLOOKUP_REVISION}")
    else()
        # An unstamped source copy without Git cannot claim any release version.
        set(_KLOOKUP_DETECTED_VERSION "0.0.0-unknown")
        message(WARNING "KLookup Git version unavailable; use a tagged source archive or -DKLOOKUP_VERSION for a known package version")
    endif()
endif()

set(KLOOKUP_VERSION_STRING "${_KLOOKUP_DETECTED_VERSION}" CACHE STRING "Git-derived KLookup version string" FORCE)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" KLOOKUP_NUMERIC_VERSION "${KLOOKUP_VERSION_STRING}")
