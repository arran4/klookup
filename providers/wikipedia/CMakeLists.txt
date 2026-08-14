add_library(provider_wikipedia MODULE
    provider_wikipedia.cpp
)

target_link_libraries(provider_wikipedia
    klookup_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_wikipedia PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_wikipedia DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
