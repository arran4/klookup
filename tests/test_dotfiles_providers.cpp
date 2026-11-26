// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/jsprovider.h"
#include "../src/mainwindow.h"
#include "../src/providerhostservice.h"
#include "../src/providerpackage.h"
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
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class DotfilesProvidersTest : public QObject
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

    void directoryAndGnuStowFileSymlinksAndTargetInvalidation()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        // Real dotfiles repository (must remain unmodified).
        const QString dotfilesRepo = root.filePath(QStringLiteral("dotfiles/providers"));
        const QString stowDir = dotfilesRepo + QStringLiteral("/stow_prov");
        writeTextFile(stowDir + QStringLiteral("/provider.json"),
                      "{\n"
                      "  \"id\": \"stow_prov\",\n"
                      "  \"name\": \"Stow Provider\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\",\n"
                      "  \"permissions\": { \"origins\": [\"api.example.invalid\"] }\n"
                      "}\n");
        writeTextFile(stowDir + QStringLiteral("/main.mjs"),
                      "export function search() { return []; }\n");
        writeTextFile(dotfilesRepo + QStringLiteral("/main_alt.mjs"),
                      "export function search() { return [{id:'2', title:'Updated'}]; }\n");

        // Stow-style regular-file symlinks in a configured provider root.
        const QString configuredRoot = root.filePath(QStringLiteral("configured/providers"));
        const QString stowedPkgDir = configuredRoot + QStringLiteral("/stow_prov");
        QVERIFY(QDir().mkpath(stowedPkgDir));
        QVERIFY(QFile::link(stowDir + QStringLiteral("/provider.json"), stowedPkgDir + QStringLiteral("/provider.json")));
        QVERIFY(QFile::link(stowDir + QStringLiteral("/main.mjs"), stowedPkgDir + QStringLiteral("/main.mjs")));

        // Directory-level symlink into dotfiles repository.
        const QString dirTarget = dotfilesRepo + QStringLiteral("/dir_prov");
        writeTextFile(dirTarget + QStringLiteral("/provider.json"),
                      "{\n"
                      "  \"id\": \"dir_prov\",\n"
                      "  \"name\": \"Dir Symlink Provider\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\"\n"
                      "}\n");
        writeTextFile(dirTarget + QStringLiteral("/main.mjs"),
                      "export function search() { return []; }\n");
        QVERIFY(QFile::link(dirTarget, configuredRoot + QStringLiteral("/dir_prov")));

        const QList<ConfiguredProviderDirectory> dirs{{configuredRoot, QStringLiteral("Dotfiles"), true}};
        const auto packages = ProviderPackageDiscovery::discoverPackages(dirs);
        QCOMPARE(packages.size(), 2);

        const ProviderPackage &stowPkg = packages[0].id() == QLatin1String("stow_prov") ? packages[0] : packages[1];
        QVERIFY(stowPkg.isValid());
        QVERIFY(stowPkg.isSymlinkBacked());
        QCOMPARE(stowPkg.provenanceLabel(), QStringLiteral("Dotfiles"));
        QCOMPARE(stowPkg.displayPath(), stowedPkgDir);

        // Dotfiles providers must NEVER receive provenance-based auto-trust exemption.
        KConfigGroup providersGroup(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Providers"));
        QVERIFY(!stowPkg.approved(providersGroup));
        stowPkg.saveApproval(providersGroup, true);
        QVERIFY(stowPkg.approved(providersGroup));

        // Retargeting a symlink inside the package invalidates prior approval.
        QVERIFY(QFile::remove(stowedPkgDir + QStringLiteral("/main.mjs")));
        QVERIFY(QFile::link(dotfilesRepo + QStringLiteral("/main_alt.mjs"), stowedPkgDir + QStringLiteral("/main.mjs")));
        const auto reloaded = ProviderPackageDiscovery::discoverPackages(dirs);
        const ProviderPackage &reloadedStow = reloaded[0].id() == QLatin1String("stow_prov") ? reloaded[0] : reloaded[1];
        QVERIFY(reloadedStow.isValid());
        QVERIFY(reloadedStow.approvalFingerprint() != stowPkg.approvalFingerprint());
        QVERIFY(!reloadedStow.approved(providersGroup));

        // External dotfiles repository files were never modified or deleted.
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
                      "  \"id\": \"bad_loop\",\n"
                      "  \"name\": \"Internal Dir Loop\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\"\n"
                      "}\n");
        writeTextFile(loopDir + QStringLiteral("/main.mjs"), "export function search() { return []; }\n");
        QVERIFY(QFile::link(loopDir, loopDir + QStringLiteral("/nested_loop")));
        const auto loopPkg = ProviderPackage::fromDirectory(loopDir, ProviderPackage::LocationType::Custom,
                                                            ProviderPackage::SymlinkPolicy::AllowSafeFileSymlinks);
        QVERIFY(!loopPkg.isValid());

        // Symlink to /etc/passwd is rejected even with AllowSafeFileSymlinks.
        const QString etcDir = root.filePath(QStringLiteral("bad_etc"));
        QVERIFY(QDir().mkpath(etcDir));
        writeTextFile(etcDir + QStringLiteral("/provider.json"),
                      "{\n"
                      "  \"id\": \"bad_etc\",\n"
                      "  \"name\": \"Etc Link\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\"\n"
                      "}\n");
        writeTextFile(etcDir + QStringLiteral("/main.mjs"), "export function search() { return []; }\n");
        QVERIFY(QFile::link(QStringLiteral("/etc/passwd"), etcDir + QStringLiteral("/secret.txt")));
        QVERIFY(!ProviderPackage::fromDirectory(etcDir, ProviderPackage::LocationType::Custom,
                                                ProviderPackage::SymlinkPolicy::AllowSafeFileSymlinks).isValid());

        // Collision tracking between multiple configured directories.
        const QString highDir = root.filePath(QStringLiteral("high"));
        const QString lowDir = root.filePath(QStringLiteral("low"));
        for (const QString &base : {highDir, lowDir}) {
            writeTextFile(base + QStringLiteral("/dup/provider.json"),
                          "{\n"
                          "  \"id\": \"dup\",\n"
                          "  \"name\": \"Duplicate Provider\",\n"
                          "  \"version\": \"1.0.0\",\n"
                          "  \"runtime\": \"javascript\",\n"
                          "  \"entry\": \"main.mjs\"\n"
                          "}\n");
            writeTextFile(base + QStringLiteral("/dup/main.mjs"), "export function search() { return []; }\n");
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

    void explicitRescanSettingsPersistenceAndMinimalExample()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString dotfilesDir = root.filePath(QStringLiteral("my_dotfiles"));
        QVERIFY(QDir().mkpath(dotfilesDir));

        KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        KConfigGroup dirGroup(cfg, QStringLiteral("ProviderDirectories"));
        ProviderPackageDiscovery::writeConfiguredDirectories(
            dirGroup, {{dotfilesDir, QStringLiteral("Personal Dotfiles"), true}});

        const auto loadedDirs = ProviderPackageDiscovery::readConfiguredDirectories(dirGroup);
        QCOMPARE(loadedDirs.size(), 1);
        QCOMPARE(loadedDirs.first().label, QStringLiteral("Personal Dotfiles"));

        // Verify SettingsDialog displays configured directory + persists toggle without auto-watchers.
        SettingsDialog dialog({});
        auto *dirList = dialog.findChild<QListWidget *>(QStringLiteral("providerDirectoriesList"));
        QVERIFY(dirList);
        QCOMPARE(dirList->count(), 1);
        QVERIFY(dirList->item(0)->text().contains(QStringLiteral("Personal Dotfiles")));
        dirList->item(0)->setCheckState(Qt::Unchecked);
        dialog.button(QDialogButtonBox::Apply)->click();
        QVERIFY(!ProviderPackageDiscovery::readConfiguredDirectories(dirGroup).first().enabled);

        // Re-enable and verify MainWindow loads on explicit rescanProviders().
        dirList->item(0)->setCheckState(Qt::Checked);
        dialog.button(QDialogButtonBox::Apply)->click();

        MainWindow window;
        writeTextFile(dotfilesDir + QStringLiteral("/added_later/provider.json"),
                      "{\n"
                      "  \"id\": \"added_later\",\n"
                      "  \"name\": \"Added Later\",\n"
                      "  \"version\": \"1.0.0\",\n"
                      "  \"runtime\": \"javascript\",\n"
                      "  \"entry\": \"main.mjs\"\n"
                      "}\n");
        writeTextFile(dotfilesDir + QStringLiteral("/added_later/main.mjs"),
                      "export function search() { return []; }\n");
        // No filesystem watcher auto-loads it before explicit rescan.
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

        // Minimal HTTP example provider loads and passes JsProvider selfTest.
        const QString exampleDir = QStringLiteral(PROJECT_SOURCE_DIR "/examples/providers/minimal-http");
        if (QFileInfo::exists(exampleDir + QStringLiteral("/provider.json"))) {
            const auto exPkg = ProviderPackage::fromDirectory(exampleDir, ProviderPackage::LocationType::Custom);
            QVERIFY2(exPkg.isValid(), qPrintable(exPkg.validationError()));
            ProviderHost::ProviderHostService host(exPkg);
            ProviderPackage approvedPkg = exPkg;
            approvedPkg.setTrusted(true);
            JsProvider js(approvedPkg, nullptr, &host);
            QVERIFY2(js.isLoaded(), qPrintable(js.loadError()));
        }
    }
};

QTEST_MAIN(DotfilesProvidersTest)
#include "test_dotfiles_providers.moc"
