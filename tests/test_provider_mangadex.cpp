#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include "../providers/mangadex/provider_mangadex.h"

class DummyNetworkReplyMangaDex : public QNetworkReply {
    Q_OBJECT
public:
    DummyNetworkReplyMangaDex(QObject *parent, const QByteArray &data, QNetworkReply::NetworkError err = QNetworkReply::NoError)
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

class MockNetworkAccessManagerMangaDex : public QNetworkAccessManager {
    Q_OBJECT
public:
    MockNetworkAccessManagerMangaDex(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    QList<DummyNetworkReplyMangaDex*> replies;
    QByteArray nextResponse;
    QNetworkReply::NetworkError nextError = QNetworkReply::NoError;
    QUrl lastUrl;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override {
        Q_UNUSED(op);
        Q_UNUSED(outgoingData);
        lastUrl = request.url();
        DummyNetworkReplyMangaDex *reply = new DummyNetworkReplyMangaDex(this, nextResponse, nextError);
        replies.append(reply);
        QTimer::singleShot(0, reply, &DummyNetworkReplyMangaDex::finish);
        return reply;
    }
};

class TestableProviderMangaDex : public ProviderMangaDex {
public:
    TestableProviderMangaDex(QObject *parent = nullptr) : ProviderMangaDex(parent) {
        QNetworkAccessManager* existing = findChild<QNetworkAccessManager*>();
        if (existing) {
            existing->setParent(nullptr);
            existing->deleteLater();
        }
        mockNam = new MockNetworkAccessManagerMangaDex(this);
        m_nam = mockNam;
    }
    MockNetworkAccessManagerMangaDex *mockNam;
    QNetworkReply* activeReply() const { return m_reply; }
};

class TestProviderMangaDex : public QObject
{
    Q_OBJECT
private slots:
    void testIdentity();
    void testSearchSuccess();
    void testNetworkFailure();
    void testParseError();
    void testReplaceStale();
};

void TestProviderMangaDex::testIdentity() {
    ProviderMangaDex provider;
    QCOMPARE(provider.id(), QStringLiteral("mangadex"));
    QCOMPARE(provider.name(), QStringLiteral("MangaDex"));
}

void TestProviderMangaDex::testSearchSuccess() {
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

void TestProviderMangaDex::testNetworkFailure() {
    TestableProviderMangaDex provider;
    provider.mockNam->nextError = QNetworkReply::TimeoutError;

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
}

void TestProviderMangaDex::testParseError() {
    TestableProviderMangaDex provider;
    provider.mockNam->nextResponse = "Not JSON";

    QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);
    QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

    provider.search("naruto");
    failSpy.wait(100);

    QCOMPARE(completeSpy.count(), 0);
    QCOMPARE(failSpy.count(), 1);
}

void TestProviderMangaDex::testReplaceStale() {
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

QTEST_MAIN(TestProviderMangaDex)
#include "test_provider_mangadex.moc"
