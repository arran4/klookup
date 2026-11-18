# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_result_ui_and_errors test_result_ui_and_errors.cpp)
target_link_libraries(test_result_ui_and_errors Qt6::Test klookup_core)
add_test(NAME test_result_ui_and_errors COMMAND test_result_ui_and_errors)
