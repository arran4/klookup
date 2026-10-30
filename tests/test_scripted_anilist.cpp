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
    int status = 200;
    bool delayed = false;
    QList<QNetworkRequest> requests;
    QList<Operation> operations;
    QList<QByteArray> bodies;
    QPointer<ScriptFixtureReply> last;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *body) override {
        operations.append(operation); bodies.append(body ? body->readAll() : QByteArray());
        requests.append(request);
        last = new ScriptFixtureReply(request, response, status, this, delayed);
        return last;
    }
};

class TestScriptedAniList : public QObject {
    Q_OBJECT
    QJsonObject fixture;
    ProviderPackage package;
    QString packagePath;
private slots:
    void initTestCase() {
        packagePath = qEnvironmentVariableIsSet("KLOOKUP_STAGED_INSTALL") ? QCoreApplication::applicationDirPath() + QStringLiteral("/../share/klookup/providers.d/anilist") : QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/anilist");
        QFile file(packagePath + QStringLiteral("/diagnostics.json"));
        QVERIFY(file.open(QIODevice::ReadOnly)); fixture = QJsonDocument::fromJson(file.readAll()).object();
        for (const auto &candidate : ProviderPackageDiscovery::discoverPackages({QFileInfo(packagePath).absolutePath()}))
            if (candidate.id() == QLatin1String("anilist")) package = candidate;
        QVERIFY(package.isValid()); package.setTrusted(true); package.setEnabled(true);
    }
    void mapping_data() {
        QTest::addColumn<QString>("key"); QTest::addColumn<int>("status"); QTest::addColumn<bool>("success"); QTest::addColumn<int>("count");
        QTest::newRow("valid") << QStringLiteral("valid") << 200 << true << 1;
        QTest::newRow("empty") << QStringLiteral("empty") << 200 << true << 0;
        QTest::newRow("partial") << QStringLiteral("partial") << 200 << true << 1;
        QTest::newRow("wrong-shape") << QStringLiteral("invalid") << 200 << false << 0;
        QTest::newRow("graphql-errors-in-200") << QStringLiteral("graphql_error") << 200 << false << 0;
        QTest::newRow("null-media") << QStringLiteral("null_media") << 200 << false << 0;
        QTest::newRow("graphql-rate-in-200") << QStringLiteral("graphql_rate") << 200 << false << 0;
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
        QCOMPARE(network.requests.first().url().host(), QStringLiteral("graphql.anilist.co"));
        QCOMPARE(network.operations.first(), QNetworkAccessManager::PostOperation);
        QCOMPARE(network.requests.first().header(QNetworkRequest::ContentTypeHeader).toString(), QStringLiteral("application/json"));
        const auto body = QJsonDocument::fromJson(network.bodies.first()).object();
        QCOMPARE(body.value(QStringLiteral("variables")).toObject().value(QStringLiteral("search")).toString(), query);
        QVERIFY(body.value(QStringLiteral("query")).toString().contains(QStringLiteral("type: ANIME")));
        QVERIFY(!body.value(QStringLiteral("query")).toString().contains(query));
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
                QCOMPARE(result.type, QStringLiteral("anime"));
                QCOMPARE(result.additionalData.value(QStringLiteral("score")).toInt(), 82);
                QCOMPARE(result.additionalData.value(QStringLiteral("episodes")).toInt(), 12);
            }
            provider.search(query); QTRY_COMPARE(ready.count(), 2);
            QCOMPARE(network.requests.size(), 1); // bounded user-query cache
        } else if (status == 429) {
            QVERIFY(failed.first()[2].toString().contains(QStringLiteral("429")));
            QVERIFY(failed.first()[2].toString().contains(QStringLiteral("3 seconds")));
            provider.search(query); QTRY_COMPARE(failed.count(), 2);
            QCOMPARE(network.requests.size(), 1); // respect cooldown, no retry loop
        }
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
        settings.writeEntry(QStringLiteral("anilist_Enabled"), true); settings.sync();
        MainWindow window;
        window.setNetworkAccessManagerFactory([this](QObject *parent) {
            auto *network = new ScriptFixtureNetwork(parent);
            network->response = QJsonDocument(fixture.value(QStringLiteral("valid")).toObject()).toJson();
            return network;
        });
        const QStringList packages{QFileInfo(packagePath).absolutePath()};
        if (qEnvironmentVariableIsSet("KLOOKUP_STAGED_INSTALL")) {
            window.loadPlugins();
            QVERIFY(window.provider(QStringLiteral("anilist"))); // Fresh protected installed package, no seeded approval.
        }
        package.saveApproval(settings, false); settings.sync();
        window.loadPlugins(packages, {QStringLiteral(OBSOLETE_DIR)});
        QVERIFY(!window.provider(QStringLiteral("anilist")));
        bool visible = false;
        for (const auto &record : window.packages()) if (record.package.id() == QLatin1String("anilist")) visible = true;
        QVERIFY(visible);
        package.saveApproval(settings, true); settings.sync(); window.loadPlugins(packages, {});
        auto *provider = window.provider(QStringLiteral("anilist")); QVERIFY(provider);
        QSignalSpy ready(provider, &SearchProvider::searchCompleted);
        provider->search(QStringLiteral("UI fixture")); QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(qvariant_cast<QList<SearchResult>>(ready.first()[1]).first().title, QStringLiteral("Fixture 雪"));
        SearchTab tab(QStringLiteral("tab fixture")); tab.startProvider(provider->clone(&tab));
        auto *tree = tab.findChild<QTreeWidget *>(); QVERIFY(tree);
        QTRY_VERIFY(tree->topLevelItemCount() > 0 && tree->topLevelItem(0)->childCount() > 0 && tree->topLevelItem(0)->child(0)->text(0) == QStringLiteral("Fixture 雪"));
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("Fixture 雪"));
        settings.writeEntry(QStringLiteral("anilist_Enabled"), false); settings.sync(); window.loadPlugins(packages, {});
        QVERIFY(!window.provider(QStringLiteral("anilist")));
        settings.writeEntry(QStringLiteral("anilist_Enabled"), true); settings.sync(); window.loadPlugins(packages, {});
        QVERIFY(window.provider(QStringLiteral("anilist")));
    }
};
int main(int argc, char **argv) {
    QTemporaryDir config; qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    if (!qEnvironmentVariableIsSet("KLOOKUP_STAGED_INSTALL")) {
        qputenv("XDG_DATA_HOME", config.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_DATA_DIRS", config.filePath(QStringLiteral("system-data")).toUtf8());
    }
    QApplication app(argc, argv);
    TestScriptedAniList test; return QTest::qExec(&test, argc, argv);
}
#include "test_scripted_anilist.moc"
