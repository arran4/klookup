# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_searcheligibility test_searcheligibility.cpp)
target_link_libraries(test_searcheligibility Qt6::Test klookup_core)
target_compile_definitions(test_searcheligibility PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_searcheligibility COMMAND test_searcheligibility)
