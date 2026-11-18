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

    }

    void gitVersionAndPackagerOverride()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString srcDir = dir.filePath(QStringLiteral("src"));
        QVERIFY(QDir().mkpath(srcDir + QStringLiteral("/cmake")));
        QVERIFY(QFile::copy(QStringLiteral(KLOOKUP_SOURCE_DIR) + QStringLiteral("/cmake/KLookupVersion.cmake"),
                            srcDir + QStringLiteral("/cmake/KLookupVersion.cmake")));

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

        // 1. Plain copy without Git/metadata must never claim a release.
        QVERIFY(runCmake({}).contains(QStringLiteral("TEST_VER=0.0.0-unknown|0.0.0")));

        // 2. Git repo without any tag reports the actual commit, not a static version.
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
        QVERIFY(runCmake({}).contains(QStringLiteral("TEST_VER=0.0.0-dev+g")));
        git({QStringLiteral("tag"), QStringLiteral("v1.2.3-rc1")});

        QVERIFY(runCmake({}).contains(QStringLiteral("TEST_VER=1.2.3-rc1|1.2.3")));

        // 3. Development commits derive the nearest tag and commit distance/hash.
        git({QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("development")});
        QVERIFY(runCmake({}).contains(QStringLiteral("TEST_VER=1.2.3-rc1-1-g")));

        // 4. Explicit -DKLOOKUP_VERSION overrides the Git-derived version.
        QVERIFY(runCmake({QStringLiteral("-DKLOOKUP_VERSION=9.8.7")}).contains(QStringLiteral("TEST_VER=9.8.7|9.8.7")));
    }

    void gitArchiveWithoutDotGitResolvesTagVersion()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString repoDir = dir.filePath(QStringLiteral("repo"));
        QVERIFY(QDir().mkpath(repoDir + QStringLiteral("/cmake")));

        QVERIFY(QFile::copy(QStringLiteral(KLOOKUP_SOURCE_DIR) + QStringLiteral("/.gitattributes"),
                            repoDir + QStringLiteral("/.gitattributes")));
        QVERIFY(QFile::copy(QStringLiteral(KLOOKUP_SOURCE_DIR) + QStringLiteral("/cmake/KLookupVersion.cmake"),
                            repoDir + QStringLiteral("/cmake/KLookupVersion.cmake")));
        QVERIFY(QFile::copy(QStringLiteral(KLOOKUP_SOURCE_DIR) + QStringLiteral("/cmake/KLookupArchiveVersion.cmake"),
                            repoDir + QStringLiteral("/cmake/KLookupArchiveVersion.cmake")));

        QFile cmFile(repoDir + QStringLiteral("/CMakeLists.txt"));
        QVERIFY(cmFile.open(QIODevice::WriteOnly | QIODevice::Text));
        cmFile.write("cmake_minimum_required(VERSION 3.16)\n"
                     "include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/KLookupVersion.cmake\")\n"
                     "project(dummy VERSION ${KLOOKUP_NUMERIC_VERSION} LANGUAGES NONE)\n"
                     "message(STATUS \"TEST_VER=${KLOOKUP_VERSION_STRING}|${PROJECT_VERSION}\")\n");
        cmFile.close();

        auto runGit = [&](const QStringList &args, const QString &cwd) -> QByteArray {
            QProcess p;
            p.setWorkingDirectory(cwd);
            p.start(QStringLiteral("git"), args);
            if (!p.waitForFinished(15000) || p.exitCode() != 0)
                return {};
            return p.readAllStandardOutput();
        };

        QVERIFY(!runGit({QStringLiteral("init")}, repoDir).isNull());
        runGit({QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.invalid")}, repoDir);
        runGit({QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")}, repoDir);
        runGit({QStringLiteral("add"), QStringLiteral(".")}, repoDir);
        runGit({QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("init")}, repoDir);

        auto extractAndConfigure = [&](const QString &ref, const QString &name, const QStringList &cmakeArgs) -> QString {
            const QString tarPath = dir.filePath(name + QStringLiteral(".tar"));
            const QString outDir = dir.filePath(name);
            if (!QDir().mkpath(outDir))
                return {};

            QProcess arch;
            arch.setWorkingDirectory(repoDir);
            arch.setStandardOutputFile(tarPath);
            arch.start(QStringLiteral("git"), QStringList{QStringLiteral("archive"), QStringLiteral("--format=tar"), ref});
            if (!arch.waitForFinished(15000) || arch.exitCode() != 0)
                return {};

            QProcess untar;
            untar.start(QStringLiteral("tar"), QStringList{QStringLiteral("-xf"), tarPath, QStringLiteral("-C"), outDir});
            if (!untar.waitForFinished(15000) || untar.exitCode() != 0)
                return {};

            if (QFileInfo::exists(outDir + QStringLiteral("/.git")))
                return {};

            QTemporaryDir buildDir;
            QProcess cmakeProc;
            QStringList args{QStringLiteral("-S"), outDir, QStringLiteral("-B"), buildDir.path()};
            args.append(cmakeArgs);
            cmakeProc.start(QStringLiteral("cmake"), args);
            if (!cmakeProc.waitForFinished(15000) || cmakeProc.exitCode() != 0)
                return {};
            return QString::fromUtf8(cmakeProc.readAllStandardOutput());
        };

        // 1. Archive of a commit with no reachable tag retains its commit identity.
        QVERIFY(extractAndConfigure(QStringLiteral("HEAD"), QStringLiteral("untagged"), {})
                    .contains(QStringLiteral("TEST_VER=0.0.0-dev+g")));

        // 2. Archive of v0.1.1 tag resolves 0.1.1 without Git or an override.
        runGit({QStringLiteral("tag"), QStringLiteral("v0.1.1")}, repoDir);
        QVERIFY(extractAndConfigure(QStringLiteral("v0.1.1"), QStringLiteral("tagged-v0.1.1"), {})
                    .contains(QStringLiteral("TEST_VER=0.1.1|0.1.1")));

        // 3. Untagged development archives retain the nearest tag and commit identity.
        runGit({QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-m"), QStringLiteral("development")}, repoDir);
        QVERIFY(extractAndConfigure(QStringLiteral("HEAD"), QStringLiteral("development"), {})
                    .contains(QStringLiteral("TEST_VER=0.1.1-1-g")));

        // 4. Explicit -DKLOOKUP_VERSION still overrides archive tag
        QVERIFY(extractAndConfigure(QStringLiteral("v0.1.1"), QStringLiteral("override-v0.1.1"),
                                    {QStringLiteral("-DKLOOKUP_VERSION=0.1.2")})
                    .contains(QStringLiteral("TEST_VER=0.1.2|0.1.2")));
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
