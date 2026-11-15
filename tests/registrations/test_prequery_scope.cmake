# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_prequery_scope test_prequery_scope.cpp)
target_link_libraries(test_prequery_scope Qt6::Test klookup_core)
add_test(NAME test_prequery_scope COMMAND test_prequery_scope)
