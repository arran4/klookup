#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class ShortcutActivationTest : public QObject
{
    Q_OBJECT
private slots:
    void flagsReuseRunningInstanceAndColdStart()
    {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), config.path());
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        QProcess primary;
        primary.setProcessEnvironment(environment);
        primary.setProcessChannelMode(QProcess::ForwardedChannels);
        primary.start(QStringLiteral(KLOOKUP_BINARY), {QStringLiteral("--background")});
        QVERIFY(primary.waitForStarted());
        auto *bus = QDBusConnection::sessionBus().interface();
        QVERIFY(bus);
        const QString service = QStringLiteral("org.kde.klookup");
        QTRY_VERIFY_WITH_TIMEOUT(bus->isServiceRegistered(service).value(), 5000);
        QCOMPARE(qint64(bus->servicePid(service).value()), primary.processId());
        for (const auto &flag : {QStringLiteral("--show-search"), QStringLiteral("--search-clipboard")}) {
            QProcess secondary;
            secondary.setProcessEnvironment(environment);
            secondary.start(QStringLiteral(KLOOKUP_BINARY), {flag});
            QVERIFY(secondary.waitForFinished(5000));
            QCOMPARE(secondary.exitStatus(), QProcess::NormalExit);
            QCOMPARE(secondary.exitCode(), 0);
            QCOMPARE(primary.state(), QProcess::Running);
            QCOMPARE(qint64(bus->servicePid(service).value()), primary.processId());
        }
        primary.terminate();
        QVERIFY(primary.waitForFinished(3000));
        QTRY_VERIFY(!bus->isServiceRegistered(service).value());
        primary.start(QStringLiteral(KLOOKUP_BINARY), {QStringLiteral("--show-search")});
        QVERIFY(primary.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(bus->isServiceRegistered(service).value(), 5000);
        QCOMPARE(qint64(bus->servicePid(service).value()), primary.processId());
        primary.terminate();
        QVERIFY(primary.waitForFinished(3000));
    }
};
QTEST_GUILESS_MAIN(ShortcutActivationTest)
#include "test_shortcut_activation.moc"
