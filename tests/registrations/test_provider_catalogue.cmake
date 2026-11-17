# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_provider_catalogue test_provider_catalogue.cpp)
target_link_libraries(test_provider_catalogue Qt6::Test klookup_core)
add_test(NAME test_provider_catalogue COMMAND test_provider_catalogue)
