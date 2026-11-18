# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_provider_duckduckgo test_provider_duckduckgo.cpp ../providers/duckduckgo/provider_duckduckgo.cpp)
target_link_libraries(test_provider_duckduckgo Qt6::Test klookup_core)
add_test(NAME test_provider_duckduckgo COMMAND test_provider_duckduckgo)
