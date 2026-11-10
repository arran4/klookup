# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_settingspanes test_settingspanes.cpp)
target_link_libraries(test_settingspanes Qt6::Test klookup_core)
add_test(NAME test_settingspanes COMMAND test_settingspanes)
