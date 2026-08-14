add_library(provider_omdb MODULE
    provider_omdb.cpp
)

target_link_libraries(provider_omdb
    klookup_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_omdb PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_omdb DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
