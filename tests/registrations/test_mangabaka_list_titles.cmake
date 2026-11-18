# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_mangabaka_list_titles test_mangabaka_list_titles.cpp)
target_link_libraries(test_mangabaka_list_titles Qt6::Test klookup_core)
add_test(NAME test_mangabaka_list_titles COMMAND test_mangabaka_list_titles)
