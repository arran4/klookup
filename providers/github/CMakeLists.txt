add_library(provider_github MODULE
    provider_github.cpp
)

target_link_libraries(provider_github
    klookup_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_github PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_github DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
