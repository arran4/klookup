# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_local_virtual_tag_and_expansion test_local_virtual_tag_and_expansion.cpp)
target_link_libraries(test_local_virtual_tag_and_expansion Qt6::Test Qt6::Widgets klookup_core)
target_compile_definitions(test_local_virtual_tag_and_expansion PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_local_virtual_tag_and_expansion COMMAND test_local_virtual_tag_and_expansion)
