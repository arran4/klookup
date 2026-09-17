#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include "../providers/duckduckgo/provider_duckduckgo.h"

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

        provider.search("first");

        QCOMPARE(provider.mockNam->replies.count(), 1);
        DummyNetworkReply *firstReply = provider.mockNam->replies[0];
        QPointer<DummyNetworkReply> firstReplyPtr(firstReply);

        // 2. replacement while the first request is active
        provider.search("second");

        QCOMPARE(provider.mockNam->replies.count(), 2);
        DummyNetworkReply *secondReply = provider.mockNam->replies[1];

        // Emitting stale signal on the old reply explicitly BEFORE deleteLater takes effect (which drops the pointer).
        // Since we explicitly want to test the stale delayed signal edge case.
        if (firstReplyPtr) {
            firstReplyPtr->finish();
        }

        // Prove that emitting the stale delayed completion doesn't disturb anything
        QCOMPARE(failSpy.count(), 0);
        QCOMPARE(completeSpy.count(), 0);
        QCOMPARE(provider.activeReply(), secondReply);

        // Let the event loop run to process the first reply's deleteLater()
        // Use sendPostedEvents instead of qWait to be immediate without timers if possible,
        // but QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete) ensures it gets deleted.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        // Verify the first request was safely cleaned up and deleted
        QVERIFY(firstReplyPtr.isNull());

        // 5. a genuine network error on the current reply
        secondReply->finishWithError(QNetworkReply::TimeoutError, "Timeout");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        // a genuine current-request failure still emits exactly one searchFailed
        QCOMPARE(failSpy.count(), 1);
        QCOMPARE(completeSpy.count(), 0);

        // 6. the replacement request remains current (until finished)
        // Check it cleared the state after finishing
        QVERIFY(provider.activeReply() == nullptr);
    }

    void test_reentrancy_on_error() {
        TestableProviderDuckDuckGo provider;

        QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);

        // Connect to searchFailed to immediately fire another search to test re-entrancy
        connect(&provider, &SearchProvider::searchFailed, this, [&provider]() {
            QCOMPARE(provider.activeReply(), nullptr);
            provider.search("replacement_from_error");
        });

        provider.search("first");
        QCOMPARE(provider.mockNam->replies.count(), 1);
        DummyNetworkReply *firstReply = provider.mockNam->replies[0];

        // Finish with error -> emits searchFailed -> triggers re-entrant search("replacement_from_error")
        firstReply->finishWithError(QNetworkReply::TimeoutError, "Timeout");

        QCOMPARE(failSpy.count(), 1);
        QCOMPARE(provider.mockNam->replies.count(), 2);

        DummyNetworkReply *secondReply = provider.mockNam->replies[1];

        // If m_reply was cleared AFTER the signal emission, the new reply would have been wiped out!
        // So the active reply MUST be the second one.
        QCOMPARE(provider.activeReply(), secondReply);
    }
};

QTEST_MAIN(TestProviderNetwork)
#include "test_provider_network.moc"
