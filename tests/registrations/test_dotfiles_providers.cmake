# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_dotfiles_providers test_dotfiles_providers.cpp)
target_link_libraries(test_dotfiles_providers Qt6::Test Qt6::Widgets klookup_core)
target_compile_definitions(test_dotfiles_providers PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_dotfiles_providers COMMAND test_dotfiles_providers)
