add_library(provider_tvmaze MODULE
    provider_tvmaze.cpp
)

target_link_libraries(provider_tvmaze
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_tvmaze PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_tvmaze DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
