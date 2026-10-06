#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include "../providers/mangabaka/provider_mangabaka.h"

class DummyNetworkReplyMangaBaka : public QNetworkReply {
    Q_OBJECT
public:
    DummyNetworkReplyMangaBaka(QObject *parent, const QByteArray &data, QNetworkReply::NetworkError err = QNetworkReply::NoError)
        : QNetworkReply(parent), m_data(data) {
        setOpenMode(QIODevice::ReadOnly);
        setError(err, "");
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, err == QNetworkReply::NoError ? 200 : 500);
    }

    void finish() {
        setFinished(true);
        emit finished();
    }

    void abort() override {
        setError(QNetworkReply::OperationCanceledError, "Operation canceled");
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override { return m_data.size() + QNetworkReply::bytesAvailable(); }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *data, qint64 maxlen) override {
        qint64 len = qMin(maxlen, (qint64)m_data.size());
        memcpy(data, m_data.constData(), len);
        m_data.remove(0, len);
        return len;
    }

private:
    QByteArray m_data;
};

class MockNetworkAccessManagerMangaBaka : public QNetworkAccessManager {
    Q_OBJECT
public:
    MockNetworkAccessManagerMangaBaka(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    QList<DummyNetworkReplyMangaBaka*> replies;
    QByteArray nextResponse;
    QNetworkReply::NetworkError nextError = QNetworkReply::NoError;
    QUrl lastUrl;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override {
        Q_UNUSED(op);
        Q_UNUSED(outgoingData);
        lastUrl = request.url();
        DummyNetworkReplyMangaBaka *reply = new DummyNetworkReplyMangaBaka(this, nextResponse, nextError);
        replies.append(reply);
        QTimer::singleShot(0, reply, &DummyNetworkReplyMangaBaka::finish);
        return reply;
    }
};

class TestableProviderMangaBaka : public ProviderMangaBaka {
public:
    TestableProviderMangaBaka(QObject *parent = nullptr) : ProviderMangaBaka(parent) {
        QNetworkAccessManager* existing = findChild<QNetworkAccessManager*>();
        if (existing) {
            existing->setParent(nullptr);
            existing->deleteLater();
        }
        mockNam = new MockNetworkAccessManagerMangaBaka(this);
        m_nam = mockNam;
    }
    MockNetworkAccessManagerMangaBaka *mockNam;
    QNetworkReply* activeReply() const { return m_reply; }
};

class TestProviderMangaBaka : public QObject
{
    Q_OBJECT
private slots:
    void testIdentity();
    void testSearchSuccess();
    void testNetworkFailure();
    void testParseError();
    void testReplaceStale();
};

void TestProviderMangaBaka::testIdentity() {
    ProviderMangaBaka provider;
    QCOMPARE(provider.id(), QStringLiteral("mangabaka"));
    QCOMPARE(provider.name(), QStringLiteral("MangaBaka"));
}

void TestProviderMangaBaka::testSearchSuccess() {
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
    QCOMPARE(results[1].additionalData.value("status").toString(), QStringLiteral(""));
    QCOMPARE(results[1].additionalData.value("year").toInt(), 0);
}

void TestProviderMangaBaka::testNetworkFailure() {
    TestableProviderMangaBaka provider;
    provider.mockNam->nextError = QNetworkReply::TimeoutError;

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
}

void TestProviderMangaBaka::testParseError() {
    TestableProviderMangaBaka provider;
    provider.mockNam->nextResponse = "Not JSON";

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
}

void TestProviderMangaBaka::testReplaceStale() {
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

QTEST_MAIN(TestProviderMangaBaka)
#include "test_provider_mangabaka.moc"
