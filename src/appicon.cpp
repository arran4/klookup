#include "appicon.h"
#include <QResource>
#include <QImage>
#include <QPixmap>
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
        const QPixmap source = symbolic.pixmap(size, size);
        if (source.isNull()) continue;
        // Recolour *pixels*, not an icon pixmap with an active Qt paint engine:
        // icon rendering can otherwise alter the colour according to a style.
        QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
        const QColor ink = palette.color(QPalette::WindowText);
        for (int y = 0; y < image.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const int alpha = qAlpha(line[x]);
                line[x] = qRgba(ink.red(), ink.green(), ink.blue(), alpha);
            }
        }
        trayIcon.addPixmap(QPixmap::fromImage(image), QIcon::Normal, QIcon::Off);
    }
    return trayIcon.isNull()
        ? QIcon::fromTheme(QStringLiteral("edit-find"), applicationIcon())
        : trayIcon;
}
