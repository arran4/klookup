#include "../providers/mangabaka/provider_mangabaka.h"
#include "../src/searchtab.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QUrlQuery>
#include <QtTest>

class DummyNetworkReplyMangaBaka : public QNetworkReply
{
    Q_OBJECT
public:
    DummyNetworkReplyMangaBaka(QObject *parent, const QByteArray &data, QNetworkReply::NetworkError err = QNetworkReply::NoError,
                               int httpStatus = 200)
        : QNetworkReply(parent), m_data(data)
    {
        setOpenMode(QIODevice::ReadOnly);
        setError(err, "");
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, httpStatus);
    }

    void finish()
    {
        setFinished(true);
        emit finished();
    }

    void abort() override
    {
        setError(QNetworkReply::OperationCanceledError, "Operation canceled");
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return m_data.size() + QNetworkReply::bytesAvailable();
    }
    bool isSequential() const override
    {
        return true;
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        qint64 len = qMin(maxlen, (qint64)m_data.size());
        memcpy(data, m_data.constData(), len);
        m_data.remove(0, len);
        return len;
    }

private:
    QByteArray m_data;
};

class MockNetworkAccessManagerMangaBaka : public QNetworkAccessManager
{
    Q_OBJECT
public:
    MockNetworkAccessManagerMangaBaka(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    QList<DummyNetworkReplyMangaBaka *> replies;
    QByteArray nextResponse;
    QNetworkReply::NetworkError nextError = QNetworkReply::NoError;
    int nextHttpStatus = 200;
    QUrl lastUrl;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override
    {
        Q_UNUSED(op);
        Q_UNUSED(outgoingData);
        lastUrl = request.url();
        DummyNetworkReplyMangaBaka *reply = new DummyNetworkReplyMangaBaka(this, nextResponse, nextError, nextHttpStatus);
        replies.append(reply);
        QTimer::singleShot(0, reply, &DummyNetworkReplyMangaBaka::finish);
        return reply;
    }
};

class TestableProviderMangaBaka : public ProviderMangaBaka
{
public:
    TestableProviderMangaBaka(QObject *parent = nullptr) : ProviderMangaBaka(parent)
    {
        QNetworkAccessManager *existing = findChild<QNetworkAccessManager *>();
        if (existing) {
            existing->setParent(nullptr);
            existing->deleteLater();
        }
        mockNam = new MockNetworkAccessManagerMangaBaka(this);
        m_nam = mockNam;
    }
    MockNetworkAccessManagerMangaBaka *mockNam;
    QNetworkReply *activeReply() const
    {
        return m_reply;
    }
};

class TestProviderMangaBaka : public QObject
{
    Q_OBJECT
private slots:
    void testPaginationAndLazyDetails();
    void testContinuationFailureAndCancellation();
    void testUntrustedContinuation();
    void testIdentity();
    void testRequestConstructionAndEncoding();
    void testSearchSuccess();
    void testNetworkFailure();
    void testMalformedJsonStructure();
    void testMalformedRecordSkip();
    void testRateLimitFailure();
    void testWrongMetadataTypeSkip();
    void testParseError();
    void testReplaceStale();
};

void TestProviderMangaBaka::testIdentity()
{
    ProviderMangaBaka provider;
    QCOMPARE(provider.id(), QStringLiteral("mangabaka"));
    QCOMPARE(provider.name(), QStringLiteral("MangaBaka"));
}

void TestProviderMangaBaka::testRequestConstructionAndEncoding()
{
    TestableProviderMangaBaka provider;
    provider.search("naruto & sasuke");

    QUrl url = provider.mockNam->lastUrl;
    QCOMPARE(url.scheme(), QStringLiteral("https"));
    QCOMPARE(url.host(), QStringLiteral("api.mangabaka.org"));
    QCOMPARE(url.path(), QStringLiteral("/v2/series/search"));

    QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("q")), QStringLiteral("naruto & sasuke"));
    QCOMPARE(query.queryItemValue(QStringLiteral("limit")), QStringLiteral("10"));
}

void TestProviderMangaBaka::testSearchSuccess()
{
    TestableProviderMangaBaka provider;

    provider.mockNam->nextResponse = R"({
        "status": 200,
        "data": [
            {
                "id": 270,
                "titles": [
                    {
                        "language": "ja-Latn",
                        "traits": [
                        "native"
                        ],
                        "title": "NARUTO",
                        "is_primary": true
                    },
                    {
                        "language": "en",
                        "traits": [
                        "official"
                        ],
                        "title": "Naruto English",
                        "is_primary": true
                    }
                ],
                "description": "Ninja adventure.",
                "canonical_url": "https://mangabaka.org/manga/270/naruto",
                "type": "manga",
                "status": "completed",
                "year": 1999
            },
            {
                "id": 271,
                "titles": [
                    {
                        "language": "ja",
                        "title": "Missing English Title"
                    }
                ],
                "canonical_url": "https://mangabaka.org/manga/271/no-desc",
                "type": "manga"
            }
        ]
    })";

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    QCOMPARE(provider.mockNam->lastUrl.toString(), QStringLiteral("https://api.mangabaka.org/v2/series/search?q=naruto&limit=10"));

    completeSpy.wait(100);
    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(completeSpy.count(), 1);

    QList<QVariant> args = completeSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("mangabaka"));
    QList<SearchResult> results = args.at(1).value<QList<SearchResult>>();

    QCOMPARE(results.count(), 2);
    QCOMPARE(results[0].id, QStringLiteral("270"));
    QCOMPARE(results[0].title, QStringLiteral("Naruto English"));
    QCOMPARE(results[0].description, QStringLiteral("Ninja adventure."));
    QCOMPARE(results[0].url, QStringLiteral("https://mangabaka.org/manga/270/naruto"));
    QCOMPARE(results[0].type, QStringLiteral("manga"));
    QCOMPARE(results[0].additionalData.value("status").toString(), QStringLiteral("completed"));
    QCOMPARE(results[0].additionalData.value("year").toInt(), 1999);

    QCOMPARE(results[1].id, QStringLiteral("271"));
    QCOMPARE(results[1].title, QStringLiteral("Missing English Title"));
    QCOMPARE(results[1].description, QStringLiteral(""));
    QCOMPARE(results[1].url, QStringLiteral("https://mangabaka.org/manga/271/no-desc"));
    QCOMPARE(results[1].type, QStringLiteral("manga"));
    QVERIFY(!results[1].additionalData.contains(QStringLiteral("status")));
    QVERIFY(!results[1].additionalData.contains(QStringLiteral("year")));
}

void TestProviderMangaBaka::testNetworkFailure()
{
    TestableProviderMangaBaka provider;
    provider.mockNam->nextError = QNetworkReply::TimeoutError;

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
}

void TestProviderMangaBaka::testMalformedJsonStructure()
{
    TestableProviderMangaBaka provider;
    provider.mockNam->nextResponse = R"({ "unexpected_field": [] })";

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
    QList<QVariant> args = failSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("mangabaka"));
    QVERIFY(args.at(1).toString().contains(QStringLiteral("Malformed response: missing 'data' array")));
}

void TestProviderMangaBaka::testMalformedRecordSkip()
{
    TestableProviderMangaBaka provider;
    provider.mockNam->nextResponse = R"({
            "status": 200,
            "data": [
                {
                    "title": "Missing ID",
                    "type": "manga"
                },
                {
                    "id": "not_a_number",
                    "title": "String ID",
                    "type": "manga"
                },
                {
                    "id": 270,
                    "title": "Valid Record",
                    "type": "manga"
                }
            ]
        })";

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    completeSpy.wait(100);

    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(completeSpy.count(), 1);
    QList<QVariant> args = completeSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("mangabaka"));
    QList<SearchResult> results = args.at(1).value<QList<SearchResult>>();

    QCOMPARE(results.count(), 1);
    QCOMPARE(results[0].id, QStringLiteral("270"));
}

void TestProviderMangaBaka::testRateLimitFailure()
{
    TestableProviderMangaBaka provider;
    provider.mockNam->nextResponse = "Rate Limited";
    provider.mockNam->nextError = QNetworkReply::NoError;
    provider.mockNam->nextHttpStatus = 429;

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
    QList<QVariant> args = failSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("mangabaka"));
    QVERIFY(args.at(1).toString().contains(QStringLiteral("HTTP Error: 429")));
}

void TestProviderMangaBaka::testWrongMetadataTypeSkip()
{
    TestableProviderMangaBaka provider;
    provider.mockNam->nextResponse = R"({
        "status": 200,
        "data": [
            {
                "id": 123,
                "title": "Wrong Types",
                "type": "manga",
                "status": 404,
                "year": "2020"
            }
        ]
    })";

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    completeSpy.wait(100);

    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(completeSpy.count(), 1);
    QList<QVariant> args = completeSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("mangabaka"));
    QList<SearchResult> results = args.at(1).value<QList<SearchResult>>();

    QCOMPARE(results.count(), 1);
    QCOMPARE(results[0].id, QStringLiteral("123"));
    QCOMPARE(results[0].title, QStringLiteral("Wrong Types"));
    QVERIFY(!results[0].additionalData.contains(QStringLiteral("status")));
    QVERIFY(!results[0].additionalData.contains(QStringLiteral("year")));
}

void TestProviderMangaBaka::testParseError()
{
    TestableProviderMangaBaka provider;
    provider.mockNam->nextResponse = "Not JSON";

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
}

void TestProviderMangaBaka::testReplaceStale()
{
    TestableProviderMangaBaka provider;

    provider.search("first");
    QCOMPARE(provider.mockNam->replies.count(), 1);
    QPointer<DummyNetworkReplyMangaBaka> firstReply = provider.mockNam->replies[0];

    provider.search("second");
    QCOMPARE(provider.mockNam->replies.count(), 2);

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    if (firstReply) {
        firstReply->finish();
    }

    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(completeSpy.count(), 0);
}

void TestProviderMangaBaka::testPaginationAndLazyDetails()
{
    SearchTab tab(QStringLiteral("thirty"));
    auto *provider = new TestableProviderMangaBaka;
    auto response = [](int page) {
        QJsonArray records;
        for (int n = (page - 1) * 10 + 1; n <= page * 10; ++n) {
            records.append(QJsonObject{{QStringLiteral("id"), n}, {QStringLiteral("title"), QString::number(n)}});
        }
        const QJsonValue next =
            page < 3 ? QJsonValue(QStringLiteral("https://api.mangabaka.org/v2/series/search?q=thirty&limit=10&page=%1").arg(page + 1))
                     : QJsonValue(QJsonValue::Null);
        return QJsonDocument(QJsonObject{{QStringLiteral("data"), records},
                                         {QStringLiteral("pagination"), QJsonObject{{QStringLiteral("next"), next}}}})
            .toJson();
    };
    provider->mockNam->nextResponse = response(1);
    tab.startProvider(provider);
    auto *tree = tab.findChild<QTreeWidget *>();
    auto *group = tree->topLevelItem(0);
    QTRY_COMPARE(group->childCount(), 11);
    QCOMPARE(provider->mockNam->replies.size(), 1); // no eager detail fetches
    for (int page = 2; page <= 3; ++page) {
        provider->mockNam->nextResponse = response(page);
        tab.loadMore(provider->id());
        QCOMPARE(QUrlQuery(provider->mockNam->lastUrl).queryItemValue(QStringLiteral("page")), QString::number(page));
        QTRY_COMPARE(group->childCount(), page * 10 + 1);
    }
    QCOMPARE(provider->mockNam->replies.size(), 3);
    tree->setCurrentItem(group->child(0));
    const QString synopsis(1000, QLatin1Char('x'));
    provider->mockNam->nextResponse =
        QJsonDocument(QJsonObject{{QStringLiteral("data"), QJsonObject{{QStringLiteral("id"), 1},
                                                                       {QStringLiteral("title"), QStringLiteral("Full title")},
                                                                       {QStringLiteral("description"), synopsis}}}})
            .toJson();
    tab.loadDetails();
    QCOMPARE(provider->mockNam->lastUrl.path(), QStringLiteral("/v2/series/1"));
    QCOMPARE(QUrlQuery(provider->mockNam->lastUrl).queryItemValue(QStringLiteral("schema")), QStringLiteral("full"));
    QTRY_VERIFY(tab.findChild<QTextBrowser *>()->toPlainText().contains(synopsis));
    QVERIFY(tab.findChild<QPushButton *>(QStringLiteral("loadDetails"))->isHidden());
    tab.loadMore(provider->id());
    QCOMPARE(provider->mockNam->replies.size(), 4);
}

void TestProviderMangaBaka::testContinuationFailureAndCancellation()
{
    TestableProviderMangaBaka provider;
    QSignalSpy failure(&provider, &SearchProvider::requestFailed);
    QSignalSpy pages(&provider, &SearchProvider::pageCompleted);
    QSignalSpy details(&provider, &SearchProvider::detailsCompleted);
    provider.requestPage(QStringLiteral("query"), QStringLiteral("-1"), 7);
    QCOMPARE(failure.count(), 1);
    QCOMPARE(failure.last().at(0).toULongLong(), 7u);
    QVERIFY(provider.mockNam->replies.isEmpty());
    provider.mockNam->nextHttpStatus = 429;
    provider.mockNam->nextResponse = "rate limited";
    provider.requestPage(QStringLiteral("query"), QString(), 8);
    QTRY_COMPARE(failure.count(), 2);
    QVERIFY(failure.last().at(2).toInt() >= 1000);
    provider.mockNam->nextHttpStatus = 200;
    provider.mockNam->nextResponse = R"({"data":{"id":2,"title":"Wrong result"}})";
    provider.requestDetails(QStringLiteral("1"), QString(), 9);
    QTRY_COMPARE(failure.count(), 3);
    QVERIFY(details.isEmpty());
    provider.mockNam->nextResponse = R"({"data":[]})";
    provider.requestPage(QStringLiteral("query"), QString(), 10);
    provider.requestDetails(QStringLiteral("1"), QString(), 11);
    const auto count = failure.count();
    provider.cancelRequests();
    QCoreApplication::processEvents();
    QCOMPARE(failure.count(), count);
    QVERIFY(pages.isEmpty());
    QVERIFY(details.isEmpty());
    QVERIFY(!provider.activeReply());
}

void TestProviderMangaBaka::testUntrustedContinuation()
{
    TestableProviderMangaBaka provider;
    QSignalSpy pages(&provider, &SearchProvider::pageCompleted);
    provider.mockNam->nextResponse = R"({"data":[],"pagination":{"next":"https://evil.example/v2/series/search?page=2"}})";
    provider.requestPage(QStringLiteral("query"), {}, 1);
    QTRY_COMPARE(pages.count(), 1);
    QVERIFY(pages.last().at(1).value<SearchPage>().nextToken.isEmpty());
    provider.mockNam->nextResponse = QByteArray(8 * 1024 * 1024 + 1, 'x');
    QSignalSpy failure(&provider, &SearchProvider::requestFailed);
    provider.requestPage(QStringLiteral("query"), {}, 2);
    QTRY_COMPARE(failure.count(), 1);
    QVERIFY(failure.last().at(1).toString().contains(QStringLiteral("8 MiB")));
}

QTEST_MAIN(TestProviderMangaBaka)
#include "test_provider_mangabaka.moc"
