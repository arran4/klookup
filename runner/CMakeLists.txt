add_library(klookup_runner MODULE
    klookup_runner.cpp
)

target_link_libraries(klookup_runner
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    KF6::Runner
    KF6::I18n
    KF6::CoreAddons
)

install(TARGETS klookup_runner DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/krunner)
