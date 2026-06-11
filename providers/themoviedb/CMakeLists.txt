add_library(provider_themoviedb MODULE
    provider_themoviedb.cpp
)

target_link_libraries(provider_themoviedb
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_themoviedb PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_themoviedb DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
