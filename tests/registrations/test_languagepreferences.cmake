# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_languagepreferences test_languagepreferences.cpp)
target_link_libraries(test_languagepreferences Qt6::Test klookup_core)
add_test(NAME test_languagepreferences COMMAND test_languagepreferences)
