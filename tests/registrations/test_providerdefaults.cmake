# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_providerdefaults test_providerdefaults.cpp)
target_link_libraries(test_providerdefaults Qt6::Test klookup_core)
target_compile_definitions(test_providerdefaults PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_providerdefaults COMMAND test_providerdefaults)
