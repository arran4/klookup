// SPDX-FileCopyrightText: 2026 KLookup contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/autostartmanager.h"
#include "../src/settingsdialog.h"
#include <KConfigGroup>
#include <KDesktopFile>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif

class TestAutostart : public QObject
{
    Q_OBJECT

private slots:
    void defaultStateDisabled()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        AutostartManager manager(tempDir.path());

        QCOMPARE(manager.autostartDirectory(), tempDir.path());
        QCOMPARE(manager.autostartFilePath(), tempDir.filePath(QStringLiteral("org.kde.klookup.desktop")));
        QVERIFY(!manager.isAutostartEnabled());
    }

    void enableAndDisableCycle()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        AutostartManager manager(tempDir.path());

        // Enable
        QVERIFY(manager.setAutostartEnabled(true));
        QVERIFY(manager.isAutostartEnabled());
        QVERIFY(QFile::exists(manager.autostartFilePath()));

        {
            KDesktopFile df(manager.autostartFilePath());
            QCOMPARE(df.desktopGroup().readEntry("Type"), QStringLiteral("Application"));
            QCOMPARE(df.desktopGroup().readEntry("Exec"), QStringLiteral("klookup --background"));
            QCOMPARE(df.desktopGroup().readEntry("Icon"), QStringLiteral("org.kde.klookup"));
            QVERIFY(!df.desktopGroup().readEntry("Hidden", true));
            QVERIFY(df.desktopGroup().readEntry("X-GNOME-Autostart-enabled", false));
        }

        // Disable
        QVERIFY(manager.setAutostartEnabled(false));
        QVERIFY(!manager.isAutostartEnabled());

        {
            KDesktopFile df(manager.autostartFilePath());
            QVERIFY(df.desktopGroup().readEntry("Hidden", false));
            QVERIFY(!df.desktopGroup().readEntry("X-GNOME-Autostart-enabled", true));
            // Exec and other metadata remain preserved
            QCOMPARE(df.desktopGroup().readEntry("Exec"), QStringLiteral("klookup --background"));
        }

        // Re-enable
        QVERIFY(manager.setAutostartEnabled(true));
        QVERIFY(manager.isAutostartEnabled());
        {
            KDesktopFile df(manager.autostartFilePath());
            QVERIFY(!df.desktopGroup().readEntry("Hidden", true));
            QVERIFY(df.desktopGroup().readEntry("X-GNOME-Autostart-enabled", false));
        }
    }

    void preserveExternalCustomKeys()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString filePath = tempDir.filePath(QStringLiteral("org.kde.klookup.desktop"));

        // Create an existing desktop file with custom user keys
        {
            KDesktopFile df(filePath);
            df.desktopGroup().writeEntry("Type", "Application");
            df.desktopGroup().writeEntry("Exec", "klookup --background");
            df.desktopGroup().writeEntry("X-Custom-Setting", "PlasmaFavorite");
            df.desktopGroup().writeEntry("Comment[fr]", "Outil de recherche");
            df.desktopGroup().writeEntry("Hidden", true);
            df.sync();
        }

        AutostartManager manager(tempDir.path());
        QVERIFY(!manager.isAutostartEnabled());

        // Enabling must preserve custom keys
        QVERIFY(manager.setAutostartEnabled(true));
        QVERIFY(manager.isAutostartEnabled());

        {
            KDesktopFile df(filePath);
            QCOMPARE(df.desktopGroup().readEntry("X-Custom-Setting"), QStringLiteral("PlasmaFavorite"));
            QCOMPARE(df.desktopGroup().readEntry("Comment[fr]"), QStringLiteral("Outil de recherche"));
            QVERIFY(!df.desktopGroup().readEntry("Hidden", true));
        }
    }

    void handleExternallyDisabledState()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        AutostartManager manager(tempDir.path());

        QVERIFY(manager.setAutostartEnabled(true));
        QVERIFY(manager.isAutostartEnabled());

        // Simulate external tool (like KDE System Settings or GNOME Tweaks) disabling it
        {
            KDesktopFile df(manager.autostartFilePath());
            df.desktopGroup().writeEntry("X-GNOME-Autostart-enabled", false);
            df.sync();
        }
        QVERIFY(!manager.isAutostartEnabled());

        {
            KDesktopFile df(manager.autostartFilePath());
            df.desktopGroup().writeEntry("X-GNOME-Autostart-enabled", true);
            df.desktopGroup().writeEntry("Hidden", true);
            df.sync();
        }
        QVERIFY(!manager.isAutostartEnabled());
    }

    void permissionFailureHandling()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Test failure when directory creation fails (e.g. invalid path component)
        const QString blockingFile = tempDir.filePath(QStringLiteral("blocking_file"));
        QFile file(blockingFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        const QString invalidDir = tempDir.filePath(QStringLiteral("blocking_file/cannot_be_a_dir"));
        AutostartManager failManager(invalidDir);
        QVERIFY(!failManager.setAutostartEnabled(true));
        QVERIFY(!failManager.isAutostartEnabled());

#ifndef Q_OS_WIN
        if (::getuid() == 0) {
            // Root bypasses standard filesystem read-only permissions in containers
            return;
        }

        const QString subDir = tempDir.filePath(QStringLiteral("readonly_autostart"));
        QVERIFY(QDir().mkdir(subDir));

        // Make directory read-only
        QVERIFY(QFile::setPermissions(subDir, QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ExeOwner | QFileDevice::ExeUser));

        AutostartManager manager(subDir);
        const bool result = manager.setAutostartEnabled(true);
        QVERIFY(!result);
        QVERIFY(!manager.isAutostartEnabled());

        // Restore permissions for cleanup
        QFile::setPermissions(subDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
    }

    void settingsDialogIntegration()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        SettingsDialog dlg({});
        dlg.setAutostartManager(std::make_unique<AutostartManager>(tempDir.path()));

        auto *check = dlg.findChild<QCheckBox *>(QStringLiteral("autostartOnLogin"));
        QVERIFY(check);
        QVERIFY(!check->isChecked());

        // Check the box and trigger save via dialog
        check->setChecked(true);
        auto *manager = dlg.autostartManager();
        QVERIFY(manager);
        QVERIFY(manager->setAutostartEnabled(check->isChecked()));
        QVERIFY(manager->isAutostartEnabled());

        // Checkbox reflects enabled state
        check->setChecked(false);
        QVERIFY(manager->setAutostartEnabled(check->isChecked()));
        QVERIFY(!manager->isAutostartEnabled());
    }
};

QTEST_MAIN(TestAutostart)
#include "test_autostart.moc"
