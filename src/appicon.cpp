#include "appicon.h"
#include <QResource>
#include <QPainter>
#include <QPalette>

QIcon applicationIcon()
{
    // Explicitly reference the resource initializer: the core is a static library.
    static const bool registered = [] {
        Q_INIT_RESOURCE(klookup_icons);
        return true;
    }();
    Q_UNUSED(registered);
    QIcon fallback(QStringLiteral(":/icons/org.kde.klookup.png"));
    fallback.addFile(QStringLiteral(":/icons/org.kde.klookup.svg"));
    return QIcon::fromTheme(QStringLiteral("org.kde.klookup"), fallback);
}

QIcon trayIconForPalette(const QPalette &palette)
{
    // The full-colour launcher icon is too dark in some Plasma panel themes.
    // Render the bundled CC0 symbolic silhouette using the active palette,
    // rather than relying on SVG currentColor (not all tray hosts recolour it).
    applicationIcon(); // register the statically linked Qt resources
    const QIcon symbolic(QStringLiteral(":/icons/org.kde.klookup-symbolic.svg"));
    QIcon trayIcon;
    for (const int size : {16, 22, 24, 32, 48}) {
        QPixmap pixmap = symbolic.pixmap(size, size);
        if (pixmap.isNull()) continue;
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), palette.color(QPalette::WindowText));
        painter.end();
        trayIcon.addPixmap(pixmap);
    }
    return trayIcon.isNull()
        ? QIcon::fromTheme(QStringLiteral("edit-find"), applicationIcon())
        : trayIcon;
}
