// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <KAboutData>
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QtTest>

class TestVersionConsistency : public QObject
{
    Q_OBJECT

private slots:
    void versionFormatAndConsistency()
    {
        const QString projectVersion = QString::fromUtf8(KLOOKUP_TEST_PROJECT_VERSION);
        const QString versionString = QString::fromUtf8(KLOOKUP_TEST_VERSION_STRING);

        QVERIFY(!projectVersion.isEmpty());
        QVERIFY(!versionString.isEmpty());

        static const QRegularExpression semverRegex(QStringLiteral(R"(^\d+\.\d+\.\d+)"));
        QVERIFY2(semverRegex.match(projectVersion).hasMatch(),
                 qPrintable(QStringLiteral("Project version '%1' is not semver").arg(projectVersion)));
        QVERIFY2(versionString.startsWith(projectVersion),
                 qPrintable(QStringLiteral("Version string '%1' does not start with project version '%2'").arg(versionString, projectVersion)));

        QVERIFY2(projectVersion != QStringLiteral("0.0.2"), "Project version is still hardcoded to obsolete 0.0.2");

        const QString versionFilePath = QStringLiteral(KLOOKUP_SOURCE_DIR) + QStringLiteral("/VERSION");
        if (QFile::exists(versionFilePath)) {
            QFile versionFile(versionFilePath);
            QVERIFY(versionFile.open(QIODevice::ReadOnly | QIODevice::Text));
            const QString fileVersion = QString::fromUtf8(versionFile.readAll()).trimmed();
            QVERIFY(!fileVersion.isEmpty());
            QVERIFY(versionString.startsWith(fileVersion) || fileVersion.startsWith(projectVersion));
        }
    }

    void binaryReportsExpectedVersion()
    {
        const QString binaryPath = QString::fromUtf8(KLOOKUP_TEST_BINARY_PATH);
        QVERIFY(QFile::exists(binaryPath));

        QProcess process;
        process.start(binaryPath, QStringList() << QStringLiteral("--version"));
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);

        const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        const QString versionString = QString::fromUtf8(KLOOKUP_TEST_VERSION_STRING);
        QVERIFY2(output.contains(versionString),
                 qPrintable(QStringLiteral("Binary output '%1' did not contain version '%2'").arg(output, versionString)));
        QVERIFY2(!output.contains(QStringLiteral("0.0.2")),
                 qPrintable(QStringLiteral("Binary output '%1' unexpectedly reported obsolete version 0.0.2").arg(output)));
    }
};

QTEST_GUILESS_MAIN(TestVersionConsistency)
#include "test_version_consistency.moc"
