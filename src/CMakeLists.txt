add_library(klookup_core STATIC
    searchprovider.cpp
    searchdashboard.cpp
    mainwindow.cpp
    searchtab.cpp
    dbusadaptor.cpp
    settingsdialog.cpp
    appicon.cpp
)

set_source_files_properties(../icons/scalable/apps/org.kde.klookup.svg PROPERTIES QT_RESOURCE_ALIAS org.kde.klookup.svg)
set_source_files_properties(../icons/256x256/apps/org.kde.klookup.png PROPERTIES QT_RESOURCE_ALIAS org.kde.klookup.png)
qt_add_resources(klookup_core "klookup_icons" PREFIX "/icons" FILES
    ../icons/scalable/apps/org.kde.klookup.svg
    ../icons/256x256/apps/org.kde.klookup.png
)

set_target_properties(klookup_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(klookup_core PUBLIC
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

configure_file(org.kde.klookup.service.in org.kde.klookup.service @ONLY)

install(TARGETS klookup ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS org.kde.klookup.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES klookupui.rc DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/klookup)
install(FILES org.kde.klookup.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.klookup.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})

