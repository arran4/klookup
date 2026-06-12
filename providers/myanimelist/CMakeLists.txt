add_library(provider_myanimelist MODULE
    provider_myanimelist.cpp
)

target_link_libraries(provider_myanimelist
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_myanimelist PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_myanimelist DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
add_dependencies(plugins provider_myanimelist)
