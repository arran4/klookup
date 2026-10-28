add_library(provider_spotify MODULE
    provider_spotify.cpp
)

target_link_libraries(provider_spotify
    klookup_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    KF6::ConfigCore
)

target_include_directories(provider_spotify PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_spotify DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
