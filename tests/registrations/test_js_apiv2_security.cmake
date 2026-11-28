# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_js_apiv2_security test_js_apiv2_security.cpp)
target_link_libraries(test_js_apiv2_security Qt6::Test Qt6::Core Qt6::Network Qt6::Qml klookup_core)
add_test(NAME test_js_apiv2_security COMMAND test_js_apiv2_security)
