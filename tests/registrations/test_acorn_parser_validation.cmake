# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_acorn_parser_validation test_acorn_parser_validation.cpp)
target_link_libraries(test_acorn_parser_validation Qt6::Test Qt6::Core Qt6::Qml klookup_core)
add_test(NAME test_acorn_parser_validation COMMAND test_acorn_parser_validation)
