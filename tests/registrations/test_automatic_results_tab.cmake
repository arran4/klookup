# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_automatic_results_tab test_automatic_results_tab.cpp)
target_link_libraries(test_automatic_results_tab Qt6::Test Qt6::Widgets klookup_core)
add_test(NAME test_automatic_results_tab COMMAND test_automatic_results_tab)
