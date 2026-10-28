#include "globalshortcuts.h"
#include <KGlobalAccel>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QTimer>
#include <utility>

GlobalShortcuts::GlobalShortcuts(const QList<QAction *> &actions, QObject *parent) : QObject(parent)
{
    for (auto *action : actions)
        m_actions.append(action);
    auto *watcher = new QDBusServiceWatcher(QStringLiteral("org.kde.kglobalaccel"), QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &GlobalShortcuts::refresh);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { m_status = Status::Unavailable; });
    QTimer::singleShot(0, this, &GlobalShortcuts::refresh);
}

bool GlobalShortcuts::serviceAvailable() const
{
    // Headless tests must not attempt daemon activation or synthetic key grabs.
    const auto platform = QGuiApplication::platformName();
    if (platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal"))
        return false;
    auto *bus = QDBusConnection::sessionBus().interface();
    if (!bus)
        return false;
    return bus->isServiceRegistered(QStringLiteral("org.kde.kglobalaccel")).value() ||
           bus->activatableServiceNames().value().contains(QStringLiteral("org.kde.kglobalaccel"));
}

bool GlobalShortcuts::registerAction(QAction *action)
{
    // Autoloading preserves user assignments; empty defaults claim no keys.
    return KGlobalAccel::setGlobalShortcut(action, QList<QKeySequence>{});
}

void GlobalShortcuts::refresh()
{
    if (!serviceAvailable()) {
        m_status = Status::Unavailable;
        return;
    }
    bool success = true;
    for (const auto &action : std::as_const(m_actions)) {
        if (action && !registerAction(action))
            success = false;
    }
    m_status = success ? Status::Registered : Status::Failed;
}
