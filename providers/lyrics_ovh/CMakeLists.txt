add_library(provider_lyrics_ovh MODULE
    provider_lyrics_ovh.cpp
)

target_link_libraries(provider_lyrics_ovh
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_lyrics_ovh PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_lyrics_ovh DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
