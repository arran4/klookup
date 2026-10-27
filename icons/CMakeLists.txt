set(KLOOKUP_ICON_NAME org.kde.klookup)
foreach(size 16 22 24 32 48 64 128 256)
    install(FILES ${size}x${size}/apps/${KLOOKUP_ICON_NAME}.png
            DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/${size}x${size}/apps)
endforeach()
install(FILES scalable/apps/${KLOOKUP_ICON_NAME}.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
install(FILES symbolic/apps/${KLOOKUP_ICON_NAME}-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)

install(FILES README.md DESTINATION ${KDE_INSTALL_DATADIR}/klookup/icons)
