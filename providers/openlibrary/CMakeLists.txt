add_library(provider_openlibrary MODULE
    provider_openlibrary.cpp
)

target_link_libraries(provider_openlibrary
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

target_include_directories(provider_openlibrary PRIVATE ${CMAKE_SOURCE_DIR}/src)

install(TARGETS provider_openlibrary DESTINATION ${KDE_INSTALL_PLUGINDIR}/klookup/providers)
add_dependencies(plugins provider_openlibrary)
