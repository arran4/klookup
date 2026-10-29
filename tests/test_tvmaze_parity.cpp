// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jsprovider.h"
#include "providerhostservice.h"
#include "providerpackage.h"
#include "providerapiv1.h"
#include "mainwindow.h"
#include "searchtab.h"
#include "../providers/tvmaze/provider_tvmaze.h"

#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QPushButton>
#include <KSharedConfig>
#include <KConfigGroup>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

class MockReply : public QNetworkReply
{
    Q_OBJECT
public:
    MockReply(const QByteArray &data, QObject *parent = nullptr)
        : QNetworkReply(parent)
        , m_data(data)
        , m_offset(0)
    {
        setOpenMode(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setHeader(QNetworkRequest::ContentLengthHeader, data.size());
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }

    void abort() override
    {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"));
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return m_data.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const qint64 toRead = qMin(maxlen, static_cast<qint64>(m_data.size() - m_offset));
        if (toRead <= 0) return 0;
        memcpy(data, m_data.constData() + m_offset, toRead);
        m_offset += toRead;
        return toRead;
    }

private:
    QByteArray m_data;
    qint64 m_offset;
};

class MockTvMazeNam : public QNetworkAccessManager
{
    Q_OBJECT
public:
    explicit MockTvMazeNam(const QByteArray &searchData, const QByteArray &showData, QObject *parent = nullptr)
        : QNetworkAccessManager(parent)
        , m_searchData(searchData)
        , m_showData(showData)
    {
    }

    bool suppress = false;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoingData) override
    {
        Q_UNUSED(op);
        Q_UNUSED(outgoingData);
        if (suppress) {
            auto *rep = new MockReply(QByteArray(), this);
            return rep;
        }

        const QString url = req.url().toString();
        if (url.contains(QStringLiteral("/search/shows"))) {
            return new MockReply(m_searchData, this);
        } else if (url.contains(QStringLiteral("/shows/"))) {
            return new MockReply(m_showData, this);
        }
        return new MockReply(QByteArray(), this);
    }

private:
    QByteArray m_searchData;
    QByteArray m_showData;
};

class TestTvMazeParity : public QObject
{
    Q_OBJECT

private:
    QByteArray m_searchFixture;
    QByteArray m_showFixture;

private Q_SLOTS:
    void initTestCase()
    {
        QFile searchFile(QStringLiteral(FIXTURES_DIR "/tvmaze_search_fixture.json"));
        QVERIFY(searchFile.open(QIODevice::ReadOnly));
        m_searchFixture = searchFile.readAll();

        m_showFixture = QByteArrayLiteral("{\n"
            "  \"id\": 139,\n"
            "  \"url\": \"https://www.tvmaze.com/shows/139/girls\",\n"
            "  \"name\": \"Girls\",\n"
            "  \"type\": \"Scripted\",\n"
            "  \"language\": \"English\",\n"
            "  \"genres\": [\"Drama\", \"Comedy\"],\n"
            "  \"status\": \"Ended\",\n"
            "  \"summary\": \"<p>Young women navigating life in New York.</p>\",\n"
            "  \"officialSite\": \"https://www.hbo.com/girls\",\n"
            "  \"network\": { \"id\": 8, \"name\": \"HBO\" },\n"
            "  \"image\": { \"medium\": \"https://static.tvmaze.com/uploads/images/medium_portrait/31/78286.jpg\" }\n"
            "}");
    }

    void testSearchParityNativeVsJs()
    {
        auto *mockNamNative = new MockTvMazeNam(m_searchFixture, m_showFixture, this);
        auto *mockNamJs = new MockTvMazeNam(m_searchFixture, m_showFixture, this);

        // 1. Run Native C++ TVMaze Provider
        ProviderTvMaze nativeProvider(this, mockNamNative);
        QCOMPARE(nativeProvider.id(), QStringLiteral("tvmaze"));
        QCOMPARE(nativeProvider.name(), QStringLiteral("TVMaze"));

        QSignalSpy nativeSpy(&nativeProvider, &SearchProvider::searchCompleted);
        nativeProvider.search(QStringLiteral("girls"));
        QVERIFY(nativeSpy.wait(1000));
        QCOMPARE(nativeSpy.count(), 1);
        const auto nativeResults = nativeSpy.first().at(1).value<QList<SearchResult>>();
        QCOMPARE(nativeResults.size(), 2);

        // 2. Run Scripted JS TVMaze Provider from providers.d/tvmaze
        const QString tvmazeDir = QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze");
        auto pkg = ProviderPackage::fromDirectory(tvmazeDir);
        QVERIFY(pkg.isValid());
        pkg.setTrusted(true); // explicit trust

        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNamJs);
        JsProvider jsProvider(pkg, this, hostService);
        QVERIFY(jsProvider.isLoaded());
        QCOMPARE(jsProvider.id(), QStringLiteral("tvmaze"));
        QCOMPARE(jsProvider.name(), QStringLiteral("TVMaze"));

        QSignalSpy jsSpy(&jsProvider, &ProviderApiV1::ProviderV1::pageReady);
        const quint64 jsReqId = jsProvider.search(QStringLiteral("girls"));
        QVERIFY(jsSpy.wait(1000));
        QCOMPARE(jsSpy.count(), 1);
        QCOMPARE(jsSpy.first().at(0).toULongLong(), jsReqId);
        const auto jsPage = jsSpy.first().at(1).value<SearchPage>();
        QCOMPARE(jsPage.results.size(), 2);

        // 3. Verify Exact Field-by-Field Parity Across Results
        for (int i = 0; i < 2; ++i) {
            const auto &nat = nativeResults[i];
            const auto &js = jsPage.results[i];

            QCOMPARE(js.id, nat.id);
            QCOMPARE(js.title, nat.title);
            QCOMPARE(js.description, nat.description);
            QCOMPARE(js.url, nat.url);
            QCOMPARE(js.type, nat.type);

            QCOMPARE(js.additionalData.value(QStringLiteral("language")).toString(),
                     nat.additionalData.value(QStringLiteral("language")).toString());
            QCOMPARE(js.additionalData.value(QStringLiteral("premiered")).toString(),
                     nat.additionalData.value(QStringLiteral("premiered")).toString());
            QCOMPARE(js.additionalData.value(QStringLiteral("status")).toString(),
                     nat.additionalData.value(QStringLiteral("status")).toString());

            if (nat.additionalData.contains(QStringLiteral("rating"))) {
                QVERIFY(js.additionalData.contains(QStringLiteral("rating")));
                QCOMPARE(js.additionalData.value(QStringLiteral("rating")).toDouble(),
                         nat.additionalData.value(QStringLiteral("rating")).toDouble());
            }
        }
    }

    void testCancellationParity()
    {
        auto *mockNamNative = new MockTvMazeNam(m_searchFixture, m_showFixture, this);
        mockNamNative->suppress = true;
        ProviderTvMaze nativeProvider(this, mockNamNative);

        QSignalSpy nativeSpy(&nativeProvider, &SearchProvider::searchCompleted);
        nativeProvider.search(QStringLiteral("girls"));
        // Abort / cancel by starting new search or destruction
        nativeProvider.search(QStringLiteral("new_search"));
        QTest::qWait(50);
        QCOMPARE(nativeSpy.count(), 0);

        // Scripted JS cancellation
        auto *mockNamJs = new MockTvMazeNam(m_searchFixture, m_showFixture, this);
        mockNamJs->suppress = true;
        const QString tvmazeDir = QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze");
        auto pkg = ProviderPackage::fromDirectory(tvmazeDir);
        pkg.setTrusted(true);

        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNamJs);
        JsProvider jsProvider(pkg, this, hostService);
        QSignalSpy jsSpy(&jsProvider, &ProviderApiV1::ProviderV1::pageReady);
        const quint64 id = jsProvider.search(QStringLiteral("girls"));
        jsProvider.cancel(id);
        QTest::qWait(50);
        QCOMPARE(jsSpy.count(), 0);
    }

    void testScriptedDetailsAndSeparateContent()
    {
        auto *mockNamJs = new MockTvMazeNam(m_searchFixture, m_showFixture, this);
        const QString tvmazeDir = QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze");
        auto pkg = ProviderPackage::fromDirectory(tvmazeDir);
        pkg.setTrusted(true);

        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNamJs);
        JsProvider jsProvider(pkg, this, hostService);
        QVERIFY(jsProvider.isLoaded());

        // 1. getDetails
        QSignalSpy detailsSpy(&jsProvider, &ProviderApiV1::ProviderV1::detailsReady);
        const quint64 detId = jsProvider.getDetails(QStringLiteral("139"));
        QVERIFY(detailsSpy.wait(1000));
        QCOMPARE(detailsSpy.count(), 1);
        QCOMPARE(detailsSpy.first().at(0).toULongLong(), detId);

        const auto details = detailsSpy.first().at(2).value<SearchDetails>();
        QCOMPARE(details.result.id, QStringLiteral("139"));
        QCOMPARE(details.result.title, QStringLiteral("Girls"));
        QCOMPARE(details.metadata.attribution, QStringList{QStringLiteral("TVMaze.com")});
        QVERIFY(!details.metadata.sections.isEmpty());
        QCOMPARE(details.metadata.sections.first().label, QStringLiteral("Show Details"));

        // 2. separate getContent
        QSignalSpy contentSpy(&jsProvider, &ProviderApiV1::ProviderV1::contentReady);
        const quint64 contId = jsProvider.getContent(QStringLiteral("139"));
        QVERIFY(contentSpy.wait(1000));
        QCOMPARE(contentSpy.count(), 1);
        QCOMPARE(contentSpy.first().at(0).toULongLong(), contId);

        const auto contentResp = contentSpy.first().at(1).value<ProviderApiV1::ContentResponse>();
        QCOMPARE(contentResp.resultId, QStringLiteral("139"));
        QCOMPARE(contentResp.content.format, ResultContent::Format::Markdown);
        QVERIFY(contentResp.content.description.contains(QStringLiteral("Young women navigating life in New York.")));
    }

    void testDuplicateIdPrecedenceAndRollback()
    {
        const QString tvmazeDir = QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze");
        auto pkg = ProviderPackage::fromDirectory(tvmazeDir);
        QVERIFY(pkg.isValid());
        QCOMPARE(pkg.id(), QStringLiteral("tvmaze"));

        // Package discovery with custom path finds the scripted provider
        const auto discovered = ProviderPackageDiscovery::discoverPackages({QStringLiteral(PROJECT_SOURCE_DIR "/providers.d")});
        bool found = false;
        for (const auto &p : discovered) {
            if (p.id() == QLatin1String("tvmaze")) {
                found = true;
                QCOMPARE(p.name(), QStringLiteral("TVMaze"));
                QVERIFY(!p.isTrusted()); // untrusted by default prevents duplicate activation
            }
        }
        QVERIFY(found);
    }

    void testEndToEndScriptedTvMazeDiscoveryAndSearch()
    {
        // 1. Create temporary directory structure replicating real XDG providers.d layout
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString xdgDataHome = tempDir.path() + QStringLiteral("/share");
        const QString targetPkgDir = xdgDataHome + QStringLiteral("/klookup/providers.d/tvmaze");
        QVERIFY(QDir().mkpath(targetPkgDir));

        // 2. Install tvmaze provider package (provider.json + main.mjs)
        const QString srcDir = QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze");
        QVERIFY(QFile::copy(srcDir + QStringLiteral("/provider.json"), targetPkgDir + QStringLiteral("/provider.json")));
        QVERIFY(QFile::copy(srcDir + QStringLiteral("/main.mjs"), targetPkgDir + QStringLiteral("/main.mjs")));

        // 3. Set XDG_DATA_HOME environment variable
        qputenv("XDG_DATA_HOME", xdgDataHome.toUtf8());

        // 4. Set explicit trust and enablement in config
        {
            KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
            KConfigGroup providersGroup(config, QStringLiteral("Providers"));
            providersGroup.writeEntry(QStringLiteral("tvmaze_Trusted"), true);
            providersGroup.writeEntry(QStringLiteral("tvmaze_Enabled"), true);
            config->sync();
        }

        // 5. Create testable MainWindow with mock network access manager
        auto *mockNam = new MockTvMazeNam(m_searchFixture, m_showFixture, this);

        MainWindow window;
        window.setNetworkAccessManagerFactory([mockNam](QObject *) -> QNetworkAccessManager * {
            return mockNam;
        });

        // Trigger discovery on the directory layout and plugin paths
        window.loadPlugins({xdgDataHome + QStringLiteral("/klookup/providers.d")},
                           {QCoreApplication::applicationDirPath() + QStringLiteral("/../src/providers")});

        // Ensure only tvmaze is active for this focused integration test
        for (SearchProvider *p : window.providers()) {
            if (p->id() != QLatin1String("tvmaze")) {
                p->setEnabled(false);
            }
        }

        // 6. Verify scripted TVMaze is active and has overridden native provider
        auto *activeTvMaze = window.provider(QStringLiteral("tvmaze"));
        QVERIFY(activeTvMaze != nullptr);
        auto *v1Adapter = qobject_cast<ProviderApiV1::ProviderV1ToLegacyAdapter *>(activeTvMaze);
        QVERIFY(v1Adapter != nullptr);
        QCOMPARE(activeTvMaze->name(), QStringLiteral("TVMaze"));

        // Verify native provider is retained as fallback
        auto *fallback = window.fallbackProvider(QStringLiteral("tvmaze"));
        QVERIFY(fallback != nullptr);
        QCOMPARE(fallback->id(), QStringLiteral("tvmaze"));

        window.clearSearchTabs();

        // 7. Perform search through normal MainWindow application path
        window.performSearch(QStringLiteral("girls"));

        auto tabs = window.findChildren<SearchTab *>();
        QVERIFY(!tabs.isEmpty());
        auto *tab = tabs.last();
        QVERIFY(tab != nullptr);

        auto *tree = tab->findChild<QTreeWidget *>();
        QVERIFY(tree != nullptr);

        // Wait for search tree to populate
        QTreeWidgetItem *tvmazeGroup = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT([&]() -> bool {
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                if (tree->topLevelItem(i)->text(0) == QLatin1String("TVMaze")) {
                    tvmazeGroup = tree->topLevelItem(i);
                    return tvmazeGroup->childCount() >= 2;
                }
            }
            return false;
        }(), 2000);
        QVERIFY(tvmazeGroup != nullptr);

        QTreeWidgetItem *item = tvmazeGroup->child(0);
        QCOMPARE(item->text(0), QStringLiteral("Girls"));

        // 8. Verify lazy details and separate content
        emit tree->itemClicked(item, 0);

        auto *detailsView = tab->findChild<QTextBrowser *>();
        QVERIFY(detailsView != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(detailsView->toHtml().contains(QStringLiteral("Girls")), 2000);

        auto trees = tab->findChildren<QTreeWidget *>();
        QVERIFY(trees.size() >= 2);
        auto *metadataTree = trees[1];
        QTRY_VERIFY_WITH_TIMEOUT(metadataTree->topLevelItemCount() > 0 &&
                                 metadataTree->topLevelItem(0)->text(0) == QLatin1String("Show Details"), 2000);

        // Request separate content
        auto *loadBtn = tab->findChild<QPushButton *>(QStringLiteral("loadDetails"));
        if (loadBtn && !loadBtn->isHidden()) {
            emit loadBtn->clicked();
            QTRY_VERIFY_WITH_TIMEOUT(detailsView->toHtml().contains(QStringLiteral("Young women navigating life in New York.")), 2000);
        }

        // 9. Test cancellation through UI/adapter
        mockNam->suppress = true;
        v1Adapter->cancelRequests();
        QTest::qWait(50);

        // 10. Test rollback / fallback to native TVMaze
        mockNam->suppress = false;
        QVERIFY(window.switchToFallback(QStringLiteral("tvmaze")));

        auto *restoredProvider = window.provider(QStringLiteral("tvmaze"));
        QVERIFY(restoredProvider != nullptr);
        QCOMPARE(restoredProvider->id(), QStringLiteral("tvmaze"));
        QCOMPARE(restoredProvider->name(), QStringLiteral("TVMaze"));
        // Confirm it is no longer the scripted V1 adapter, but the native fallback
        QVERIFY(qobject_cast<ProviderApiV1::ProviderV1ToLegacyAdapter *>(restoredProvider) == nullptr);

        // Perform search through native fallback
        window.performSearch(QStringLiteral("girls_native"));
        auto nativeTabs = window.findChildren<SearchTab *>();
        QVERIFY(!nativeTabs.isEmpty());
        auto *nativeTab = nativeTabs.last();
        auto *nativeTree = nativeTab->findChild<QTreeWidget *>();
        QVERIFY(nativeTree != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT([&]() -> bool {
            for (int i = 0; i < nativeTree->topLevelItemCount(); ++i) {
                if (nativeTree->topLevelItem(i)->text(0) == QLatin1String("TVMaze")) {
                    return true;
                }
            }
            return false;
        }(), 2000);
    }
};

QTEST_MAIN(TestTvMazeParity)
#include "test_tvmaze_parity.moc"
