#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

class SessionLifecycleTest : public QObject
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
        helper.start(QCoreApplication::applicationDirPath() + QStringLiteral("/test_session_lifecycle_helper"), { mode });
        if (!helper.waitForStarted() || !helper.waitForFinished()
            || helper.exitStatus() != QProcess::NormalExit || helper.exitCode() != 0) {
            qWarning().noquote() << "Lifecycle helper failed:" << helper.errorString()
                                 << helper.readAllStandardError();
            return {};
        }
        return QString::fromUtf8(helper.readAllStandardOutput());
    }

    void verifyLifecycle(const QString &mode)
    {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        const QString expected = QStringLiteral("STATE tabs=%1-one|%1-two;history=%1-two|%1-one")
                                     .arg(mode);
        QVERIFY(runHelper(mode, config.path()).contains(expected));
        QVERIFY(runHelper(QStringLiteral("restore"), config.path()).contains(expected));
    }

private slots:
    void visibleQuitPersistsSession() { verifyLifecycle(QStringLiteral("visible-quit")); }
    void trayQuitPersistsSession() { verifyLifecycle(QStringLiteral("tray-quit")); }
    void closeWithoutTrayPersistsSession() { verifyLifecycle(QStringLiteral("close")); }
    void hideToTrayDoesNotPersistBeforeQuit() { verifyLifecycle(QStringLiteral("hide")); }
};

QTEST_GUILESS_MAIN(SessionLifecycleTest)
#include "test_session_lifecycle.moc"
