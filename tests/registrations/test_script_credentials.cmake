# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_script_credentials test_script_credentials.cpp)
target_link_libraries(test_script_credentials Qt6::Test Qt6::Core Qt6::Network Qt6::Qml klookup_core)
add_test(NAME test_script_credentials COMMAND test_script_credentials)
