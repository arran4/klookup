add_library(provider_mangabaka MODULE
    provider_mangabaka.cpp
)

target_link_libraries(provider_mangabaka
    klookup_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_mangabaka PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_mangabaka DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)

add_library(provider_mangabaka_experimental MODULE
    provider_mangabaka.cpp
    provider_mangabaka_experimental.cpp
)

target_link_libraries(provider_mangabaka_experimental
    klookup_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_mangabaka_experimental PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(provider_mangabaka_experimental PRIVATE KLOOKUP_MANGABAKA_AS_BASE)

install(TARGETS provider_mangabaka_experimental DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
