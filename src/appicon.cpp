#include "appicon.h"
#include <QResource>

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
