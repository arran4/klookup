add_library(provider_mangadex MODULE
    provider_mangadex.cpp
)

target_link_libraries(provider_mangadex
    klookup_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_mangadex PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_mangadex DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
