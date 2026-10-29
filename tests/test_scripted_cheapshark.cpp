// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "jsprovider.h"
#include "mainwindow.h"
#include "searchtab.h"
#include "settingsdialog.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QApplication>
#include <QFile>
#include <QUrlQuery>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstring>

class ScriptFixtureReply : public QNetworkReply {
    Q_OBJECT
public:
    QByteArray payload;
    qint64 offset = 0;
    bool aborted = false;
    ScriptFixtureReply(const QNetworkRequest &request, const QByteArray &data, int status, QObject *parent, bool delayed = false) : QNetworkReply(parent), payload(data) {
        setRequest(request); setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (status == 0) setError(QNetworkReply::ConnectionRefusedError, QStringLiteral("Fixture connection refused"));
        setRawHeader("Retry-After", "3");
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (!delayed) QTimer::singleShot(0, this, &ScriptFixtureReply::finish);
    }
    void abort() override { aborted = true; setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled")); finish(); }
    void finish() { if (isFinished()) return; setFinished(true); emit readyRead(); emit finished(); }
    qint64 bytesAvailable() const override { return payload.size() - offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override {
        const auto count = qMin<qint64>(max, payload.size() - offset);
        if (count <= 0) return -1;
        std::memcpy(data, payload.constData() + offset, count); offset += count; return count;
    }
};
class ScriptFixtureNetwork : public QNetworkAccessManager {
    Q_OBJECT
public:
    using QNetworkAccessManager::QNetworkAccessManager;
    QByteArray response;
    QByteArray storeResponse;
    int storeStatus = 200;
    QByteArray detailResponse;
    int status = 200;
    bool delayed = false;
    QList<QNetworkRequest> requests;
    QPointer<ScriptFixtureReply> last;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        requests.append(request);
        const bool isStores = request.url().path().endsWith(QStringLiteral("/stores"));
        const bool isDetails = QUrlQuery(request.url()).hasQueryItem(QStringLiteral("id"));
        last = new ScriptFixtureReply(request, isStores ? storeResponse : isDetails ? detailResponse : response, isStores ? storeStatus : status, this, isStores ? false : delayed);
        return last;
    }
};

class TestScriptedCheapShark : public QObject {
    Q_OBJECT
    QJsonObject fixture;
    ProviderPackage package;
    QString packagePath;
private slots:
    void initTestCase() {
        packagePath = qEnvironmentVariableIsSet("KLOOKUP_STAGED_INSTALL") ? QCoreApplication::applicationDirPath() + QStringLiteral("/../share/klookup/providers.d/cheapshark") : QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/cheapshark");
        QFile file(packagePath + QStringLiteral("/diagnostics.json"));
        QVERIFY(file.open(QIODevice::ReadOnly)); fixture = QJsonDocument::fromJson(file.readAll()).object();
        for (const auto &candidate : ProviderPackageDiscovery::discoverPackages({QFileInfo(packagePath).absolutePath()}))
            if (candidate.id() == QLatin1String("cheapshark")) package = candidate;
        QVERIFY(package.isValid()); package.setTrusted(true); package.setEnabled(true);
    }
    void mapping_data() {
        QTest::addColumn<QString>("key"); QTest::addColumn<int>("status"); QTest::addColumn<bool>("success"); QTest::addColumn<int>("count");
        QTest::newRow("valid") << QStringLiteral("valid") << 200 << true << 1;
        QTest::newRow("empty") << QStringLiteral("empty") << 200 << true << 0;
        QTest::newRow("partial") << QStringLiteral("partial") << 200 << true << 1;
        QTest::newRow("wrong-shape") << QStringLiteral("invalid") << 200 << false << 0;
        QTest::newRow("malformed") << QStringLiteral("malformed") << 200 << false << 0;
        QTest::newRow("transport-error") << QStringLiteral("invalid") << 0 << false << 0;
        QTest::newRow("http-error") << QStringLiteral("invalid") << 503 << false << 0;
        QTest::newRow("rate-limit") << QStringLiteral("invalid") << 429 << false << 0;
    }
    void mapping() {
        QFETCH(QString, key); QFETCH(int, status); QFETCH(bool, success); QFETCH(int, count);
        ScriptFixtureNetwork network;
        network.storeResponse = QJsonDocument(fixture.value(QStringLiteral("stores")).toArray()).toJson();
        network.response = key == QLatin1String("malformed") ? QByteArray("{") : key == QLatin1String("invalid") ? QJsonDocument(fixture.value(key).toObject()).toJson() : QJsonDocument(fixture.value(key).toArray()).toJson();
        network.status = status;
        ProviderHost::ProviderHostService host(package, nullptr, &network);
        JsProvider provider(package, nullptr, &host); QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady), failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        const auto query = fixture.value(QStringLiteral("query")).toString();
        provider.search(query, 42);
        QTRY_COMPARE(ready.count() + failed.count(), 1);
        QVERIFY2(ready.count() == (success ? 1 : 0), failed.isEmpty() ? "Unexpected result" : qPrintable(failed.first()[2].toString()));
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests.last().url().host(), QStringLiteral("www.cheapshark.com"));
        QUrlQuery parameters(network.requests.last().url());
        QCOMPARE(parameters.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded), query);
        QCOMPARE(parameters.queryItemValue(QStringLiteral("pageNumber")), QStringLiteral("0"));
        QCOMPARE(parameters.queryItemValue(QStringLiteral("pageSize")), QStringLiteral("10"));
        QVERIFY(network.requests.last().rawHeader("User-Agent").startsWith("KLookup/"));
        if (success) {
            const auto page = qvariant_cast<SearchPage>(ready.first()[1]);
            QCOMPARE(page.results.size(), count);
            QVERIFY(page.nextToken.isEmpty());
            if (count) {
                const auto expected = fixture.value(QStringLiteral("expected")).toObject();
                const auto result = page.results.first();
                QCOMPARE(result.id, expected.value(QStringLiteral("id")).toString());
                QCOMPARE(result.title, expected.value(QStringLiteral("title")).toString());
                QCOMPARE(result.url, expected.value(QStringLiteral("url")).toString());
                QCOMPARE(result.type, QStringLiteral("game"));
                QCOMPARE(result.additionalData.value(QStringLiteral("currency")).toString(), QStringLiteral("USD"));
                QCOMPARE(result.additionalData.value(QStringLiteral("sale_price_usd")).toDouble(), 8.99);
            }
            provider.search(query); QTRY_COMPARE(ready.count(), 2);
            QCOMPARE(network.requests.size(), 2); // bounded user-query cache
        } else if (status == 429) {
            QVERIFY(failed.first()[2].toString().contains(QStringLiteral("429")));
            QVERIFY(failed.first()[2].toString().contains(QStringLiteral("3 seconds")));
            provider.search(query); QTRY_COMPARE(failed.count(), 2);
            QCOMPARE(network.requests.size(), 2); // respect cooldown, no retry loop
        }
    }
    void lazyDetailsAndReferenceCache() {
        ScriptFixtureNetwork network;
        network.storeResponse = QJsonDocument(fixture.value(QStringLiteral("stores")).toArray()).toJson();
        network.response = QJsonDocument(fixture.value(QStringLiteral("valid")).toArray()).toJson();
        network.detailResponse = QJsonDocument(fixture.value(QStringLiteral("details")).toObject()).toJson();
        ProviderHost::ProviderHostService host(package, nullptr, &network);
        JsProvider provider(package, nullptr, &host);
        QSignalSpy pages(&provider, &ProviderApiV1::ProviderV1::pageReady), details(&provider, &ProviderApiV1::ProviderV1::detailsReady), failed(&provider, &ProviderApiV1::ProviderV1::detailsFailed);
        QCOMPARE(network.requests.size(), 0);
        provider.search(QStringLiteral("game")); QTRY_COMPARE(pages.count(), 1);
        QCOMPARE(network.requests.size(), 2);
        const auto id = fixture.value(QStringLiteral("expected")).toObject().value(QStringLiteral("id")).toString();
        provider.getDetails(id); QTRY_COMPARE(details.count(), 1);
        QCOMPARE(network.requests.size(), 3); // fulfilled store reference data reused
        QCOMPARE(network.requests.last().url().toEncoded(), QByteArray("https://www.cheapshark.com/api/1.0/deals?id=abc%2Bdef%3D%3D"));
        const auto result = details.first()[2].value<SearchDetails>();
        QCOMPARE(result.result.url, fixture.value(QStringLiteral("expected")).toObject().value(QStringLiteral("url")).toString());
        QVERIFY(!result.metadata.sections.isEmpty());
        network.detailResponse = QByteArray("{}"); provider.getDetails(id); QTRY_COMPARE(failed.count(), 1);
    }
    void cancellationAbortsAndSuppressesLateResults() {
        ScriptFixtureNetwork network;
        network.storeResponse = QJsonDocument(fixture.value(QStringLiteral("stores")).toArray()).toJson(); network.response = QJsonDocument(fixture.value(QStringLiteral("valid")).toArray()).toJson(); network.delayed = true;
        ProviderHost::ProviderHostService host(package, nullptr, &network);
        JsProvider provider(package, nullptr, &host);
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady), failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        provider.search(QStringLiteral("cancel"), 17);
        QTRY_VERIFY(network.last);
        auto *reply = network.last.data(); provider.cancel(17);
        QVERIFY(reply->aborted);
        QCoreApplication::processEvents();
        QCOMPARE(ready.count(), 0); QCOMPARE(failed.count(), 0); QCOMPARE(provider.pendingFetchCount(), 0);
        network.delayed = false;
        provider.search(QStringLiteral("after cancellation"), 18);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first()[0].toULongLong(), 18ULL);
        QCOMPARE(failed.count(), 0);
    }
    void optionalStoresFailSafelyAndRateLimitsPropagate() {
        ScriptFixtureNetwork network;
        network.storeResponse = QByteArray("{}");
        network.response = QJsonDocument(fixture.value(QStringLiteral("valid")).toArray()).toJson();
        ProviderHost::ProviderHostService host(package, nullptr, &network);
        JsProvider provider(package, nullptr, &host);
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady), failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        provider.search(QStringLiteral("store unavailable")); QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first()[1].value<SearchPage>().results.first().additionalData.value(QStringLiteral("store")).toString(), QStringLiteral("Store ID 1"));
        network.storeStatus = 429; provider.search(QStringLiteral("rate limited store")); QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.first()[2].toString().contains(QStringLiteral("429")));
        const auto requests = network.requests.size(); provider.search(QStringLiteral("cooldown")); QTRY_COMPARE(failed.count(), 2);
        QCOMPARE(network.requests.size(), requests);
    }
    void normalDiscoveryApprovalAndEnablement() {
        auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        KConfigGroup settings(config, QStringLiteral("Providers"));
        settings.writeEntry(QStringLiteral("cheapshark_Enabled"), true); settings.sync();
        MainWindow window;
        window.setNetworkAccessManagerFactory([this](QObject *parent) {
            auto *network = new ScriptFixtureNetwork(parent);
            network->detailResponse = QJsonDocument(fixture.value(QStringLiteral("details")).toObject()).toJson(); network->storeResponse = QJsonDocument(fixture.value(QStringLiteral("stores")).toArray()).toJson(); network->response = QJsonDocument(fixture.value(QStringLiteral("valid")).toArray()).toJson();
            return network;
        });
        const QStringList packages{QFileInfo(packagePath).absolutePath()};
        if (qEnvironmentVariableIsSet("KLOOKUP_STAGED_INSTALL")) {
            window.loadPlugins();
            QVERIFY(window.provider(QStringLiteral("cheapshark"))); // Fresh protected installed package, no seeded approval.
        }
        package.saveApproval(settings, false); settings.sync();
        window.loadPlugins(packages, {});
        QVERIFY(!window.provider(QStringLiteral("cheapshark")));
        bool visible = false;
        for (const auto &record : window.packages()) if (record.package.id() == QLatin1String("cheapshark")) visible = true;
        QVERIFY(visible);
        package.saveApproval(settings, true); settings.sync(); window.loadPlugins(packages, {});
        auto *provider = window.provider(QStringLiteral("cheapshark")); QVERIFY(provider);
        QSignalSpy ready(provider, &SearchProvider::searchCompleted);
        provider->search(QStringLiteral("UI fixture")); QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(qvariant_cast<QList<SearchResult>>(ready.first()[1]).first().title, QStringLiteral("Fixture 雪"));
        SearchTab tab(QStringLiteral("tab fixture")); tab.startProvider(provider->clone(&tab));
        auto *tree = tab.findChild<QTreeWidget *>(); QVERIFY(tree);
        QTRY_VERIFY(tree->topLevelItemCount() > 0 && tree->topLevelItem(0)->childCount() > 0 && tree->topLevelItem(0)->child(0)->text(0) == QStringLiteral("Fixture 雪"));
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("Fixture 雪"));
        settings.writeEntry(QStringLiteral("cheapshark_Enabled"), false); settings.sync(); window.loadPlugins(packages, {});
        QVERIFY(!window.provider(QStringLiteral("cheapshark")));
        settings.writeEntry(QStringLiteral("cheapshark_Enabled"), true); settings.sync(); window.loadPlugins(packages, {});
        QVERIFY(window.provider(QStringLiteral("cheapshark")));
    }
};
int main(int argc, char **argv) {
    QTemporaryDir config; qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    if (!qEnvironmentVariableIsSet("KLOOKUP_STAGED_INSTALL")) {
        qputenv("XDG_DATA_HOME", config.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_DATA_DIRS", config.filePath(QStringLiteral("system-data")).toUtf8());
    }
    QApplication app(argc, argv);
    TestScriptedCheapShark test; return QTest::qExec(&test, argc, argv);
}
#include "test_scripted_cheapshark.moc"
