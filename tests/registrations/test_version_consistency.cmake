# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_version_consistency test_version_consistency.cpp)
target_compile_definitions(test_version_consistency PRIVATE
    KLOOKUP_TEST_PROJECT_VERSION="${PROJECT_VERSION}"
    KLOOKUP_TEST_VERSION_STRING="${KLOOKUP_VERSION_STRING}"
    KLOOKUP_TEST_BINARY_PATH="$<TARGET_FILE:klookup>"
    KLOOKUP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_version_consistency Qt6::Test Qt6::Core KF6::CoreAddons)
add_dependencies(test_version_consistency klookup)
add_test(NAME test_version_consistency COMMAND test_version_consistency)
