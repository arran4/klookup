add_library(klookup_core STATIC
    searchprovider.cpp
    searchdashboard.cpp
    mainwindow.cpp
    searchtab.cpp
    dbusadaptor.cpp
    settingsdialog.cpp
)

set_target_properties(klookup_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(klookup_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::I18n
    KF6::DBusAddons
    KF6::WidgetsAddons
    KF6::XmlGui
    KF6::Wallet
    KF6::Notifications
)

add_executable(klookup
    main.cpp
)

qt_add_resources(klookup "klookup_resources" PREFIX "/kxmlgui5/klookup" FILES klookupui.rc)

target_link_libraries(klookup
    klookup_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::I18n
    KF6::DBusAddons
    KF6::WidgetsAddons
    KF6::XmlGui
    KF6::Wallet
    KF6::Notifications
)

install(TARGETS klookup ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS org.kde.klookup.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES klookupui.rc DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/klookup)
install(FILES org.kde.klookup.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})

