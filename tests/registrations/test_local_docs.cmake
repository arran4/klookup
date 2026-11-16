# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_local_docs test_local_docs.cpp ../providers/localdocs/provider_localdocs.cpp)
target_link_libraries(test_local_docs Qt6::Test Qt6::Widgets KF6::ConfigCore KF6::I18n klookup_core)
add_test(NAME test_local_docs COMMAND test_local_docs)
