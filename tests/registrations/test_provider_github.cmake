# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_provider_github test_provider_github.cpp ../providers/github/provider_github.cpp)
target_link_libraries(test_provider_github Qt6::Test klookup_core)
add_test(NAME test_provider_github COMMAND test_provider_github)
