#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

class ProviderSettingsLifecycleTest : public QObject
{
    Q_OBJECT

private:
    QString runHelper(const QString &mode, const QString &configHome)
    {
        QProcess helper;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), configHome);
        environment.insert(QStringLiteral("XDG_DATA_HOME"), configHome + QStringLiteral("/data"));
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        helper.setProcessEnvironment(environment);

        QString helperPath = QStringLiteral(HELPER_PATH);
        helper.start(helperPath, { mode });
        if (!helper.waitForStarted() || !helper.waitForFinished()
            || helper.exitStatus() != QProcess::NormalExit || helper.exitCode() != 0) {
            qWarning().noquote() << "Lifecycle helper failed:" << helper.errorString()
                                 << "code" << helper.exitCode()
                                 << helper.readAllStandardError();
            return {};
        }
        return QString::fromUtf8(helper.readAllStandardOutput());
    }

private slots:
    void providerSettingsPersistence() {
        QTemporaryDir config;
        QVERIFY(config.isValid());

        const QString expected = QStringLiteral("STATE OK\n");

        // 1. Disabling a provider persists after a complete application restart.
        // First we disable it, then we exit. The config changes are made with the mock provider loaded.
        QCOMPARE(runHelper(QStringLiteral("disable-alpha"), config.path()), expected);

        // 2. Disabled providers are skipped during searches.
        // 5. Provider configuration remains associated with stable IDs rather than display names or discovery order.
        // We restart the app (which runs the plugin discovery) and verify the provider 'alpha' is omitted from search results,
        // while the provider 'beta' is still used.
        QCOMPARE(runHelper(QStringLiteral("verify-alpha-disabled"), config.path()), expected);

        // 3. Re-enabling a provider persists after a subsequent restart.
        QCOMPARE(runHelper(QStringLiteral("enable-alpha"), config.path()), expected);

        // 4. A newly discovered provider defaults to enabled. (Demonstrated by Beta being enabled in verify steps).
        // 6. Provider cloning respects the restored enabled state.
        // This is verified because the main window search performs a clone of the enabled templates.
        QCOMPARE(runHelper(QStringLiteral("verify-alpha-enabled"), config.path()), expected);
    }
};

QTEST_GUILESS_MAIN(ProviderSettingsLifecycleTest)
#include "test_provider_settings_lifecycle.moc"
