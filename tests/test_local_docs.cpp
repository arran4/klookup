// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../providers/localdocs/provider_localdocs.h"
#include "localdocscanner.h"
#include "providerdefaults.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

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
        QTRY_COMPARE_WITH_TIMEOUT(okSpy.count(), 1, 5000);
        const auto results = okSpy.first().at(1).value<QList<SearchResult>>();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().title, QStringLiteral("readme.markdown"));
        QCOMPARE(results.first().additionalData.value(QStringLiteral("root")).toString(), QStringLiteral("Docs"));
        // Never leak full local filesystem path in SearchResult
        QVERIFY(!results.first().id.contains(docsDir.path()));
    }

    void testAsyncSearchAndPreviewResponsivenessAndCancellation()
    {
        QTemporaryDir largeDir;
        QVERIFY(largeDir.isValid());
        for (int i = 0; i < 60; ++i) {
            const QString sub = QStringLiteral("bucket_%1/doc_%2.md").arg(i % 10).arg(i);
            const QByteArray payload = (i == 59)
                ? QByteArrayLiteral("# Final\nkeyword_fresh_only\n")
                : QByteArrayLiteral("# Note\nkeyword_stale_many filler content for async scan test\n");
            QVERIFY(writeDoc(largeDir.filePath(sub), payload));
        }

        LocalScanRoot root{QStringLiteral("big"), QStringLiteral("Large Docs"), largeDir.path(), 3, true};
        ProviderLocalDocs provider;
        provider.setRootsForTesting({root});

        // 1. search() is non-blocking and GUI event loop stays responsive
        QSignalSpy okSpy(&provider, &SearchProvider::searchCompleted);
        bool timerFired = false;
        QTimer::singleShot(0, &provider, [&timerFired]() { timerFired = true; });
        provider.search(QStringLiteral("keyword_stale_many"));
        QCOMPARE(okSpy.count(), 0); // async: not emitted synchronously on GUI thread
        QCoreApplication::processEvents();
        QVERIFY(timerFired);

        // 2. Cancelling stale search prevents stale result emission; superseding search delivers only fresh results
        provider.cancel();
        QTest::qWait(80);
        QCOMPARE(okSpy.count(), 0);

        provider.search(QStringLiteral("keyword_stale_many"));
        provider.search(QStringLiteral("keyword_fresh_only"));
        QTRY_COMPARE_WITH_TIMEOUT(okSpy.count(), 1, 5000);
        const auto results = okSpy.first().at(1).value<QList<SearchResult>>();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().title, QStringLiteral("bucket_9/doc_59.md"));

        // 3. Options widget preview runs asynchronously and survives immediate destruction
        QWidget *opts = provider.createOptionsWidget();
        QVERIFY(opts != nullptr);
        auto *previewLabel = opts->findChild<QLabel *>(QStringLiteral("localDocPreview"));
        QVERIFY(previewLabel != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(previewLabel->text().contains(QStringLiteral("60 readable file(s)")), 5000);
        delete opts;

        // Destroy mid-scan guard
        QWidget *opts2 = provider.createOptionsWidget();
        delete opts2;
        QTest::qWait(50);

        // 4. Stress test: rapid repeated searches, provider destruction mid-scan, and rapid options churn
        for (int round = 0; round < 5; ++round) {
            auto *ephemeral = new ProviderLocalDocs;
            ephemeral->setRootsForTesting({root});
            QSignalSpy ephemSpy(ephemeral, &SearchProvider::searchCompleted);
            for (int burst = 0; burst < 10; ++burst) {
                ephemeral->search(QStringLiteral("keyword_stale_many"));
            }
            QWidget *w = ephemeral->createOptionsWidget();
            delete w;
            delete ephemeral;
            QCoreApplication::processEvents();
            QCOMPARE(ephemSpy.count(), 0);
        }
        QTest::qWait(80);
    }
};

QTEST_MAIN(TestLocalDocs)
#include "test_local_docs.moc"
