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
            QVERIFY(semverRegex.match(fileVersion).hasMatch());
        }
    }

    void tagOverridesVersionFileAndOverrideWins()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString srcDir = dir.filePath(QStringLiteral("src"));
        QVERIFY(QDir().mkpath(srcDir + QStringLiteral("/cmake")));
        QVERIFY(QFile::copy(QStringLiteral(KLOOKUP_SOURCE_DIR) + QStringLiteral("/cmake/KLookupVersion.cmake"),
                            srcDir + QStringLiteral("/cmake/KLookupVersion.cmake")));

        QFile verFile(srcDir + QStringLiteral("/VERSION"));
        QVERIFY(verFile.open(QIODevice::WriteOnly | QIODevice::Text));
        verFile.write("0.1.0\n");
        verFile.close();

        QFile cmFile(srcDir + QStringLiteral("/CMakeLists.txt"));
        QVERIFY(cmFile.open(QIODevice::WriteOnly | QIODevice::Text));
        cmFile.write("cmake_minimum_required(VERSION 3.16)\n"
                     "include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/KLookupVersion.cmake\")\n"
                     "project(dummy VERSION ${KLOOKUP_NUMERIC_VERSION} LANGUAGES NONE)\n"
                     "message(STATUS \"TEST_VER=${KLOOKUP_VERSION_STRING}|${PROJECT_VERSION}\")\n");
        cmFile.close();

        auto runCmake = [&](const QStringList &extraArgs) -> QString {
            QTemporaryDir buildDir;
            QProcess p;
            QStringList args{QStringLiteral("-S"), srcDir, QStringLiteral("-B"), buildDir.path()};
            args.append(extraArgs);
            p.start(QStringLiteral("cmake"), args);
            if (!p.waitForFinished(15000) || p.exitCode() != 0)
                return {};
            return QString::fromUtf8(p.readAllStandardOutput());
        };

        // 1. Archive/untagged tree uses VERSION
        QVERIFY(runCmake({}).contains(QStringLiteral("TEST_VER=0.1.0|0.1.0")));

        // 2. Git repo with exact tag v1.2.3-rc1 overrides stale VERSION 0.1.0
        auto git = [&](const QStringList &args) {
            QProcess p;
            p.setWorkingDirectory(srcDir);
            p.start(QStringLiteral("git"), args);
            QVERIFY(p.waitForFinished(10000));
            QCOMPARE(p.exitCode(), 0);
        };
        git({QStringLiteral("init")});
        git({QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.invalid")});
        git({QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")});
        git({QStringLiteral("add"), QStringLiteral(".")});
        git({QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("init")});
        git({QStringLiteral("tag"), QStringLiteral("v1.2.3-rc1")});

        QVERIFY(runCmake({}).contains(QStringLiteral("TEST_VER=1.2.3-rc1|1.2.3")));

        // 3. Explicit -DKLOOKUP_VERSION overrides both tag and VERSION
        QVERIFY(runCmake({QStringLiteral("-DKLOOKUP_VERSION=9.8.7")}).contains(QStringLiteral("TEST_VER=9.8.7|9.8.7")));
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
