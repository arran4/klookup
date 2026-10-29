// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jsprovider.h"
#include "providerhostservice.h"
#include "providerpackage.h"
#include "providerapiv1.h"
#include "mainwindow.h"
#include "searchtab.h"

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

class TestTvMazeScripted : public QObject
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

    void testScriptedSearchUsesExpectedFixture()
    {
        auto *mockNam = new MockTvMazeNam(m_searchFixture, m_showFixture, this);
        const QString tvmazeDir = QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze");
        auto pkg = ProviderPackage::fromDirectory(tvmazeDir);
        QVERIFY(pkg.isValid());
        QVERIFY(!pkg.isTrusted());
        pkg.setTrusted(true);

        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNam);
        JsProvider provider(pkg, this, hostService);
        QVERIFY(provider.isLoaded());
        QCOMPARE(provider.id(), QStringLiteral("tvmaze"));
        QCOMPARE(provider.name(), QStringLiteral("TVMaze"));

        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        const quint64 id = provider.search(QStringLiteral("girls"));
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toULongLong(), id);

        const auto results = spy.first().at(1).value<SearchPage>().results;
        QCOMPARE(results.size(), 2);
        QCOMPARE(results.at(0).id, QStringLiteral("139"));
        QCOMPARE(results.at(0).title, QStringLiteral("Girls"));
        QCOMPARE(results.at(0).type, QStringLiteral("tvshow"));
        QCOMPARE(results.at(0).url, QStringLiteral("https://www.tvmaze.com/shows/139/girls"));
        QVERIFY(!results.at(0).description.contains(QLatin1Char('<')));
        QVERIFY(results.at(0).additionalData.contains(QStringLiteral("language")));
        QVERIFY(results.at(0).additionalData.contains(QStringLiteral("status")));
    }

    void testScriptedCancellation()
    {
        auto *mockNam = new MockTvMazeNam(m_searchFixture, m_showFixture, this);
        mockNam->suppress = true;
        const QString tvmazeDir = QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze");
        auto pkg = ProviderPackage::fromDirectory(tvmazeDir);
        QVERIFY(pkg.isValid());
        pkg.setTrusted(true);
        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNam);
        JsProvider provider(pkg, this, hostService);
        QSignalSpy readySpy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        const quint64 id = provider.search(QStringLiteral("girls"));
        provider.cancel(id);
        QTest::qWait(50);
        QCOMPARE(readySpy.count(), 0);
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

    void testScriptPackageDiscoveryAndDefaultUntrusted()
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

        // 6. Verify the only shipped TVMaze implementation is scripted
        auto *activeTvMaze = window.provider(QStringLiteral("tvmaze"));
        QVERIFY(activeTvMaze != nullptr);
        auto *v1Adapter = qobject_cast<ProviderApiV1::ProviderV1ToLegacyAdapter *>(activeTvMaze);
        QVERIFY(v1Adapter != nullptr);
        QCOMPARE(activeTvMaze->name(), QStringLiteral("TVMaze"));

        // No duplicate native TVMaze plugin should be installed by this build.
        QVERIFY(window.fallbackProvider(QStringLiteral("tvmaze")) == nullptr);

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

        // 10. Hard cutover: disabling the script does not resurrect a
        // second, bundled native TVMaze implementation.
        mockNam->suppress = false;
        {
            KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
            KConfigGroup providersGroup(config, QStringLiteral("Providers"));
            providersGroup.writeEntry(QStringLiteral("tvmaze_Enabled"), false);
            config->sync();
        }
        window.loadPlugins({xdgDataHome + QStringLiteral("/klookup/providers.d")}, {});
        QVERIFY(window.provider(QStringLiteral("tvmaze")) == nullptr);
        QVERIFY(window.fallbackProvider(QStringLiteral("tvmaze")) == nullptr);
    }
};

QTEST_MAIN(TestTvMazeScripted)
#include "test_tvmaze_scripted.moc"
