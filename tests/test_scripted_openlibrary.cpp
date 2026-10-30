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
#include <QElapsedTimer>
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
    int status = 200;
    bool delayed = false;
    QList<QNetworkRequest> requests;
    QList<qint64> requestTimes;
    QPointer<ScriptFixtureReply> last;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        requests.append(request);
        QElapsedTimer clock; clock.start(); requestTimes.append(clock.msecsSinceReference());
        last = new ScriptFixtureReply(request, response, status, this, delayed);
        return last;
    }
};

class TestScriptedOpenLibrary : public QObject {
    Q_OBJECT
    QJsonObject fixture;
    ProviderPackage package;
    QString packagePath;
private slots:
    void initTestCase() {
        packagePath = qEnvironmentVariableIsSet("KLOOKUP_STAGED_INSTALL") ? QCoreApplication::applicationDirPath() + QStringLiteral("/../share/klookup/providers.d/openlibrary") : QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/openlibrary");
        QFile file(packagePath + QStringLiteral("/diagnostics.json"));
        QVERIFY(file.open(QIODevice::ReadOnly)); fixture = QJsonDocument::fromJson(file.readAll()).object();
        for (const auto &candidate : ProviderPackageDiscovery::discoverPackages({QFileInfo(packagePath).absolutePath()}))
            if (candidate.id() == QLatin1String("openlibrary")) package = candidate;
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
        network.response = key == QLatin1String("malformed") ? QByteArray("{") : QJsonDocument(fixture.value(key).toObject()).toJson();
        network.status = status;
        ProviderHost::ProviderHostService host(package, nullptr, &network);
        JsProvider provider(package, nullptr, &host); QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady), failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        const auto query = fixture.value(QStringLiteral("query")).toString();
        provider.search(query, 42);
        QTRY_COMPARE(ready.count() + failed.count(), 1);
        QVERIFY2(ready.count() == (success ? 1 : 0), failed.isEmpty() ? "Unexpected result" : qPrintable(failed.first()[2].toString()));
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(network.requests.first().url().host(), QStringLiteral("openlibrary.org"));
        QUrlQuery parameters(network.requests.first().url());
        QCOMPARE(parameters.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded), query);
        QCOMPARE(parameters.queryItemValue(QStringLiteral("limit")), QStringLiteral("10"));
        QCOMPARE(parameters.queryItemValue(QStringLiteral("offset")), QStringLiteral("0"));
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
                QCOMPARE(result.type, QStringLiteral("book"));
                QCOMPARE(result.additionalData.value(QStringLiteral("authors")).toStringList(), QStringList{QStringLiteral("Author 雪")});
                QCOMPARE(result.additionalData.value(QStringLiteral("first_publish_year")).toInt(), 1984);
            }
            provider.search(query); QTRY_COMPARE(ready.count(), 2);
            QCOMPARE(network.requests.size(), 1); // bounded user-query cache
        } else if (status == 429) {
            QVERIFY(failed.first()[2].toString().contains(QStringLiteral("429")));
            QVERIFY(failed.first()[2].toString().contains(QStringLiteral("3 seconds")));
            provider.search(query); QTRY_COMPARE(failed.count(), 2);
            QCOMPARE(network.requests.size(), 1); // respect cooldown, no retry loop
            QTest::qWait(3050); // The application-wide origin cooldown survives this instance.
        }
    }
    void independentTabsShareSpacingAndQueuedCancellation() {
        ScriptFixtureNetwork firstNetwork, secondNetwork;
        firstNetwork.response = secondNetwork.response = QJsonDocument(fixture.value(QStringLiteral("empty")).toObject()).toJson();
        ProviderHost::ProviderHostService firstHost(package, nullptr, &firstNetwork), secondHost(package, nullptr, &secondNetwork);
        JsProvider first(package, nullptr, &firstHost), second(package, nullptr, &secondHost);
        QSignalSpy firstReady(&first, &ProviderApiV1::ProviderV1::pageReady), secondReady(&second, &ProviderApiV1::ProviderV1::pageReady);
        first.search(QStringLiteral("first independent tab"));
        second.search(QStringLiteral("second independent tab"));
        QTRY_COMPARE(firstReady.count() + secondReady.count(), 2);
        QCOMPARE(firstNetwork.requests.size(), 1);
        QCOMPARE(secondNetwork.requests.size(), 1);
        QVERIFY(qAbs(secondNetwork.requestTimes.first() - firstNetwork.requestTimes.first()) >= 1000);

        // Cancellation while waiting for the shared slot never starts HTTP.
        const auto cancelled = first.search(QStringLiteral("cancel queued tab"));
        first.cancel(cancelled);
        QTest::qWait(1050);
        QCOMPARE(firstNetwork.requests.size(), 1);
        QCOMPARE(firstReady.count(), 1);
    }
    void retryAfterSurvivesHostDestructionAndNewTab() {
        {
            ScriptFixtureNetwork network;
            network.response = "{}"; network.status = 429;
            ProviderHost::ProviderHostService host(package, nullptr, &network);
            JsProvider provider(package, nullptr, &host);
            QSignalSpy failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
            provider.search(QStringLiteral("rate limited old tab"));
            QTRY_COMPARE(failed.count(), 1);
            QCOMPARE(network.requests.size(), 1);
        }
        ScriptFixtureNetwork network;
        network.response = QJsonDocument(fixture.value(QStringLiteral("empty")).toObject()).toJson();
        ProviderHost::ProviderHostService host(package, nullptr, &network);
        JsProvider provider(package, nullptr, &host);
        QSignalSpy failed(&provider, &ProviderApiV1::ProviderV1::pageFailed), ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        provider.search(QStringLiteral("new tab during cooldown"));
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.first()[2].toString().contains(QStringLiteral("rate limited")));
        QCOMPARE(network.requests.size(), 0);
        QTest::qWait(3050);
        provider.search(QStringLiteral("new tab after cooldown"));
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(network.requests.size(), 1);
    }
    void documentedPaginationUsesOffsets() {
        ScriptFixtureNetwork network;
        auto payload = fixture.value(QStringLiteral("valid")).toObject();
        QJsonArray books;
        for (int i = 0; i < 10; ++i) {
            auto book = payload.value(QStringLiteral("docs")).toArray().first().toObject();
            book.insert(QStringLiteral("key"), QStringLiteral("/works/OL%1W").arg(42 + i)); books.append(book);
        }
        payload.insert(QStringLiteral("docs"), books); payload.insert(QStringLiteral("numFound"), 11);
        network.response = QJsonDocument(payload).toJson();
        ProviderHost::ProviderHostService host(package, nullptr, &network);
        JsProvider provider(package, nullptr, &host);
        QVERIFY(provider.capabilities().pagination);
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady), failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        provider.search(QStringLiteral("pages")); QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first()[1].value<SearchPage>().results.size(), 10);
        QCOMPARE(ready.first()[1].value<SearchPage>().nextToken, QStringLiteral("10"));
        QTest::qWait(1050); // Default upstream quota: one request per second.
        payload.insert(QStringLiteral("docs"), QJsonArray{books.first()});
        payload.insert(QStringLiteral("start"), 10); network.response = QJsonDocument(payload).toJson();
        provider.getPage(QStringLiteral("pages"), QStringLiteral("10")); QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(ready.last()[1].value<SearchPage>().nextToken, QString());
        QUrlQuery parameters(network.requests.last().url());
        QCOMPARE(parameters.queryItemValue(QStringLiteral("offset")), QStringLiteral("10"));
        provider.getPage(QStringLiteral("pages"), QStringLiteral("https://elsewhere.invalid")); QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(network.requests.size(), 2);
    }
    void cancellationAbortsAndSuppressesLateResults() {
        ScriptFixtureNetwork network;
        network.response = QJsonDocument(fixture.value(QStringLiteral("valid")).toObject()).toJson(); network.delayed = true;
        ProviderHost::ProviderHostService host(package, nullptr, &network);
        JsProvider provider(package, nullptr, &host);
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady), failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        provider.search(QStringLiteral("cancel"), 17);
        QTRY_VERIFY(network.last);
        auto *reply = network.last.data(); provider.cancel(17);
        QVERIFY(reply->aborted);
        QCoreApplication::processEvents();
        QCOMPARE(ready.count(), 0); QCOMPARE(failed.count(), 0); QCOMPARE(provider.pendingFetchCount(), 0);
    }
    void normalDiscoveryApprovalAndEnablement() {
        auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        KConfigGroup settings(config, QStringLiteral("Providers"));
        settings.writeEntry(QStringLiteral("openlibrary_Enabled"), true); settings.sync();
        MainWindow window;
        window.setNetworkAccessManagerFactory([this](QObject *parent) {
            auto *network = new ScriptFixtureNetwork(parent);
            network->response = QJsonDocument(fixture.value(QStringLiteral("valid")).toObject()).toJson();
            return network;
        });
        const QStringList packages{QFileInfo(packagePath).absolutePath()};
        if (qEnvironmentVariableIsSet("KLOOKUP_STAGED_INSTALL")) {
            window.loadPlugins();
            QVERIFY(window.provider(QStringLiteral("openlibrary"))); // Fresh protected installed package, no seeded approval.
        }
        package.saveApproval(settings, false); settings.sync();
        window.loadPlugins(packages, {QStringLiteral(OBSOLETE_DIR)});
        QVERIFY(!window.provider(QStringLiteral("openlibrary")));
        bool visible = false;
        for (const auto &record : window.packages()) if (record.package.id() == QLatin1String("openlibrary")) visible = true;
        QVERIFY(visible);
        package.saveApproval(settings, true); settings.sync(); window.loadPlugins(packages, {});
        auto *provider = window.provider(QStringLiteral("openlibrary")); QVERIFY(provider);
        QSignalSpy ready(provider, &SearchProvider::searchCompleted);
        provider->search(QStringLiteral("UI fixture")); QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(qvariant_cast<QList<SearchResult>>(ready.first()[1]).first().title, QStringLiteral("Fixture 雪"));
        SearchTab tab(QStringLiteral("tab fixture")); tab.startProvider(provider->clone(&tab));
        auto *tree = tab.findChild<QTreeWidget *>(); QVERIFY(tree);
        QTRY_VERIFY(tree->topLevelItemCount() > 0 && tree->topLevelItem(0)->childCount() > 0 && tree->topLevelItem(0)->child(0)->text(0) == QStringLiteral("Fixture 雪"));
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("Fixture 雪"));
        settings.writeEntry(QStringLiteral("openlibrary_Enabled"), false); settings.sync(); window.loadPlugins(packages, {});
        QVERIFY(!window.provider(QStringLiteral("openlibrary")));
        settings.writeEntry(QStringLiteral("openlibrary_Enabled"), true); settings.sync(); window.loadPlugins(packages, {});
        QVERIFY(window.provider(QStringLiteral("openlibrary")));
    }
};
int main(int argc, char **argv) {
    QTemporaryDir config; qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    if (!qEnvironmentVariableIsSet("KLOOKUP_STAGED_INSTALL")) {
        qputenv("XDG_DATA_HOME", config.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_DATA_DIRS", config.filePath(QStringLiteral("system-data")).toUtf8());
    }
    QApplication app(argc, argv);
    TestScriptedOpenLibrary test; return QTest::qExec(&test, argc, argv);
}
#include "test_scripted_openlibrary.moc"
