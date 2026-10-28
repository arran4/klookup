// SPDX-FileCopyrightText: 2026 KLookup contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autostartmanager.h"

#include <KConfigGroup>
#include <KDesktopFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

AutostartManager::AutostartManager(const QString &customAutostartDir)
    : m_customDir(customAutostartDir)
{
}

QString AutostartManager::autostartDirectory() const
{
    if (!m_customDir.isEmpty()) {
        return m_customDir;
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart");
}

QString AutostartManager::autostartFilePath() const
{
    return autostartDirectory() + QStringLiteral("/org.kde.klookup.desktop");
}

bool AutostartManager::isAutostartEnabled() const
{
    const QString filePath = autostartFilePath();
    if (QFileInfo::exists(filePath)) {
        KDesktopFile df(filePath);
        if (df.desktopGroup().readEntry(QStringLiteral("Hidden"), false)) {
            return false;
        }
        if (!df.desktopGroup().readEntry(QStringLiteral("X-GNOME-Autostart-enabled"), true)) {
            return false;
        }
        const QString exec = df.desktopGroup().readEntry(QStringLiteral("Exec"), QString());
        return !exec.isEmpty();
    }

    if (!m_customDir.isEmpty()) {
        return false;
    }

    const QStringList systemFiles = QStandardPaths::locateAll(QStandardPaths::GenericConfigLocation,
                                                             QStringLiteral("autostart/org.kde.klookup.desktop"));
    for (const QString &file : systemFiles) {
        if (file == filePath) {
            continue;
        }
        KDesktopFile df(file);
        if (!df.desktopGroup().readEntry(QStringLiteral("Hidden"), false) &&
            df.desktopGroup().readEntry(QStringLiteral("X-GNOME-Autostart-enabled"), true)) {
            return true;
        }
    }

    return false;
}

bool AutostartManager::setAutostartEnabled(bool enabled)
{
    const QString dir = autostartDirectory();
    const QString filePath = autostartFilePath();

    if (enabled) {
        if (!QDir().mkpath(dir)) {
            return false;
        }
        if (QFileInfo::exists(filePath)) {
            if (!QFileInfo(filePath).isWritable()) {
                return false;
            }
            KDesktopFile df(filePath);
            df.desktopGroup().writeEntry(QStringLiteral("Hidden"), false);
            df.desktopGroup().writeEntry(QStringLiteral("X-GNOME-Autostart-enabled"), true);
            if (df.desktopGroup().readEntry(QStringLiteral("Exec"), QString()).isEmpty()) {
                df.desktopGroup().writeEntry(QStringLiteral("Exec"), QStringLiteral("klookup --background"));
            }
            df.sync();
            return true;
        }

        KDesktopFile df(filePath);
        df.desktopGroup().writeEntry(QStringLiteral("Type"), QStringLiteral("Application"));
        df.desktopGroup().writeEntry(QStringLiteral("Name"), QStringLiteral("KLookup"));
        df.desktopGroup().writeEntry(QStringLiteral("GenericName"), QStringLiteral("Multi-Provider Search Tool"));
        df.desktopGroup().writeEntry(QStringLiteral("Comment"), QStringLiteral("Dictionary-style multi-provider search tool for KDE"));
        df.desktopGroup().writeEntry(QStringLiteral("Exec"), QStringLiteral("klookup --background"));
        df.desktopGroup().writeEntry(QStringLiteral("Icon"), QStringLiteral("org.kde.klookup"));
        df.desktopGroup().writeEntry(QStringLiteral("Terminal"), false);
        df.desktopGroup().writeEntry(QStringLiteral("Categories"), QStringLiteral("Qt;KDE;Utility;Dictionary;"));
        df.desktopGroup().writeEntry(QStringLiteral("StartupNotify"), false);
        df.desktopGroup().writeEntry(QStringLiteral("DBusActivatable"), true);
        df.desktopGroup().writeEntry(QStringLiteral("X-DBus-ServiceName"), QStringLiteral("org.kde.klookup"));
        df.desktopGroup().writeEntry(QStringLiteral("X-GNOME-Autostart-enabled"), true);
        df.desktopGroup().writeEntry(QStringLiteral("X-KDE-autostart-after"), QStringLiteral("panel"));
        df.desktopGroup().writeEntry(QStringLiteral("Hidden"), false);
        df.sync();
        return QFileInfo::exists(filePath);
    }

    // Disabling autostart
    if (QFileInfo::exists(filePath)) {
        if (!QFileInfo(filePath).isWritable()) {
            return false;
        }
        KDesktopFile df(filePath);
        df.desktopGroup().writeEntry(QStringLiteral("Hidden"), true);
        df.desktopGroup().writeEntry(QStringLiteral("X-GNOME-Autostart-enabled"), false);
        df.sync();
        return true;
    }

    if (m_customDir.isEmpty()) {
        const QStringList systemFiles = QStandardPaths::locateAll(QStandardPaths::GenericConfigLocation,
                                                                 QStringLiteral("autostart/org.kde.klookup.desktop"));
        bool hasSystemEntry = false;
        for (const QString &file : systemFiles) {
            if (file != filePath) {
                hasSystemEntry = true;
                break;
            }
        }
        if (hasSystemEntry) {
            if (!QDir().mkpath(dir)) {
                return false;
            }
            KDesktopFile df(filePath);
            df.desktopGroup().writeEntry(QStringLiteral("Type"), QStringLiteral("Application"));
            df.desktopGroup().writeEntry(QStringLiteral("Exec"), QStringLiteral("klookup --background"));
            df.desktopGroup().writeEntry(QStringLiteral("Hidden"), true);
            df.desktopGroup().writeEntry(QStringLiteral("X-GNOME-Autostart-enabled"), false);
            df.sync();
            return QFileInfo::exists(filePath);
        }
    }

    return true;
}
