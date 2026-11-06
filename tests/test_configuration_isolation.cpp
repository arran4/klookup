// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
class ConfigurationIsolationTest : public QObject {
    Q_OBJECT
private slots:
    void guiExecutablesPreserveInheritedUserConfig() {
        QTemporaryDir home; QVERIFY(home.isValid());
        QFile sentinel(home.filePath(QStringLiteral("klookuprc")));
        const QByteArray original("[Layout]\nSearchTabSplitter=111,999\nDetailWindowSplitter=222,888\nUserSentinel=keep-me\n");
        QVERIFY(sentinel.open(QIODevice::WriteOnly)); sentinel.write(original); sentinel.close();
        for (const auto &command : QList<QStringList>{
             {QStringLiteral(SEARCHTAB_TEST), QStringLiteral("testPersistenceAcrossTabs")},
             {QStringLiteral(WINDOWS_TEST), QStringLiteral("testResultDetailWindowSplitterAndPersistence")}}) {
            QProcess process;
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("XDG_CONFIG_HOME"), home.path());
            env.insert(QStringLiteral("HOME"), home.path());
            process.setProcessEnvironment(env);
            process.start(command[0], command.mid(1));
            QVERIFY(process.waitForFinished(30000));
            const auto output = process.readAllStandardOutput() + process.readAllStandardError();
            QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, output.constData());
            QVERIFY(sentinel.open(QIODevice::ReadOnly));
            QCOMPARE(sentinel.readAll(), original); sentinel.close();
        }
    }
};
QTEST_GUILESS_MAIN(ConfigurationIsolationTest)
#include "test_configuration_isolation.moc"
