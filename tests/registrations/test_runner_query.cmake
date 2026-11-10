# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# KRunner query normalization is independent of the Plasma desktop session.
add_executable(test_runner_query test_runner_query.cpp)
target_link_libraries(test_runner_query Qt6::Test Qt6::Core)
add_test(NAME test_runner_query COMMAND test_runner_query)
