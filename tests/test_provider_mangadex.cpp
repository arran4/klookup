#include "../providers/mangadex/provider_mangadex.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrlQuery>
#include <QtTest>
#include <functional>

class DummyNetworkReplyMangaDex : public QNetworkReply
{
    Q_OBJECT
public:
    DummyNetworkReplyMangaDex(QObject *parent, const QByteArray &data, QNetworkReply::NetworkError err = QNetworkReply::NoError,
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

class MockNetworkAccessManagerMangaDex : public QNetworkAccessManager
{
    Q_OBJECT
public:
    MockNetworkAccessManagerMangaDex(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    QList<DummyNetworkReplyMangaDex *> replies;
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
        DummyNetworkReplyMangaDex *reply = new DummyNetworkReplyMangaDex(this, nextResponse, nextError, nextHttpStatus);
        replies.append(reply);
        QTimer::singleShot(0, reply, &DummyNetworkReplyMangaDex::finish);
        return reply;
    }
};

class TestableProviderMangaDex : public ProviderMangaDex
{
public:
    TestableProviderMangaDex(QObject *parent = nullptr) : ProviderMangaDex(parent)
    {
        QNetworkAccessManager *existing = findChild<QNetworkAccessManager *>();
        if (existing) {
            existing->setParent(nullptr);
            existing->deleteLater();
        }
        mockNam = new MockNetworkAccessManagerMangaDex(this);
        m_nam = mockNam;
    }
    MockNetworkAccessManagerMangaDex *mockNam;
    QNetworkReply *activeReply() const
    {
        return m_reply;
    }
};

class TestProviderMangaDex : public QObject
{
    Q_OBJECT
private slots:
    void testHostilePaginationIsTerminal();
    void testOffsetContinuationAndDetails();
    void testStructuredMetadata();
    void testIdentity();
    void testRequestConstructionAndEncoding();
    void testSearchSuccess();
    void testNetworkFailure();
    void testMalformedJsonStructure();
    void testMalformedRecordSkip();
    void testRateLimitFailure();
    void testParseError();
    void testReplaceStale();
};

void TestProviderMangaDex::testIdentity()
{
    ProviderMangaDex provider;
    QCOMPARE(provider.id(), QStringLiteral("mangadex"));
    QCOMPARE(provider.name(), QStringLiteral("MangaDex"));
}

void TestProviderMangaDex::testRequestConstructionAndEncoding()
{
    TestableProviderMangaDex provider;
    provider.search("naruto & sasuke");

    QUrl url = provider.mockNam->lastUrl;
    QCOMPARE(url.scheme(), QStringLiteral("https"));
    QCOMPARE(url.host(), QStringLiteral("api.mangadex.org"));
    QCOMPARE(url.path(), QStringLiteral("/manga"));

    QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("title")), QStringLiteral("naruto & sasuke"));
    QCOMPARE(query.queryItemValue(QStringLiteral("limit")), QStringLiteral("10"));
}

void TestProviderMangaDex::testSearchSuccess()
{
    TestableProviderMangaDex provider;

    provider.mockNam->nextResponse = R"({
        "data": [
            {
                "id": "12345",
                "attributes": {
                    "title": {"en": "Naruto"},
                    "description": {"en": "A ninja story."},
                    "status": "completed",
                    "year": 1999
                }
            },
            {
                "id": "12346",
                "attributes": {
                    "title": {"ja": "Naruto JA"}
                }
            }
        ]
    })";

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    QCOMPARE(provider.mockNam->lastUrl.toString(), QStringLiteral("https://api.mangadex.org/manga?title=naruto&limit=10"));

    completeSpy.wait(100);
    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(completeSpy.count(), 1);

    QList<QVariant> args = completeSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("mangadex"));
    QList<SearchResult> results = args.at(1).value<QList<SearchResult>>();

    QCOMPARE(results.count(), 2);
    QCOMPARE(results[0].id, QStringLiteral("12345"));
    QCOMPARE(results[0].title, QStringLiteral("Naruto"));
    QCOMPARE(results[0].description, QStringLiteral("A ninja story."));
    QCOMPARE(results[0].url, QStringLiteral("https://mangadex.org/title/12345"));
    QCOMPARE(results[0].type, QStringLiteral("manga"));
    QCOMPARE(results[0].additionalData.value("status").toString(), QStringLiteral("completed"));
    QCOMPARE(results[0].additionalData.value("year").toInt(), 1999);

    QCOMPARE(results[1].id, QStringLiteral("12346"));
    QCOMPARE(results[1].title, QStringLiteral("Naruto JA"));
    QCOMPARE(results[1].description, QStringLiteral(""));
    QCOMPARE(results[1].url, QStringLiteral("https://mangadex.org/title/12346"));
    QCOMPARE(results[1].type, QStringLiteral("manga"));
    QVERIFY(!results[1].additionalData.contains(QStringLiteral("status")));
    QVERIFY(!results[1].additionalData.contains(QStringLiteral("year")));
}

void TestProviderMangaDex::testNetworkFailure()
{
    TestableProviderMangaDex provider;
    provider.mockNam->nextError = QNetworkReply::TimeoutError;

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
}

void TestProviderMangaDex::testMalformedJsonStructure()
{
    TestableProviderMangaDex provider;
    provider.mockNam->nextResponse = R"({ "unexpected_field": [] })";

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
    QList<QVariant> args = failSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("mangadex"));
    QVERIFY(args.at(1).toString().contains(QStringLiteral("Malformed response: missing 'data' array")));
}

void TestProviderMangaDex::testMalformedRecordSkip()
{
    TestableProviderMangaDex provider;
    provider.mockNam->nextResponse = R"({
            "data": [
                {
                    "attributes": {
                        "title": {"en": "Missing ID"}
                    }
                },
                {
                    "id": 12345,
                    "attributes": {
                        "title": {"en": "Integer ID"}
                    }
                },
                {
                    "id": "12345",
                    "attributes": {
                        "title": {"en": "Valid Record"}
                    }
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
    QCOMPARE(args.at(0).toString(), QStringLiteral("mangadex"));
    QList<SearchResult> results = args.at(1).value<QList<SearchResult>>();

    QCOMPARE(results.count(), 1);
    QCOMPARE(results[0].id, QStringLiteral("12345"));
    QCOMPARE(results[0].title, QStringLiteral("Valid Record"));
}

void TestProviderMangaDex::testRateLimitFailure()
{
    TestableProviderMangaDex provider;
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
    QCOMPARE(args.at(0).toString(), QStringLiteral("mangadex"));
    QVERIFY(args.at(1).toString().contains(QStringLiteral("HTTP Error: 429")));
}

void TestProviderMangaDex::testParseError()
{
    TestableProviderMangaDex provider;
    provider.mockNam->nextResponse = "Not JSON";

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
}

void TestProviderMangaDex::testReplaceStale()
{
    TestableProviderMangaDex provider;

    provider.search("first");
    QCOMPARE(provider.mockNam->replies.count(), 1);
    QPointer<DummyNetworkReplyMangaDex> firstReply = provider.mockNam->replies[0];

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

void TestProviderMangaDex::testOffsetContinuationAndDetails()
{
    TestableProviderMangaDex provider;
    QSignalSpy pages(&provider, &SearchProvider::pageCompleted);
    QSignalSpy details(&provider, &SearchProvider::detailsCompleted);
    QSignalSpy failures(&provider, &SearchProvider::requestFailed);
    provider.mockNam->nextResponse = R"({"data":[{"id":"one","attributes":{"title":{"en":"One"}}}],"offset":0,"limit":10,"total":30})";
    provider.requestPage(QStringLiteral("query"), {}, 1);
    QTRY_COMPARE(pages.count(), 1);
    QCOMPARE(pages.last().at(1).value<SearchPage>().nextToken, QStringLiteral("10"));
    provider.mockNam->nextResponse = R"({"data":[{"id":"two","attributes":{"title":{"en":"Two"}}}],"offset":10,"limit":10,"total":30})";
    provider.requestPage(QStringLiteral("query"), QStringLiteral("10"), 2);
    QCOMPARE(QUrlQuery(provider.mockNam->lastUrl).queryItemValue(QStringLiteral("offset")), QStringLiteral("10"));
    QTRY_COMPARE(pages.count(), 2);
    QCOMPARE(pages.last().at(1).value<SearchPage>().nextToken, QStringLiteral("20"));
    provider.mockNam->nextResponse = R"({"data":[{"id":"three","attributes":{"title":{"en":"Three"}}}],"offset":20,"limit":10,"total":30})";
    provider.requestPage(QStringLiteral("query"), QStringLiteral("20"), 3);
    QTRY_COMPARE(pages.count(), 3);
    QVERIFY(pages.last().at(1).value<SearchPage>().nextToken.isEmpty());
    provider.mockNam->nextResponse = R"({"data":{"id":"one","attributes":{"title":{"en":"One"},"description":{"en":"Full synopsis"}}}})";
    provider.requestDetails(QStringLiteral("one"), {}, 4);
    QCOMPARE(provider.mockNam->lastUrl.path(), QStringLiteral("/manga/one"));
    QTRY_COMPARE(details.count(), 1);
    QCOMPARE(details.last().at(1).value<SearchDetails>().result.description, QStringLiteral("Full synopsis"));
    provider.requestDetails(QStringLiteral("../invalid"), {}, 5);
    QCOMPARE(failures.count(), 1);
    provider.mockNam->nextError = QNetworkReply::TimeoutError;
    provider.requestPage(QStringLiteral("query"), QStringLiteral("10"), 6);
    QTRY_COMPARE(failures.count(), 2);
    QCOMPARE(failures.last().at(0).toULongLong(), 6u);
    QVERIFY(failures.last().at(1).toString().contains(QStringLiteral("timed out")));
}

void TestProviderMangaDex::testHostilePaginationIsTerminal()
{
    TestableProviderMangaDex provider;
    QSignalSpy pages(&provider, &SearchProvider::pageCompleted);
    for (const auto &response : {QByteArray(R"({"data":[{"id":"valid"}],"offset":2147483640,"limit":100,"total":2147483647})"),
                                 QByteArray(R"({"data":[{"id":"valid"}],"offset":0,"limit":2147483647,"total":2147483647})"),
                                 QByteArray(R"({"data":[{"id":"valid"}],"offset":20,"limit":10,"total":100})")}) {
        provider.mockNam->nextResponse = response;
        provider.requestPage(QStringLiteral("query"), {}, 1);
        QTRY_VERIFY(!pages.isEmpty());
        QVERIFY(pages.takeFirst().at(1).value<SearchPage>().nextToken.isEmpty());
    }
}

void TestProviderMangaDex::testStructuredMetadata()
{
    TestableProviderMangaDex provider;
    QFile fixture(QStringLiteral(FIXTURE_DIR "/mangadex-full.json"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    provider.mockNam->nextResponse = fixture.readAll();
    QSignalSpy details(&provider, &SearchProvider::detailsCompleted);
    const QString id = QStringLiteral("example-id");
    provider.requestDetails(id, {}, 42);
    QTRY_COMPARE(details.count(), 1);
    const auto result = details.first().at(1).value<SearchDetails>();
    QCOMPARE(result.result.id, id);
    QVERIFY(!result.metadata.sections.isEmpty());
    QStringList labels;
    QStringList values;
    QList<QUrl> links;
    std::function<void(const MetadataNode &)> collect = [&](const MetadataNode &node) {
        labels.append(node.label);
        for (const auto &value : node.values) {
            values.append(value.text);
            if (!value.url.isEmpty())
                links.append(value.url);
        }
        for (const auto &child : node.children)
            collect(child);
    };
    for (const auto &section : result.metadata.sections)
        collect(section);
    QVERIFY(!result.metadata.attribution.isEmpty());
    QVERIFY(labels.contains(QStringLiteral("genre")));
    QVERIFY(labels.contains(QStringLiteral("theme")));
    QVERIFY(labels.contains(QStringLiteral("future-group")));
    QVERIFY(values.contains(QStringLiteral("Another name")));
    QVERIFY(values.contains(QStringLiteral("Named Author")));
    QVERIFY(values.contains(QStringLiteral("Named Artist")));
    QVERIFY(links.contains(QUrl(QStringLiteral("https://anilist.co/manga/12345"))));
    const auto includes = QUrlQuery(provider.mockNam->lastUrl).allQueryItemValues(QStringLiteral("includes[]"));
    QVERIFY(includes.contains(QStringLiteral("author")));
    QVERIFY(includes.contains(QStringLiteral("artist")));
    for (const auto &url : links) {
        QVERIFY(url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"));
        QVERIFY(url.userInfo().isEmpty());
    }
}

QTEST_MAIN(TestProviderMangaDex)
#include "test_provider_mangadex.moc"
