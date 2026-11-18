# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_keyboard_navigation test_keyboard_navigation.cpp)
target_link_libraries(test_keyboard_navigation Qt6::Test KF6::GlobalAccel klookup_core)
add_test(NAME test_keyboard_navigation COMMAND test_keyboard_navigation)
