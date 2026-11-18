# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(provider_localdocs MODULE
    provider_localdocs.cpp
    provider_localdocs.h
)

target_link_libraries(provider_localdocs
    Qt6::Core
    Qt6::Widgets
    KF6::ConfigCore
    KF6::I18n
    klookup_core
)

target_include_directories(provider_localdocs PRIVATE ${CMAKE_SOURCE_DIR}/src)
install(TARGETS provider_localdocs DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
