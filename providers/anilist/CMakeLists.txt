add_library(provider_anilist MODULE
    provider_anilist.cpp
)

target_link_libraries(provider_anilist
    klookup_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_anilist PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_anilist DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
