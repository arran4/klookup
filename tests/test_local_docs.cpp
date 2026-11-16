// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../providers/localdocs/provider_localdocs.h"
#include "localdocscanner.h"
#include "providerdefaults.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
bool writeDoc(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    f.close();
    return true;
}
} // namespace

class TestLocalDocs : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultDisabledAndBroadHomeCheck()
    {
        QVERIFY(!ProviderDefaults::enabled(QStringLiteral("localdocs")));
        QVERIFY(LocalDocScanner::isBroadOrHomeRoot(QDir::homePath()));
        QVERIFY(LocalDocScanner::isBroadOrHomeRoot(QStringLiteral("/")));
        QVERIFY(!LocalDocScanner::isBroadOrHomeRoot(QStringLiteral("/tmp/some-docs")));
    }

    void testAcceptsOnlyAllowedFormatsAndBlocksSymlinksHiddenAndDepth()
    {
        QTemporaryDir rootDir, outsideDir;
        QVERIFY(rootDir.isValid() && outsideDir.isValid());

        QVERIFY(writeDoc(rootDir.filePath(QStringLiteral("guide.md")), QByteArrayLiteral("# KLookup Guide\nOffline search keyword_alpha\n")));
        QVERIFY(writeDoc(rootDir.filePath(QStringLiteral("notes.txt")), QByteArrayLiteral("Plain text keyword_alpha\n")));
        QVERIFY(writeDoc(rootDir.filePath(QStringLiteral("sub/page.html")),
                         QByteArrayLiteral("<html><script>evil()</script><body><p>HTML keyword_alpha</p></body></html>")));
        QVERIFY(writeDoc(rootDir.filePath(QStringLiteral("deep/a/b/c/too_deep.md")), QByteArrayLiteral("keyword_alpha")));
        QVERIFY(writeDoc(rootDir.filePath(QStringLiteral(".git/secret.md")), QByteArrayLiteral("keyword_alpha")));
        QVERIFY(writeDoc(rootDir.filePath(QStringLiteral("ignored.qch")), QByteArrayLiteral("keyword_alpha")));
        QVERIFY(writeDoc(rootDir.filePath(QStringLiteral("ignored.pdf")), QByteArrayLiteral("keyword_alpha")));
        QVERIFY(writeDoc(rootDir.filePath(QStringLiteral("binary.txt")), QByteArray("bad\0keyword_alpha", 17)));
        QVERIFY(writeDoc(outsideDir.filePath(QStringLiteral("escaped.txt")), QByteArrayLiteral("keyword_alpha")));
        QFile::link(outsideDir.filePath(QStringLiteral("escaped.txt")), rootDir.filePath(QStringLiteral("link.txt")));

        LocalScanRoot root;
        root.id = QStringLiteral("docs1");
        root.label = QStringLiteral("Project Docs");
        root.path = rootDir.path();
        root.maxDepth = 1; // sub/page.html is depth 1; deep/a/b/c is depth 4

        const LocalScanPreview prev = LocalDocScanner::preview({root});
        QCOMPARE(prev.fileCount, 4); // guide.md, notes.txt, sub/page.html, binary.txt (too_deep/.git/ignored/symlink skipped)

        const auto matches = LocalDocScanner::search(QStringLiteral("keyword_alpha"), {root});
        QCOMPARE(matches.size(), 3);
        for (const auto &m : matches) {
            QVERIFY(!m.relativePath.contains(QStringLiteral("too_deep")));
            QVERIFY(!m.relativePath.contains(QStringLiteral(".git")));
            QVERIFY(!m.relativePath.contains(QStringLiteral("ignored")));
            QVERIFY(!m.relativePath.contains(QStringLiteral("binary")));
            QVERIFY(!m.relativePath.contains(QStringLiteral("link")));
            QVERIFY(!m.snippet.contains(QStringLiteral("evil()")));
        }

        // Safe cancellation stops immediately
        std::atomic_bool cancelled{true};
        bool wasCancelled = false;
        const auto cancelledMatches = LocalDocScanner::search(QStringLiteral("keyword_alpha"), {root}, LocalScanPolicy(), &cancelled, &wasCancelled);
        QVERIFY(wasCancelled);
        QVERIFY(cancelledMatches.isEmpty());
    }

    void testRootPersistenceAndProviderSearchAndUnavailableState()
    {
        QTemporaryDir cfgDir, docsDir;
        QVERIFY(cfgDir.isValid() && docsDir.isValid());
        QVERIFY(writeDoc(docsDir.filePath(QStringLiteral("readme.markdown")), QByteArrayLiteral("Offline markdown match_beta")));

        KConfig cfg(cfgDir.filePath(QStringLiteral("klookuprc")), KConfig::SimpleConfig);
        KConfigGroup group(&cfg, QStringLiteral("Provider-localdocs"));
        LocalScanRoot root{QStringLiteral("r1"), QStringLiteral("Docs"), docsDir.path(), 2, true};
        LocalDocScanner::writeRoots(group, {root});
        const auto restored = LocalDocScanner::readRoots(group);
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.first().path, docsDir.path());
        QCOMPARE(restored.first().maxDepth, 2);

        ProviderLocalDocs provider;
        QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);
        provider.setRootsForTesting({});
        provider.search(QStringLiteral("match_beta"));
        QCOMPARE(failSpy.count(), 1);

        QSignalSpy okSpy(&provider, &SearchProvider::searchCompleted);
        provider.setRootsForTesting(restored);
        provider.search(QStringLiteral("match_beta"));
        QCOMPARE(okSpy.count(), 1);
        const auto results = okSpy.first().at(1).value<QList<SearchResult>>();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().title, QStringLiteral("readme.markdown"));
        QCOMPARE(results.first().additionalData.value(QStringLiteral("root")).toString(), QStringLiteral("Docs"));
        // Never leak full local filesystem path in SearchResult
        QVERIFY(!results.first().id.contains(docsDir.path()));
    }
};

QTEST_MAIN(TestLocalDocs)
#include "test_local_docs.moc"
