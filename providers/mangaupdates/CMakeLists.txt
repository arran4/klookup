add_library(provider_mangaupdates MODULE
    provider_mangaupdates.cpp
)

target_link_libraries(provider_mangaupdates
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_mangaupdates PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_mangaupdates DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
