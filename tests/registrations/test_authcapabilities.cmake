# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Auth capability v2 must remain binary compatible with the frozen /1.0 header.
add_library(provider_auth_abi_fixture MODULE provider_auth_abi_fixture.cpp)
target_link_libraries(provider_auth_abi_fixture klookup_core Qt6::Core)
add_executable(test_authcapabilities test_authcapabilities.cpp)
target_link_libraries(test_authcapabilities Qt6::Test klookup_core)
add_dependencies(test_authcapabilities provider_auth_abi_fixture)
target_compile_definitions(test_authcapabilities PRIVATE LEGACY_AUTH_LIBRARY="$<TARGET_FILE:provider_auth_abi_fixture>")
add_test(NAME test_authcapabilities COMMAND test_authcapabilities)
