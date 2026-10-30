// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jsprovider.h"
#include "providerhostservice.h"
#include "providerpackage.h"
#include "providerapiv1.h"
#include "mainwindow.h"
#include "searchtab.h"
#include "settingsdialog.h"
#include <QListWidget>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QApplication>
#include <QCheckBox>
#include <QJsonDocument>
#include <QJsonObject>

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
    MockReply(const QByteArray &data, QObject *parent = nullptr, int status = 200, int delayMs = 0,
              bool transportError = false, std::function<void()> onAbort = {})
        : QNetworkReply(parent)
        , m_data(data)
        , m_offset(0), m_onAbort(std::move(onAbort))
    {
        setOpenMode(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentLengthHeader, data.size());
        if (delayMs >= 0) QTimer::singleShot(delayMs, this, [this, transportError] {
            if (transportError) setError(QNetworkReply::RemoteHostClosedError, QStringLiteral("Transport closed"));
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }

    void abort() override
    {
        if (m_onAbort) m_onAbort();
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"));
        setFinished(true);
        emit finished();
    }

    void complete()
    {
        setFinished(true);
        emit readyRead();
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
    std::function<void()> m_onAbort;
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
    int status = 200;
    int delayMs = 0;
    bool transportError = false;
    int aborted = 0;
    QList<QUrl> requests;
    QList<QPointer<MockReply>> replies;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoingData) override
    {
        Q_UNUSED(op);
        Q_UNUSED(outgoingData);
        requests.append(req.url());
        auto reply = [this](const QByteArray &data) {
            auto *result = new MockReply(data, this, status, suppress ? -1 : delayMs, transportError, [this] { ++aborted; });
            replies.append(result);
            return result;
        };

        const QString url = req.url().toString();
        if (url.contains(QStringLiteral("/search/shows"))) {
            return reply(m_searchData);
        } else if (url.contains(QStringLiteral("/shows/"))) {
            return reply(m_showData);
        }
        return reply(QByteArray());
    }

private:
    QByteArray m_searchData;
    QByteArray m_showData;
};

class TestTvMazeScripted : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_configRoot;
    QByteArray m_searchFixture;
    QByteArray m_showFixture;

private Q_SLOTS:
    void initTestCase()
    {
        qputenv("XDG_CONFIG_HOME", m_configRoot.path().toUtf8());
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
        QCOMPARE(mockNam->aborted, 1);
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

    void testSearchResponseFailures_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("transportError");
        QTest::addColumn<bool>("success");
        QTest::newRow("empty") << QByteArray("[]") << 200 << false << true;
        QTest::newRow("malformed") << QByteArray("{") << 200 << false << false;
        QTest::newRow("wrong-shape") << QByteArray("{}") << 200 << false << false;
        QTest::newRow("partial") << QByteArray("[null,{}, {\"show\":null}]") << 200 << false << true;
        QTest::newRow("rate-limit") << QByteArray("{}") << 429 << false << false;
        QTest::newRow("http-error") << QByteArray("{}") << 503 << false << false;
        QTest::newRow("transport") << QByteArray() << 0 << true << false;
    }

    void testSearchResponseFailures()
    {
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(bool, transportError);
        QFETCH(bool, success);
        MockTvMazeNam nam(body, m_showFixture);
        nam.status = status;
        nam.transportError = transportError;
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze"));
        pkg.setTrusted(true);
        ProviderHost::ProviderHostService host(pkg, nullptr, &nam);
        JsProvider provider(pkg, nullptr, &host);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        QSignalSpy failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        provider.search(QString::fromUtf8("Björk & 東京/?"));
        QTRY_COMPARE(ready.count() + failed.count(), 1);
        QCOMPARE(ready.count(), success ? 1 : 0);
        QCOMPARE(failed.count(), success ? 0 : 1);
        QVERIFY(nam.requests.first().toEncoded().contains("Bj%C3%B6rk%20%26%20%E6%9D%B1%E4%BA%AC%2F%3F"));
    }

    void testOverlappingSearchesAndLateReply()
    {
        MockTvMazeNam nam(m_searchFixture, m_showFixture);
        nam.delayMs = 20;
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze"));
        pkg.setTrusted(true);
        ProviderHost::ProviderHostService host(pkg, nullptr, &nam);
        JsProvider provider(pkg, nullptr, &host);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        QSignalSpy failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        const auto first = provider.search(QStringLiteral("first"));
        const auto second = provider.search(QStringLiteral("second"));
        provider.cancel(first);
        QCOMPARE(nam.aborted, 1);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first().first().toULongLong(), second);
        QTest::qWait(30);
        QCOMPARE(ready.count(), 1);
        QCOMPARE(failed.count(), 0);
    }

    void testFreshInstalledDiscovery()
    {
        if (!qEnvironmentVariableIsSet("KLOOKUP_STAGED_INSTALL")) QSKIP("Exercised by test_staged_install in an isolated installed prefix");
        MainWindow window;
        auto *active = window.provider(QStringLiteral("tvmaze"));
        QVERIFY(active);
        QVERIFY(qobject_cast<ProviderApiV1::ProviderV1ToLegacyAdapter *>(active));
        QVERIFY(!window.fallbackProvider(QStringLiteral("tvmaze")));
        bool system = false;
        QString installedRoot;
        for (const auto &record : window.packages()) {
            if (record.package.id() == QLatin1String("tvmaze")) {
                QCOMPARE(record.package.locationType(), ProviderPackage::LocationType::System);
                QVERIFY(record.package.isDistributionPackage());
                QVERIFY(record.package.isTrusted());
                installedRoot = record.package.packageDir();
                system = true;
            }
        }
        QVERIFY(system);
        auto *nam = new MockTvMazeNam(m_searchFixture, m_showFixture, this);
        window.setNetworkAccessManagerFactory([nam](QObject *) { return nam; });
        window.loadPlugins();
        for (auto *provider : window.providers()) provider->setEnabled(provider->id() == QLatin1String("tvmaze"));
        window.clearSearchTabs();
        window.performSearch(QStringLiteral("girls"));
        auto *tree = window.findChildren<SearchTab *>().last()->findChild<QTreeWidget *>();
        QVERIFY(tree);
        QTRY_COMPARE(nam->requests.size(), 1);
        QTRY_COMPARE(tree->topLevelItemCount(), 1);
        QTRY_COMPARE(tree->topLevelItem(0)->childCount(), 3); // Two results and the standard completion status.
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("Girls"));
        QCOMPARE(tree->topLevelItem(0)->child(2)->text(0), QStringLiteral("No more results"));
        // A real XDG user override wins precedence without inheriting system approval.
        const QString userRoot = qEnvironmentVariable("XDG_DATA_HOME") + QStringLiteral("/klookup/providers.d/tvmaze");
        QVERIFY(QDir().mkpath(userRoot));
        QVERIFY(QFile::copy(installedRoot + QStringLiteral("/provider.json"), userRoot + QStringLiteral("/provider.json")));
        QVERIFY(QFile::copy(installedRoot + QStringLiteral("/main.mjs"), userRoot + QStringLiteral("/main.mjs")));
        window.loadPlugins();
        QVERIFY(!window.provider(QStringLiteral("tvmaze")));
        bool userOverride = false;
        for (const auto &record : window.packages()) if (record.package.id() == QLatin1String("tvmaze"))
            userOverride = record.package.locationType() == ProviderPackage::LocationType::User;
        QVERIFY(userOverride);
        window.setPackageTrusted(QStringLiteral("tvmaze"), true);
        window.loadPlugins();
        QVERIFY(window.provider(QStringLiteral("tvmaze")));
        QVERIFY(QDir(userRoot).removeRecursively());
        window.loadPlugins();
        QVERIFY(!window.provider(QStringLiteral("tvmaze"))); // Replacement path requires review again.
        window.setPackageTrusted(QStringLiteral("tvmaze"), true);
        window.loadPlugins();
        QVERIFY(window.provider(QStringLiteral("tvmaze")));
        QFile upgraded(installedRoot + QStringLiteral("/main.mjs"));
        QVERIFY(upgraded.open(QIODevice::Append));
        upgraded.write("\n// package upgrade\n");
        upgraded.close();
        window.loadPlugins();
        QVERIFY(!window.provider(QStringLiteral("tvmaze"))); // Code changes invalidate system approval too.
        window.setPackageTrusted(QStringLiteral("tvmaze"), true);
        window.loadPlugins();
        QVERIFY(window.provider(QStringLiteral("tvmaze")));
        window.setPackageTrusted(QStringLiteral("tvmaze"), false);
        window.loadPlugins();
        QVERIFY(!window.provider(QStringLiteral("tvmaze")));
        QVERIFY(!window.fallbackProvider(QStringLiteral("tvmaze")));
        MainWindow restarted;
        QVERIFY(!restarted.provider(QStringLiteral("tvmaze")));
    }

    void testApprovalReviewCancellationAndFailureStates()
    {
        QTemporaryDir temp;
        const QString root = temp.path() + QStringLiteral("/tvmaze");
        QVERIFY(QDir().mkpath(root));
        const QString source = QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze");
        QVERIFY(QFile::copy(source + QStringLiteral("/provider.json"), root + QStringLiteral("/provider.json")));
        QVERIFY(QFile::copy(source + QStringLiteral("/main.mjs"), root + QStringLiteral("/main.mjs")));
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Providers"));
        group.writeEntry(QStringLiteral("tvmaze_Enabled"), true);
        group.sync();
        MainWindow window;
        window.loadPlugins({temp.path()});
        QVERIFY(!window.provider(QStringLiteral("tvmaze")));
        SettingsDialog settings(window.providers(), nullptr, nullptr, window.packages());
        auto *list = settings.findChild<QListWidget *>(QStringLiteral("scriptedPackages"));
        QVERIFY(list);
        int row = -1;
        for (int i = 0; i < window.packages().size(); ++i)
            if (window.packages()[i].package.id() == QLatin1String("tvmaze")) row = i;
        QVERIFY(row >= 0);
        list->setCurrentRow(row);
        auto *approve = settings.findChild<QPushButton *>(QStringLiteral("approvePackage"));
        QVERIFY(approve && approve->isEnabled());
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Cancel)->click();
        });
        approve->click();
        QVERIFY(!window.isPackageTrusted(QStringLiteral("tvmaze")));
        // Even accepting the review is only a draft until Apply/OK.
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
        });
        approve->click();
        settings.button(QDialogButtonBox::Cancel)->click();
        QVERIFY(!window.isPackageTrusted(QStringLiteral("tvmaze")));
        SettingsDialog approving(window.providers(), nullptr, nullptr, window.packages());
        auto *approvalList = approving.findChild<QListWidget *>(QStringLiteral("scriptedPackages"));
        approvalList->setCurrentRow(row);
        QTimer::singleShot(0, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click();
        });
        approving.findChild<QPushButton *>(QStringLiteral("approvePackage"))->click();
        approving.button(QDialogButtonBox::Apply)->click();
        QVERIFY(window.isPackageTrusted(QStringLiteral("tvmaze")));
        window.loadPlugins({temp.path()});
        QVERIFY(window.provider(QStringLiteral("tvmaze")));
        SettingsDialog revoking(window.providers(), nullptr, nullptr, window.packages());
        revoking.findChild<QListWidget *>(QStringLiteral("scriptedPackages"))->setCurrentRow(row);
        revoking.findChild<QPushButton *>(QStringLiteral("revokePackage"))->click();
        revoking.button(QDialogButtonBox::Apply)->click();
        window.loadPlugins({temp.path()});
        QVERIFY(!window.provider(QStringLiteral("tvmaze")));
        QVERIFY(!window.isPackageTrusted(QStringLiteral("tvmaze")));

        for (const QByteArray &script : {QByteArray("export function search( {"),
                                       QByteArray("throw new Error('broken module'); export function search() {}")}) {
            QFile entry(root + QStringLiteral("/main.mjs"));
            QVERIFY(entry.open(QIODevice::WriteOnly));
            entry.write(script);
            entry.close();
            window.loadPlugins({temp.path()});
            window.setPackageTrusted(QStringLiteral("tvmaze"), true);
            window.loadPlugins({temp.path()});
            QVERIFY(!window.provider(QStringLiteral("tvmaze")));
            bool failed = false;
            for (const auto &record : window.packages())
                if (record.package.id() == QLatin1String("tvmaze")) failed = record.status.startsWith(QStringLiteral("Load failed:"));
            QVERIFY(failed);
        }
        QVERIFY(QFile::remove(root + QStringLiteral("/main.mjs")));
        window.loadPlugins({temp.path()});
        bool missing = false;
        for (const auto &record : window.packages())
            if (record.package.id() == QLatin1String("tvmaze")) missing = !record.package.isValid() && record.status.contains(QStringLiteral("does not exist"));
        QVERIFY(missing);
    }

    void testSettingsPreserveUnaffectedSearches_data()
    {
        QTest::addColumn<int>("action");
        QTest::newRow("cancel") << 0;
        QTest::newRow("ok-without-changes") << 1;
        QTest::newRow("apply-general-then-cancel") << 2;
        QTest::newRow("disable-one-package") << 3;
    }

    void testSettingsPreserveUnaffectedSearches()
    {
        QFETCH(int, action);
        QTemporaryDir root;
        for (const QString &id : {QStringLiteral("tvmaze"), QStringLiteral("other")}) {
            const QString destination = root.path() + QLatin1Char('/') + id;
            QVERIFY(QDir().mkpath(destination));
            QFile manifest(QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze/provider.json"));
            QVERIFY(manifest.open(QIODevice::ReadOnly));
            auto object = QJsonDocument::fromJson(manifest.readAll()).object();
            object.insert(QStringLiteral("id"), id);
            QFile output(destination + QStringLiteral("/provider.json"));
            QVERIFY(output.open(QIODevice::WriteOnly));
            output.write(QJsonDocument(object).toJson());
            output.close();
            QVERIFY(QFile::copy(QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze/main.mjs"), destination + QStringLiteral("/main.mjs")));
        }
        MockTvMazeNam network(m_searchFixture, m_showFixture);
        network.suppress = true;
        MainWindow window;
        window.setNetworkAccessManagerFactory([&network](QObject *) { return &network; });
        window.loadPlugins({root.path()});
        for (const QString &id : {QStringLiteral("tvmaze"), QStringLiteral("other")}) window.setPackageTrusted(id, true);
        window.loadPlugins({root.path()});
        auto clone = [&window](const QString &id) {
            auto *adapter = qobject_cast<ProviderApiV1::ProviderV1ToLegacyAdapter *>(window.provider(id)->clone(&window));
            return qobject_cast<JsProvider *>(adapter->underlyingProvider());
        };
        QVERIFY(window.provider(QStringLiteral("tvmaze")));
        QVERIFY(window.provider(QStringLiteral("other")));
        auto *tvmaze = clone(QStringLiteral("tvmaze"));
        auto *other = clone(QStringLiteral("other"));
        QVERIFY(tvmaze && other);
        QSignalSpy tvmazeReady(tvmaze, &ProviderApiV1::ProviderV1::pageReady);
        QSignalSpy otherReady(other, &ProviderApiV1::ProviderV1::pageReady);
        QSignalSpy reloads(&window, &MainWindow::packageReloaded);
        tvmaze->search(QStringLiteral("pending TVMaze"));
        other->search(QStringLiteral("pending other"));
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<SettingsDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            if (action == 3) {
                auto *list = dialog->findChild<QListWidget *>(QStringLiteral("scriptedPackages"));
                for (int row = 0; row < window.packages().size(); ++row)
                    if (window.packages()[row].package.id() == QLatin1String("tvmaze")) list->item(row)->setCheckState(Qt::Unchecked);
                dialog->button(QDialogButtonBox::Apply)->click();
            } else if (action == 2) {
                auto *setting = dialog->findChild<QCheckBox *>(QStringLiteral("showRecentActivity"));
                QVERIFY(setting);
                setting->setChecked(!setting->isChecked());
                dialog->button(QDialogButtonBox::Apply)->click();
                dialog->button(QDialogButtonBox::Cancel)->click();
            } else dialog->button(action == 0 ? QDialogButtonBox::Cancel : QDialogButtonBox::Ok)->click();
        });
        QVERIFY(QMetaObject::invokeMethod(&window, "showSettingsDialog", Qt::DirectConnection));
        QCOMPARE(reloads.count(), action == 3 ? 1 : 0);
        if (action == 3) QCOMPARE(reloads.first().first().toString(), QStringLiteral("tvmaze"));
        QCOMPARE(tvmaze->isLoaded(), action != 3);
        QVERIFY(other->isLoaded());
        QCOMPARE(network.aborted, action == 3 ? 1 : 0);
        const auto pending = network.replies;
        for (const auto &reply : pending) if (reply && !reply->isFinished()) reply->complete();
        QTRY_COMPARE(otherReady.count(), 1);
        if (action != 3) QTRY_COMPARE(tvmazeReady.count(), 1);
        // A preserved tab can still load details after the dialog is closed.
        network.suppress = false;
        QSignalSpy details(other, &ProviderApiV1::ProviderV1::detailsReady);
        other->getDetails(QStringLiteral("139"));
        QTRY_COMPARE(details.count(), 1);
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

        // 4. Enabled is independent of approval; legacy ID trust cannot activate code.
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

        QVERIFY(window.provider(QStringLiteral("tvmaze")) == nullptr);
        QVERIFY(!window.packages().isEmpty());
        window.setPackageTrusted(QStringLiteral("tvmaze"), true);
        window.loadPlugins({xdgDataHome + QStringLiteral("/klookup/providers.d")}, {});

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
        // Disabled packages remain selectable after restart through normal Settings.
        MainWindow restarted;
        QVERIFY(restarted.provider(QStringLiteral("tvmaze")) == nullptr);
        SettingsDialog settings(restarted.providers(), nullptr, nullptr, restarted.packages());
        auto *list = settings.findChild<QListWidget *>(QStringLiteral("scriptedPackages"));
        QVERIFY(list);
        QVERIFY(list->count() > 0);
        bool found = false;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->text().startsWith(QStringLiteral("TVMaze"))) {
                QCOMPARE(list->item(row)->checkState(), Qt::Unchecked);
                list->item(row)->setCheckState(Qt::Checked);
                found = true;
            }
        }
        QVERIFY(found);
        settings.button(QDialogButtonBox::Apply)->click();
        restarted.setNetworkAccessManagerFactory([mockNam](QObject *) { return mockNam; });
        restarted.loadPlugins();
        QVERIFY(restarted.provider(QStringLiteral("tvmaze")));
    }
};

QTEST_MAIN(TestTvMazeScripted)
#include "test_tvmaze_scripted.moc"
