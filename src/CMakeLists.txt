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
    # KF6::CoreAddons
    # KF6::ConfigCore
    # KF6::ConfigGui
    # KF6::I18n
    # KF6::DBusAddons
    # KF6::WidgetsAddons
    # KF6::XmlGui
    # KF6::Wallet
)

# install(TARGETS klookup ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
