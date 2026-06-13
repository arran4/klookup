add_executable(klookup
    main.cpp
    mainwindow.cpp
    searchdashboard.cpp
    searchtab.cpp
    searchprovider.cpp
    dbusadaptor.cpp
    settingsdialog.cpp
)

target_link_libraries(klookup
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
    # KF5::CoreAddons
    # KF5::ConfigCore
    # KF5::ConfigGui
    # KF5::I18n
    # KF5::DBusAddons
    # KF5::WidgetsAddons
    # KF5::XmlGui
    # KF5::Wallet
)

# install(TARGETS klookup ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
