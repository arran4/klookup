# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_displayfilters test_displayfilters.cpp)
target_link_libraries(test_displayfilters Qt6::Test klookup_core)
add_test(NAME test_displayfilters COMMAND test_displayfilters)
