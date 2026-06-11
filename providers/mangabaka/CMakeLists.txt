add_library(provider_mangabaka MODULE
    provider_mangabaka.cpp
)

target_link_libraries(provider_mangabaka
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_mangabaka PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_mangabaka DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
