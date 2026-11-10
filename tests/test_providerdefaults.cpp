// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providerdefaults.h"
#include "providerdiagnostics.h"
#include "mainwindow.h"
#include "searchwindow.h"
#include "settingsdialog.h"
#include "searchprovider.h"
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QApplication>
#include <QListWidget>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstring>

class EmptyReply : public QNetworkReply {
public:
    EmptyReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] { setFinished(true); emit readyRead(); emit finished(); });
    }
    void abort() override { setFinished(true); }
    qint64 bytesAvailable() const override { return 2 - offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override {
        const auto count = qMin<qint64>(max, 2 - offset);
        if (!count) return -1;
        std::memcpy(data, "[]" + offset, count); offset += count; return count;
    }
    qint64 offset = 0;
};
class CountNetwork : public QNetworkAccessManager {
public:
    CountNetwork(int *count, QObject *parent) : QNetworkAccessManager(parent), count(count) {}
    int *count;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++*count;
        return new EmptyReply(request, this);
    }
};
class ProviderDefaultsTest : public QObject {
    Q_OBJECT
private slots:
    void restartAndMigration_data() {
        QTest::addColumn<int>("legacy"); QTest::addColumn<int>("current");
        QTest::addColumn<bool>("migrated"); QTest::addColumn<bool>("expected");
        QTest::newRow("fresh") << -1 << -1 << false << false;
        QTest::newRow("upgrade-absent") << -1 << -1 << true << false;
        QTest::newRow("legacy-true") << 1 << -1 << false << true;
        QTest::newRow("legacy-false") << 0 << -1 << false << false;
        QTest::newRow("explicit-true") << -1 << 1 << true << true;
        QTest::newRow("explicit-false") << -1 << 0 << true << false;
        QTest::newRow("kconfig-wins-false") << 1 << 0 << false << false;
        QTest::newRow("kconfig-wins-true") << 0 << 1 << false << true;
        QTest::newRow("ignore-legacy-after-migration") << 1 << -1 << true << false;
    }
    void restartAndMigration() {
        QFETCH(int, legacy); QFETCH(int, current); QFETCH(bool, migrated); QFETCH(bool, expected);
        QTemporaryDir root; QVERIFY(root.isValid());
        if (legacy >= 0) {
            QSettings settings(root.filePath(QStringLiteral("KDE/klookup.conf")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("cheapshark_Enabled"), bool(legacy)); settings.sync();
        }
        KConfig config(root.filePath(QStringLiteral("klookuprc")), KConfig::SimpleConfig);
        KConfigGroup general(&config, QStringLiteral("General"));
        general.writeEntry(QStringLiteral("MigratedToKConfig"), migrated);
        KConfigGroup providers(&config, QStringLiteral("Providers"));
        if (current >= 0) providers.writeEntry(QStringLiteral("cheapshark_Enabled"), bool(current));
        config.sync();
        for (int restart = 0; restart < 2; ++restart) {
            QProcess child;
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("XDG_CONFIG_HOME"), root.path());
            env.insert(QStringLiteral("XDG_DATA_HOME"), root.filePath(QStringLiteral("data")));
            env.insert(QStringLiteral("XDG_DATA_DIRS"), root.filePath(QStringLiteral("system")));
            env.insert(QStringLiteral("KLOOKUP_DEFAULTS_PROBE"), expected ? QStringLiteral("true") : QStringLiteral("false"));
            child.setProcessEnvironment(env);
            child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("profileProbe")});
            QVERIFY(child.waitForFinished(30000));
            const auto output = child.readAllStandardOutput() + child.readAllStandardError();
            QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
        }
        config.reparseConfiguration();
        const bool explicitValue = current >= 0 || (!migrated && legacy >= 0);
        QCOMPARE(providers.hasKey(QStringLiteral("cheapshark_Enabled")), explicitValue);
    }
    void profileProbe() {
        if (!qEnvironmentVariableIsSet("KLOOKUP_DEFAULTS_PROBE")) QSKIP("Only executed in isolated child profiles");
        const bool expected = qgetenv("KLOOKUP_DEFAULTS_PROBE") == "true";
        auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        KConfigGroup settings(config, QStringLiteral("Providers"));
        int requests = 0;
        MainWindow window;
        window.setNetworkAccessManagerFactory([&requests](QObject *parent) { return new CountNetwork(&requests, parent); });
        const QStringList paths{QStringLiteral(PROJECT_SOURCE_DIR "/providers.d")};
        window.loadPlugins(paths, {});
        const bool explicitValue = settings.hasKey(QStringLiteral("cheapshark_Enabled"));
        // Approval is permission to run, not an enablement choice.
        window.setPackageTrusted(QStringLiteral("cheapshark"), true);
        window.loadPlugins(paths, {});
        QCOMPARE(settings.hasKey(QStringLiteral("cheapshark_Enabled")), explicitValue);
        auto *provider = window.provider(QStringLiteral("cheapshark"));
        QCOMPARE(provider != nullptr, expected);
        if (provider) {
            QVERIFY(provider->isEnabled());
            std::unique_ptr<SearchProvider> copy(provider->clone()); QVERIFY(copy->isEnabled());
            provider->setEnabled(false);
            std::unique_ptr<SearchProvider> disabled(provider->clone()); QVERIFY(!disabled->isEnabled());
            disabled->search(QStringLiteral("must not request"));
            provider->setEnabled(true);
        }
        auto packages = window.packages(); bool visible = false;
        for (const auto &record : packages) if (record.package.id() == QLatin1String("cheapshark")) {
            visible = true; QCOMPARE(record.package.isEnabled(), expected);
        }
        QVERIFY(visible);
        auto sources = ProviderDiagnostics::discover(paths, {}); bool inventoried = false;
        for (const auto &source : sources) if (source.id == QLatin1String("cheapshark")) {
            inventoried = true; QCOMPARE(source.enabled, expected);
        }
        QVERIFY(inventoried);
        SettingsDialog dialog({}, nullptr, nullptr, packages);
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("scriptedPackages")); QVERIFY(list);
        int row = -1;
        for (int i = 0; i < packages.size(); ++i) if (packages[i].package.id() == QLatin1String("cheapshark")) row = i;
        QVERIFY(row >= 0);
        QCOMPARE(list->item(row)->checkState(), expected ? Qt::Checked : Qt::Unchecked);
        QVERIFY(list->item(row)->toolTip().contains(QStringLiteral("US-dollar")));
        QVERIFY(QMetaObject::invokeMethod(&dialog, "saveSettings", Qt::DirectConnection));
        QCOMPARE(settings.hasKey(QStringLiteral("cheapshark_Enabled")), explicitValue);
        QCOMPARE(requests, 0);
        // Ensure all other providers remain outside this dispatch-count test.
        for (const auto &record : packages) if (auto *p = window.provider(record.package.id())) p->setEnabled(p == provider);
        window.performSearch(QStringLiteral("default policy"));
        std::unique_ptr<SearchWindow> detached(window.openSearchInNewWindow(QStringLiteral("independent policy")));
        if (expected) QTRY_COMPARE(requests, 4); // store reference + search per independent clone
        else { QTest::qWait(30); QCOMPARE(requests, 0); }
        // Deliberate enable/disable in Settings is saved and never triggers a request.
        const int before = requests;
        list->item(row)->setCheckState(expected ? Qt::Unchecked : Qt::Checked);
        QVERIFY(QMetaObject::invokeMethod(&dialog, "saveSettings", Qt::DirectConnection));
        QCOMPARE(ProviderDefaults::enabled(settings, QStringLiteral("cheapshark")), !expected);
        QCOMPARE(requests, before);
        // Restore the original profile for the next complete process restart.
        if (explicitValue) settings.writeEntry(QStringLiteral("cheapshark_Enabled"), expected);
        else settings.deleteEntry(QStringLiteral("cheapshark_Enabled"));
        settings.sync();
    }
};
int main(int argc, char **argv) {
    QTemporaryDir config;
    if (!qEnvironmentVariableIsSet("KLOOKUP_DEFAULTS_PROBE")) qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QApplication app(argc, argv);
    ProviderDefaultsTest test; return QTest::qExec(&test, argc, argv);
}
#include "test_providerdefaults.moc"
