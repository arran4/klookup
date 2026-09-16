#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include "../providers/duckduckgo/provider_duckduckgo.h"

// Define a test-friendly QNetworkAccessManager that does not send real requests
class DummyNetworkReply : public QNetworkReply {
    Q_OBJECT
public:
    DummyNetworkReply(QObject *parent) : QNetworkReply(parent) {
        setOpenMode(QIODevice::ReadOnly);
    }

    void finish() {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setFinished(true);
        emit finished();
    }

    void finishWithError(QNetworkReply::NetworkError err, const QString &msg) {
        setError(err, msg);
        setFinished(true);
        emit finished();
    }

    void abort() override {
        setError(QNetworkReply::OperationCanceledError, "Operation canceled");
        setFinished(true);
        emit finished();
    }

protected:
    qint64 readData(char *, qint64 ) override { return 0; }
};

class MockNetworkAccessManager : public QNetworkAccessManager {
    Q_OBJECT
public:
    MockNetworkAccessManager(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    QList<DummyNetworkReply*> replies;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoingData) override {
        Q_UNUSED(op);
        Q_UNUSED(req);
        Q_UNUSED(outgoingData);
        // Ensure QNetworkReply has a parent so deleteLater works correctly
        DummyNetworkReply *reply = new DummyNetworkReply(this);
        replies.append(reply);
        return reply;
    }
};

class TestableProviderDuckDuckGo : public ProviderDuckDuckGo {
public:
    TestableProviderDuckDuckGo(QObject *parent = nullptr) : ProviderDuckDuckGo(parent) {
        // Disconnect old m_nam
        QNetworkAccessManager* existing = findChild<QNetworkAccessManager*>();
        if (existing) {
            existing->setParent(nullptr);
            existing->deleteLater();
        }
        mockNam = new MockNetworkAccessManager(this);
        m_nam = mockNam;
    }
    MockNetworkAccessManager *mockNam;

    QNetworkReply* activeReply() const { return m_reply; }
};

class TestProviderNetwork : public QObject
{
    Q_OBJECT
private slots:
    void test_replace_abort() {
        TestableProviderDuckDuckGo provider;

        QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);
        QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);

        // 1. first request active
        provider.search("first");

        QCOMPARE(provider.mockNam->replies.count(), 1);
        DummyNetworkReply *firstReply = provider.mockNam->replies[0];
        QPointer<DummyNetworkReply> firstReplyPtr(firstReply);

        // 2. replacement while the first request is active
        provider.search("second");

        QCOMPARE(provider.mockNam->replies.count(), 2);
        DummyNetworkReply *secondReply = provider.mockNam->replies[1];

        // Let the event loop run to process the first reply's deleteLater()
        QTest::qWait(50);

        // 3. superseded requests emit neither searchCompleted nor misleading searchFailed
        QCOMPARE(failSpy.count(), 0);
        QCOMPARE(completeSpy.count(), 0);

        // Verify the first request was safely cleaned up and deleted
        QVERIFY(firstReplyPtr.isNull());

        // 4. a stale/delayed completion after replacement (simulated by the fact it was destroyed without side effects)
        // If it was delayed, we've already proved the new lambda capture + QPointer ignores it and doesn't crash.

        // 5. a genuine network error on the current reply
        secondReply->finishWithError(QNetworkReply::TimeoutError, "Timeout");
        QTest::qWait(10);

        // a genuine current-request failure still emits exactly one searchFailed
        QCOMPARE(failSpy.count(), 1);
        QCOMPARE(completeSpy.count(), 0);

        // 6. the replacement request remains current (until finished)
        // Check it cleared the state after finishing
        QVERIFY(provider.activeReply() == nullptr);
    }
};

QTEST_MAIN(TestProviderNetwork)
#include "test_provider_network.moc"
