// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "jsprovider.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <cstring>
#include <utility>

class NovelUpdatesFixtureReply : public QNetworkReply {
    Q_OBJECT
public:
    NovelUpdatesFixtureReply(const QNetworkRequest &request, QByteArray data, int status, QObject *parent)
        : QNetworkReply(parent), body(std::move(data)) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setRawHeader("Retry-After", "3");
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, &NovelUpdatesFixtureReply::finish);
    }
    void abort() override {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        finish();
    }
    qint64 bytesAvailable() const override { return body.size() - offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override {
        const auto count = qMin<qint64>(max, body.size() - offset);
        if (count <= 0) return -1;
        std::memcpy(data, body.constData() + offset, count);
        offset += count;
        return count;
    }
private:
    void finish() {
        if (isFinished()) return;
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    QByteArray body;
    qint64 offset = 0;
};

class NovelUpdatesFixtureNetwork : public QNetworkAccessManager {
    Q_OBJECT
public:
    using QNetworkAccessManager::QNetworkAccessManager;
    QByteArray response;
    QByteArray posted;
    QList<QNetworkRequest> requests;
    Operation operation = UnknownOperation;
    int status = 200;
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override {
        operation = op;
        requests.append(request);
        posted = outgoingData ? outgoingData->readAll() : QByteArray();
        return new NovelUpdatesFixtureReply(request, response, status, this);
    }
};

class TestScriptedNovelUpdates : public QObject {
    Q_OBJECT
    QJsonObject fixture;
    ProviderPackage package;
private slots:
    void initTestCase() {
        const QString packagePath = QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/novelupdates");
        QFile file(packagePath + QStringLiteral("/diagnostics.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        fixture = QJsonDocument::fromJson(file.readAll()).object();
        for (const auto &candidate : ProviderPackageDiscovery::discoverPackages({QFileInfo(packagePath).absolutePath()}))
            if (candidate.id() == QLatin1String("novelupdates")) package = candidate;
        QVERIFY(package.isValid());
        package.setTrusted(true);
        package.setEnabled(true);
    }
    void searchResponses_data() {
        QTest::addColumn<QString>("fixtureKey");
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("success");
        QTest::addColumn<int>("results");
        QTest::newRow("valid") << QStringLiteral("valid") << 200 << true << 1;
        QTest::newRow("partial-and-hostile-link") << QStringLiteral("partial") << 200 << true << 1;
        QTest::newRow("empty") << QStringLiteral("empty") << 200 << true << 0;
        QTest::newRow("challenge") << QStringLiteral("invalid") << 200 << false << 0;
        QTest::newRow("unexpected-markup") << QStringLiteral("unexpected") << 200 << false << 0;
        QTest::newRow("access-denied") << QStringLiteral("valid") << 403 << false << 0;
        QTest::newRow("upstream-error") << QStringLiteral("valid") << 503 << false << 0;
        QTest::newRow("rate-limited") << QStringLiteral("valid") << 429 << false << 0;
    }
    void searchResponses() {
        QFETCH(QString, fixtureKey);
        QFETCH(int, status);
        QFETCH(bool, success);
        QFETCH(int, results);
        NovelUpdatesFixtureNetwork network;
        network.response = fixtureKey == QLatin1String("unexpected") ? QByteArray("<html><body>layout changed</body></html>")
                          : fixture.value(fixtureKey).toString().toUtf8();
        network.status = status;
        ProviderHost::ProviderHostService host(package, nullptr, &network);
        JsProvider provider(package, nullptr, &host);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QVERIFY(!provider.capabilities().pagination);
        const QString query = fixture.value(QStringLiteral("query")).toString();
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        QSignalSpy failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        provider.search(query, 42);
        QTRY_COMPARE(ready.size() + failed.size(), 1);
        QCOMPARE(ready.size(), success ? 1 : 0);
        QCOMPARE(failed.size(), success ? 0 : 1);
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(network.operation, QNetworkAccessManager::PostOperation);
        QCOMPARE(network.requests.first().url().host(), QStringLiteral("www.novelupdates.com"));
        QCOMPARE(network.requests.first().url().path(), QStringLiteral("/wp-admin/admin-ajax.php"));
        QUrlQuery form(QString::fromUtf8(network.posted));
        QCOMPARE(form.queryItemValue(QStringLiteral("action")), QStringLiteral("nd_ajaxsearchmain"));
        QCOMPARE(form.queryItemValue(QStringLiteral("strType")), QStringLiteral("desktop"));
        QCOMPARE(form.queryItemValue(QStringLiteral("strSearchType")), QStringLiteral("series"));
        QCOMPARE(form.queryItemValue(QStringLiteral("strOne"), QUrl::FullyDecoded), query);
        if (success) {
            const auto page = ready.first().at(1).value<SearchPage>();
            QCOMPARE(page.results.size(), results);
            QVERIFY(page.nextToken.isEmpty());
            if (results) {
                const auto expected = fixture.value(QStringLiteral("expected")).toObject();
                QCOMPARE(page.results.first().id, expected.value(QStringLiteral("id")).toString());
                QCOMPARE(page.results.first().title, expected.value(QStringLiteral("title")).toString());
                QCOMPARE(page.results.first().url, expected.value(QStringLiteral("url")).toString());
                QCOMPARE(page.results.first().type, QStringLiteral("book"));
            }
            provider.search(query, 43);
            QTRY_COMPARE(ready.size(), 2);
            QCOMPARE(network.requests.size(), 1); // Query cache prevents duplicate requests.
        } else if (status == 429) {
            QVERIFY(failed.first().at(2).toString().contains(QStringLiteral("429")));
            provider.search(query, 43);
            QTRY_COMPARE(failed.size(), 2);
            QCOMPARE(network.requests.size(), 1); // No retries during cooldown.
            QTest::qWait(3100);
        } else if (status == 403) {
            QVERIFY(failed.first().at(2).toString().contains(QStringLiteral("403")));
        }
    }
};

QTEST_GUILESS_MAIN(TestScriptedNovelUpdates)
#include "test_scripted_novelupdates.moc"
