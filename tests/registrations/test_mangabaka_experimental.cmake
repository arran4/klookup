# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_mangabaka_experimental
    test_mangabaka_experimental.cpp
    ../providers/mangabaka/provider_mangabaka.cpp
    ../providers/mangabaka/provider_mangabaka_experimental.cpp
)
target_link_libraries(test_mangabaka_experimental Qt6::Test klookup_core KF6::WidgetsAddons Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network)
target_compile_definitions(test_mangabaka_experimental PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures" KLOOKUP_MANGABAKA_AS_BASE)
add_test(NAME test_mangabaka_experimental COMMAND test_mangabaka_experimental)
