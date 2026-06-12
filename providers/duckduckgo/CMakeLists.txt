add_library(provider_duckduckgo MODULE
    provider_duckduckgo.cpp
)

target_link_libraries(provider_duckduckgo
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_duckduckgo PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_duckduckgo DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
