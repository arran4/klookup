# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_selected_text_lookup test_selected_text_lookup.cpp)
target_link_libraries(test_selected_text_lookup Qt6::Test klookup_core)
add_test(NAME test_selected_text_lookup COMMAND test_selected_text_lookup)
