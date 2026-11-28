// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/jsprovider.h"
#include "../src/mainwindow.h"
#include "../src/providerhostservice.h"
#include "../src/providerpackage.h"
#include "../src/searchtab.h"
#include "../src/settingsdialog.h"
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

class UserProvidersTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_configHome;

    static void writeTextFile(const QString &path, const QByteArray &content)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(content), content.size());
        file.close();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_configHome.isValid());
        qputenv("XDG_CONFIG_HOME", m_configHome.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanup()
    {
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("ProviderDirectories"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("Providers"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->sync();
    }

    void directoryAndStowFileSymlinksAndTargetInvalidation()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        // External user provider source tree (must remain unmodified).
        const QString sourceRepo = root.filePath(QStringLiteral("user_source/providers"));
        const QString stowDir = sourceRepo + QStringLiteral("/stow_prov");
        writeTextFile(stowDir + QStringLiteral("/provider.json"),
                      "{\n"
                      "  \"apiVersion\": 2,\n"
                      "  \"id\": \"stow_prov\",\n"
                      "  \"name\": \"Stow Provider\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\",\n"
                      "  \"permissions\": { \"origins\": [\"api.example.invalid\"] }\n"
                      "}\n");
        writeTextFile(stowDir + QStringLiteral("/main.mjs"),
                      "module.exports = { search: function() { return []; } };\n");
        writeTextFile(sourceRepo + QStringLiteral("/main_alt.mjs"),
                      "module.exports = { search: function() { return [{id:'2', title:'Updated'}]; } };\n");

        // Stow-style regular-file symlinks in a configured user provider root.
        const QString configuredRoot = root.filePath(QStringLiteral("configured/providers"));
        const QString stowedPkgDir = configuredRoot + QStringLiteral("/stow_prov");
        QVERIFY(QDir().mkpath(stowedPkgDir));
        QVERIFY(QFile::link(stowDir + QStringLiteral("/provider.json"), stowedPkgDir + QStringLiteral("/provider.json")));
        QVERIFY(QFile::link(stowDir + QStringLiteral("/main.mjs"), stowedPkgDir + QStringLiteral("/main.mjs")));

        // Directory-level symlink into external user provider repository.
        const QString dirTarget = sourceRepo + QStringLiteral("/dir_prov");
        writeTextFile(dirTarget + QStringLiteral("/provider.json"),
                      "{\n"
                      "  \"apiVersion\": 2,\n"
                      "  \"id\": \"dir_prov\",\n"
                      "  \"name\": \"Dir Symlink Provider\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\"\n"
                      "}\n");
        writeTextFile(dirTarget + QStringLiteral("/main.mjs"),
                      "module.exports = { search: function() { return []; } };\n");
        QVERIFY(QFile::link(dirTarget, configuredRoot + QStringLiteral("/dir_prov")));

        const QList<ConfiguredProviderDirectory> dirs{{configuredRoot, QStringLiteral("Dotfiles"), true}};
        const auto packages = ProviderPackageDiscovery::discoverPackages(dirs);
        QCOMPARE(packages.size(), 2);

        const ProviderPackage &stowPkg = packages[0].id() == QLatin1String("stow_prov") ? packages[0] : packages[1];
        QVERIFY(stowPkg.isValid());
        QVERIFY(stowPkg.isSymlinkBacked());
        QCOMPARE(stowPkg.provenanceLabel(), QStringLiteral("Dotfiles"));
        QCOMPARE(stowPkg.displayPath(), stowedPkgDir);

        // User-provided packages must NEVER receive provenance-based auto-trust exemption.
        KConfigGroup providersGroup(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Providers"));
        QVERIFY(!stowPkg.approved(providersGroup));
        stowPkg.saveApproval(providersGroup, true);
        QVERIFY(stowPkg.approved(providersGroup));

        // Retargeting a symlink inside the package invalidates prior approval.
        QVERIFY(QFile::remove(stowedPkgDir + QStringLiteral("/main.mjs")));
        QVERIFY(QFile::link(sourceRepo + QStringLiteral("/main_alt.mjs"), stowedPkgDir + QStringLiteral("/main.mjs")));
        const auto reloaded = ProviderPackageDiscovery::discoverPackages(dirs);
        const ProviderPackage &reloadedStow = reloaded[0].id() == QLatin1String("stow_prov") ? reloaded[0] : reloaded[1];
        QVERIFY(reloadedStow.isValid());
        QVERIFY(reloadedStow.approvalFingerprint() != stowPkg.approvalFingerprint());
        QVERIFY(!reloadedStow.approved(providersGroup));

        // External source files were never modified or deleted.
        QVERIFY(QFileInfo::exists(stowDir + QStringLiteral("/main.mjs")));
        QVERIFY(QFileInfo::exists(dirTarget + QStringLiteral("/main.mjs")));
    }

    void brokenAndDirectoryLoopSymlinksAreRejectedAndCollisionsTracked()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        const QString badDir = root.filePath(QStringLiteral("bad_broken"));
        QVERIFY(QDir().mkpath(badDir));
        writeTextFile(badDir + QStringLiteral("/provider.json"),
                      "{\n"
                      "  \"apiVersion\": 2,\n"
                      "  \"id\": \"bad_broken\",\n"
                      "  \"name\": \"Broken Link Provider\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\"\n"
                      "}\n");
        QVERIFY(QFile::link(root.filePath(QStringLiteral("nonexistent_target.mjs")), badDir + QStringLiteral("/main.mjs")));
        const auto brokenPkg = ProviderPackage::fromDirectory(badDir, ProviderPackage::LocationType::Custom);
        QVERIFY(!brokenPkg.isValid());

        const QString loopDir = root.filePath(QStringLiteral("bad_loop"));
        QVERIFY(QDir().mkpath(loopDir));
        writeTextFile(loopDir + QStringLiteral("/provider.json"),
                      "{\n"
                      "  \"apiVersion\": 2,\n"
                      "  \"id\": \"bad_loop\",\n"
                      "  \"name\": \"Internal Dir Loop\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\"\n"
                      "}\n");
        writeTextFile(loopDir + QStringLiteral("/main.mjs"), "module.exports = { search: function() { return []; } };\n");
        QVERIFY(QFile::link(loopDir, loopDir + QStringLiteral("/nested_loop")));
        const auto loopPkg = ProviderPackage::fromDirectory(loopDir, ProviderPackage::LocationType::Custom,
                                                            ProviderPackage::SymlinkPolicy::AllowSafeFileSymlinks);
        QVERIFY(!loopPkg.isValid());

        // Symlink to /etc/passwd is rejected even with AllowSafeFileSymlinks.
        const QString etcDir = root.filePath(QStringLiteral("bad_etc"));
        QVERIFY(QDir().mkpath(etcDir));
        writeTextFile(etcDir + QStringLiteral("/provider.json"),
                      "{\n"
                      "  \"apiVersion\": 2,\n"
                      "  \"id\": \"bad_etc\",\n"
                      "  \"name\": \"Etc Link\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\"\n"
                      "}\n");
        writeTextFile(etcDir + QStringLiteral("/main.mjs"), "module.exports = { search: function() { return []; } };\n");
        QVERIFY(QFile::link(QStringLiteral("/etc/passwd"), etcDir + QStringLiteral("/secret.txt")));
        QVERIFY(!ProviderPackage::fromDirectory(etcDir, ProviderPackage::LocationType::Custom,
                                                ProviderPackage::SymlinkPolicy::AllowSafeFileSymlinks).isValid());

        // Collision tracking between multiple configured directories.
        const QString highDir = root.filePath(QStringLiteral("high"));
        const QString lowDir = root.filePath(QStringLiteral("low"));
        for (const QString &base : {highDir, lowDir}) {
            writeTextFile(base + QStringLiteral("/dup/provider.json"),
                          "{\n"
                          "  \"apiVersion\": 2,\n"
                          "  \"id\": \"dup\",\n"
                          "  \"name\": \"Duplicate Provider\",\n"
                          "  \"version\": \"1.0.0\",\n"
                          "  \"runtime\": \"javascript\",\n"
                          "  \"entry\": \"main.mjs\"\n"
                          "}\n");
            writeTextFile(base + QStringLiteral("/dup/main.mjs"), "module.exports = { search: function() { return []; } };\n");
        }

        QList<ProviderPackage> shadowed;
        const auto active = ProviderPackageDiscovery::discoverPackages(
            {{highDir, QStringLiteral("High"), true}, {lowDir, QStringLiteral("Low"), true}},
            {},
            &shadowed);
        QCOMPARE(active.size(), 1);
        QCOMPARE(active.first().provenanceLabel(), QStringLiteral("High"));
        QCOMPARE(shadowed.size(), 1);
        QCOMPARE(shadowed.first().id(), QStringLiteral("dup"));
        QCOMPARE(shadowed.first().shadowedBy(), highDir + QStringLiteral("/dup"));
    }

    void explicitRescanReplacementRemovalLifecycleAndMinimalExample()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString userDir = root.filePath(QStringLiteral("my_user_providers"));
        QVERIFY(QDir().mkpath(userDir));

        KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        KConfigGroup dirGroup(cfg, QStringLiteral("ProviderDirectories"));
        ProviderPackageDiscovery::writeConfiguredDirectories(
            dirGroup, {{userDir, QStringLiteral("Personal Providers"), true}});

        const auto loadedDirs = ProviderPackageDiscovery::readConfiguredDirectories(dirGroup);
        QCOMPARE(loadedDirs.size(), 1);
        QCOMPARE(loadedDirs.first().label, QStringLiteral("Personal Providers"));

        // Verify SettingsDialog displays configured directory + persists toggle without auto-watchers.
        SettingsDialog dialog({});
        auto *dirList = dialog.findChild<QListWidget *>(QStringLiteral("providerDirectoriesList"));
        QVERIFY(dirList);
        QCOMPARE(dirList->count(), 1);
        QVERIFY(dirList->item(0)->text().contains(QStringLiteral("Personal Providers")));
        dirList->item(0)->setCheckState(Qt::Unchecked);
        dialog.button(QDialogButtonBox::Apply)->click();
        QVERIFY(!ProviderPackageDiscovery::readConfiguredDirectories(dirGroup).first().enabled);

        // Re-enable and verify MainWindow loads on explicit rescanProviders().
        dirList->item(0)->setCheckState(Qt::Checked);
        dialog.button(QDialogButtonBox::Apply)->click();

        MainWindow window;
        writeTextFile(userDir + QStringLiteral("/added_later/provider.json"),
                      "{\n"
                      "  \"apiVersion\": 2,\n"
                      "  \"id\": \"added_later\",\n"
                      "  \"name\": \"Added Later\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\"\n"
                      "}\n");
        writeTextFile(userDir + QStringLiteral("/added_later/main.mjs"),
                      "module.exports = { search: function() { return [{id: '1', title: 'v1'}]; } };\n");
        bool foundBefore = false;
        for (const auto &status : window.packages()) {
            if (status.package.id() == QLatin1String("added_later"))
                foundBefore = true;
        }
        QVERIFY(!foundBefore);

        window.rescanProviders();
        bool foundAfter = false;
        for (const auto &status : window.packages()) {
            if (status.package.id() == QLatin1String("added_later"))
                foundAfter = true;
        }
        QVERIFY(foundAfter);

        // Approve, trigger a search, then replace & remove with explicit rescan -> in-flight reloads cleanly and removal is tracked.
        window.setPackageTrusted(QStringLiteral("added_later"), true);
        window.rescanProviders();
        QVERIFY(window.provider(QStringLiteral("added_later")) != nullptr);

        QSignalSpy reloadSpy(&window, &MainWindow::packageReloaded);
        window.performSearch(QStringLiteral("user query"));
        writeTextFile(userDir + QStringLiteral("/added_later/main.mjs"),
                      "module.exports = { search: function() { return [{id: '2', title: 'v2'}]; } };\n");
        window.rescanProviders();
        QVERIFY(reloadSpy.count() >= 1);

        QVERIFY(QDir(userDir + QStringLiteral("/added_later")).removeRecursively());
        window.rescanProviders();
        QVERIFY(window.provider(QStringLiteral("added_later")) == nullptr);

        // Minimal HTTP example provider loads and passes Provider API v2 JsProvider evaluation.
        const QString exampleDir = QStringLiteral(PROJECT_SOURCE_DIR "/examples/providers/minimal-http");
        if (QFileInfo::exists(exampleDir + QStringLiteral("/provider.json"))) {
            const auto exPkg = ProviderPackage::fromDirectory(exampleDir, ProviderPackage::LocationType::Custom);
            QVERIFY2(exPkg.isValid(), qPrintable(exPkg.validationError()));
            QCOMPARE(exPkg.manifest().apiVersion, 2);
            ProviderHost::ProviderHostService host(exPkg);
            ProviderPackage approvedPkg = exPkg;
            approvedPkg.setTrusted(true);
            JsProvider js(approvedPkg, nullptr, &host);
            QVERIFY2(js.isLoaded(), qPrintable(js.loadError()));
        }
    }
};

QTEST_MAIN(UserProvidersTest)
#include "test_user_providers.moc"
