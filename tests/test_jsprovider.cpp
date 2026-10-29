// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jsprovider.h"
#include "providerpackage.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QThread>
#include <QTemporaryDir>
#include <QFile>

class FakeNetworkReply : public QNetworkReply
{
    Q_OBJECT
public:
    FakeNetworkReply(const QNetworkRequest &request, QObject *parent = nullptr)
        : QNetworkReply(parent), m_request(request)
    {
        setRequest(request);
        setUrl(request.url());
        setOpenMode(QIODevice::ReadOnly);
    }

    void respondSuccess(int statusCode, const QByteArray &data)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        m_data = data;
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit metaDataChanged();
            emit readyRead();
            emit finished();
        });
    }

    void abort() override
    {
        m_aborted = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"));
        setFinished(true);
        emit finished();
    }

    bool isAborted() const { return m_aborted; }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const qint64 len = qMin(maxlen, static_cast<qint64>(m_data.size() - m_offset));
        if (len <= 0) return 0;
        memcpy(data, m_data.constData() + m_offset, len);
        m_offset += len;
        return len;
    }

    qint64 bytesAvailable() const override
    {
        return (m_data.size() - m_offset) + QIODevice::bytesAvailable();
    }

private:
    QNetworkRequest m_request;
    QByteArray m_data;
    qint64 m_offset = 0;
    bool m_aborted = false;
};

class FakeNetworkAccessManager : public QNetworkAccessManager
{
    Q_OBJECT
public:
    explicit FakeNetworkAccessManager(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    std::function<FakeNetworkReply *(const QNetworkRequest &req, QNetworkAccessManager::Operation op)> handler;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoingData) override
    {
        Q_UNUSED(outgoingData);
        if (handler) {
            return handler(req, op);
        }
        auto *rep = new FakeNetworkReply(req, this);
        rep->respondSuccess(200, "OK");
        return rep;
    }
};

class TestJsProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testNeverSettlingOperationsHaveIndependentDeadlines()
    {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        JsProvider provider(pkg);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        provider.setOperationTimeoutMs(25);
        QSignalSpy pages(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        QSignalSpy details(&provider, &ProviderApiV1::ProviderV1::detailsFailed);
        QSignalSpy content(&provider, &ProviderApiV1::ProviderV1::contentFailed);
        provider.search(QStringLiteral("never"), 1);
        provider.getDetails(QStringLiteral("never"), 2);
        provider.getContent(QStringLiteral("never"), {}, 3);
        QTRY_COMPARE(pages.count(), 1);
        QTRY_COMPARE(details.count(), 1);
        QTRY_COMPARE(content.count(), 1);
        QCOMPARE(pages.first()[1].value<ProviderApiV1::ErrorCode>(), ProviderApiV1::ErrorCode::Timeout);
        QCOMPARE(details.first()[2].value<ProviderApiV1::ErrorCode>(), ProviderApiV1::ErrorCode::Timeout);
        QCOMPARE(content.first()[2].value<ProviderApiV1::ErrorCode>(), ProviderApiV1::ErrorCode::Timeout);
        QCOMPARE(provider.activeOperationCount(), 0);
        QCOMPARE(provider.pendingFetchCount(), 0);
        QCOMPARE(provider.retainedPromiseCount(), 0);
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        provider.search(QStringLiteral("state"));
        QTRY_COMPARE(ready.count(), 1);
        provider.search(QStringLiteral("state"));
        QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(ready[1][1].value<SearchPage>().results.first().id, QStringLiteral("2"));
    }

    void testRepeatedCancellationAndReusedRequestIds()
    {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        FakeNetworkAccessManager nam;
        int aborted = 0;
        nam.handler = [&](const QNetworkRequest &request, QNetworkAccessManager::Operation) {
            auto *reply = new FakeNetworkReply(request, &nam);
            connect(reply, &QNetworkReply::finished, this, [&, reply] { if (reply->isAborted()) ++aborted; });
            return reply;
        };
        ProviderHost::ProviderHostService host(pkg, nullptr, &nam);
        JsProvider provider(pkg, nullptr, &host);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        QSignalSpy failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        for (int i = 0; i < 50; ++i) {
            provider.search(QStringLiteral("fetch"), 77);
            QCOMPARE(provider.pendingFetchCount(), 1);
            provider.cancel(77);
            provider.cancel(77);
            QCOMPARE(provider.activeOperationCount(), 0);
            QCOMPARE(provider.pendingFetchCount(), 0);
        QCOMPARE(provider.retainedPromiseCount(), 0);
        }
        QCOMPARE(aborted, 50);
        provider.search(QStringLiteral("state"), 77);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(provider.activeOperationCount(), 0);
        QCOMPARE(provider.pendingFetchCount(), 0);
        QCOMPARE(provider.retainedPromiseCount(), 0);
        QTRY_VERIFY([&] {
            for (auto *child : provider.children()) if (QByteArray(child->metaObject()->className()) == "JsPromiseHandler") return false;
            return true;
        }());
    }

    void testChainedFetchesRemainOwnedByOverlappingOperations()
    {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        FakeNetworkAccessManager nam;
        QMap<QString, QPointer<FakeNetworkReply>> replies;
        nam.handler = [&](const QNetworkRequest &request, QNetworkAccessManager::Operation) {
            auto *reply = new FakeNetworkReply(request, &nam);
            replies.insert(request.url().path(), reply);
            return reply;
        };
        ProviderHost::ProviderHostService host(pkg, nullptr, &nam);
        JsProvider provider(pkg, nullptr, &host);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        provider.search(QStringLiteral("chain-a"), 101);
        provider.search(QStringLiteral("chain-b"), 102);
        QCOMPARE(replies.size(), 2);
        replies[QStringLiteral("/first/chain-a")]->respondSuccess(200, "{}");
        QTRY_COMPARE(replies.size(), 3);
        replies[QStringLiteral("/first/chain-b")]->respondSuccess(200, "{}");
        QTRY_COMPARE(replies.size(), 4);
        auto a = replies.value(QStringLiteral("/second/chain-a"));
        auto b = replies.value(QStringLiteral("/second/chain-b"));
        QVERIFY(a && b);
        provider.cancel(101);
        QVERIFY(a->isAborted());
        QVERIFY(!b->isAborted());
        b->respondSuccess(200, "[]");
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first().first().toULongLong(), 102ULL);
        QCOMPARE(provider.activeOperationCount(), 0);
        QCOMPARE(provider.pendingFetchCount(), 0);
        QCOMPARE(provider.retainedPromiseCount(), 0);
    }

    void testCompletionAndDeadlineAbortAllOwnedWork()
    {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        FakeNetworkAccessManager nam;
        int aborted = 0;
        nam.handler = [&](const QNetworkRequest &request, QNetworkAccessManager::Operation) {
            auto *reply = new FakeNetworkReply(request, &nam);
            connect(reply, &QNetworkReply::finished, this, [&, reply] { if (reply->isAborted()) ++aborted; });
            return reply;
        };
        ProviderHost::ProviderHostService host(pkg, nullptr, &nam);
        JsProvider provider(pkg, nullptr, &host);
        provider.setOperationTimeoutMs(30);
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        QSignalSpy failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        provider.search(QStringLiteral("detached"));
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(aborted, 2);
        QCOMPARE(provider.pendingFetchCount(), 0);
        QCOMPARE(provider.retainedPromiseCount(), 0);
        provider.search(QStringLiteral("fetch"));
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(aborted, 3);
        QCOMPARE(failed.first()[1].value<ProviderApiV1::ErrorCode>(), ProviderApiV1::ErrorCode::Timeout);
        QCOMPARE(provider.activeOperationCount(), 0);
        QCOMPARE(provider.pendingFetchCount(), 0);
        QCOMPARE(provider.retainedPromiseCount(), 0);
    }

    void testDetailsContentFallbackDoesNotRetainHandlers()
    {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        JsProvider provider(pkg);
        QSignalSpy content(&provider, &ProviderApiV1::ProviderV1::contentReady);
        QSignalSpy details(&provider, &ProviderApiV1::ProviderV1::detailsReady);
        provider.getContent(QStringLiteral("id"), {}, 77);
        QTRY_COMPARE(content.count(), 1);
        QCOMPARE(content.first()[1].value<ProviderApiV1::ContentResponse>().content.description, QStringLiteral("Details content"));
        provider.getDetails(QStringLiteral("id"), 77);
        QTRY_COMPARE(details.count(), 1);
        QCOMPARE(content.count(), 1);
        QCOMPARE(provider.activeOperationCount(), 0);
        QCOMPARE(provider.retainedPromiseCount(), 0);
    }

    void testBorrowedHostDestructionDuringOperation()
    {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        FakeNetworkAccessManager nam;
        nam.handler = [&](const QNetworkRequest &request, QNetworkAccessManager::Operation) { return new FakeNetworkReply(request, &nam); };
        auto host = std::make_unique<ProviderHost::ProviderHostService>(pkg, nullptr, &nam);
        JsProvider provider(pkg, nullptr, host.get());
        provider.setOperationTimeoutMs(25);
        QSignalSpy failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        provider.search(QStringLiteral("fetch"));
        host.reset();
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(provider.activeOperationCount(), 0);
        QCOMPARE(provider.pendingFetchCount(), 0);
        QCOMPARE(provider.retainedPromiseCount(), 0);
    }

    void testCpuInterruption_data()
    {
        QTest::addColumn<QString>("query");
        QTest::newRow("synchronous-loop") << QStringLiteral("spin");
        QTest::newRow("promise-callback-loop") << QStringLiteral("microtask-spin");
        QTest::newRow("promise-job-storm") << QStringLiteral("microtask-storm");
        QTest::newRow("returned-thenable-loop") << QStringLiteral("thenable-spin");
        QTest::newRow("export-thenable-loop") << QStringLiteral("export-thenable-spin");
        QTest::newRow("resolved-thenable-loop") << QStringLiteral("resolve-thenable-spin");
    }

    void testCpuInterruption()
    {
        QFETCH(QString, query);
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        JsProvider provider(pkg);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QSignalSpy failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        QElapsedTimer elapsed;
        elapsed.start();
        provider.search(query);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(elapsed.elapsed() < 1500);
        QCOMPARE(failed.first()[1].value<ProviderApiV1::ErrorCode>(), ProviderApiV1::ErrorCode::Timeout);
        QCOMPARE(provider.activeOperationCount(), 0);
        QVERIFY(!provider.isLoaded());
    }

    void testQueuedCompletionCancellationAndConsumerDestruction()
    {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        JsProvider revoked(pkg);
        QSignalSpy late(&revoked, &ProviderApiV1::ProviderV1::pageReady);
        revoked.search(QStringLiteral("sync"), 77);
        revoked.invalidate();
        QTest::qWait(10);
        QCOMPARE(late.count(), 0);
        QPointer<JsProvider> provider = new JsProvider(pkg);
        QSignalSpy ready(provider, &ProviderApiV1::ProviderV1::pageReady);
        provider->search(QStringLiteral("sync"), 7);
        provider->cancel(7);
        QTest::qWait(10);
        QCOMPARE(ready.count(), 0);
        connect(provider, &ProviderApiV1::ProviderV1::pageReady, this, [provider] { delete provider; });
        provider->search(QStringLiteral("state"));
        QTRY_VERIFY(provider.isNull());
        QCOMPARE(ready.count(), 1);
    }

    void testNativeWaitDoesNotConsumeLinuxCpuBudget()
    {
#ifndef Q_OS_LINUX
        QSKIP("Per-thread CPU accounting is Linux-specific; other platforms use elapsed execution time");
#else
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        FakeNetworkAccessManager network;
        network.handler = [](const QNetworkRequest &request, QNetworkAccessManager::Operation) {
            // Deliberately block in a borrowed native dependency. The watchdog
            // must charge CPU consumed, not time descheduled/asleep. This is
            // not a promise that arbitrary native blocking can be preempted.
            QThread::msleep(300);
            auto *reply = new FakeNetworkReply(request);
            reply->respondSuccess(200, "[]");
            return reply;
        };
        ProviderHost::ProviderHostService host(pkg, nullptr, &network);
        JsProvider provider(pkg, nullptr, &host);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        QSignalSpy failed(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        provider.search(QStringLiteral("fetch"));
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(failed.count(), 0);
        QVERIFY(provider.isLoaded());
#endif
    }

    void testNestedThenablesKeepOperationOwnership()
    {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        JsProvider provider(pkg);
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        const QList<QPair<QString, QString>> cases = {{QStringLiteral("nested-thenable"), QStringLiteral("nested")},
            {QStringLiteral("baseline"), QStringLiteral("baseline")}, {QStringLiteral("subclass"), QStringLiteral("subclass")},
            {QStringLiteral("all"), QStringLiteral("one")}};
        for (const auto &item : cases) {
            provider.search(item.first);
            QTRY_COMPARE(ready.count(), 1);
            const auto results = ready.takeFirst()[1].value<SearchPage>().results;
            QCOMPARE(results.first().id, item.second);
            QCOMPARE(results.size(), item.first == QLatin1String("all") ? 2 : 1);
            QCOMPARE(provider.retainedPromiseCount(), 0);
        }
    }

    void testInterruptedEnginePreservesOtherProvidersQueuedWork()
    {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        FakeNetworkAccessManager nam;
        FakeNetworkReply *reply = nullptr;
        nam.handler = [&](const QNetworkRequest &request, QNetworkAccessManager::Operation) {
            reply = new FakeNetworkReply(request, &nam);
            return reply;
        };
        ProviderHost::ProviderHostService host(pkg, nullptr, &nam);
        JsProvider healthy(pkg, nullptr, &host), interrupted(pkg);
        QSignalSpy ready(&healthy, &ProviderApiV1::ProviderV1::pageReady);
        QSignalSpy failed(&interrupted, &ProviderApiV1::ProviderV1::pageFailed);
        healthy.search(QStringLiteral("fetch"));
        QVERIFY(reply);
        reply->respondSuccess(200, "[]");
        interrupted.search(QStringLiteral("spin"));
        QTRY_COMPARE(failed.count(), 1);
        QTRY_COMPARE(ready.count(), 1);
        QVERIFY(healthy.isLoaded());
        healthy.search(QStringLiteral("state"));
        QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(healthy.retainedPromiseCount(), 0);
    }

    void testAccountingBridgeIsPrivate()
    {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(FIXTURES_DIR "/providers.d/lifecycle_js"));
        pkg.setTrusted(true);
        JsProvider provider(pkg);
        QSignalSpy ready(&provider, &ProviderApiV1::ProviderV1::pageReady);
        provider.search(QStringLiteral("boundary"));
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first()[1].value<SearchPage>().results.first().title, QStringLiteral("undefined,undefined,undefined"));
    }

    void testModuleInitializationIsInterruptible()
    {
        QTemporaryDir temporary;
        QFile manifest(temporary.filePath(QStringLiteral("provider.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write("{\"id\":\"loop\",\"name\":\"Loop\"}");
        manifest.close();
        QFile source(temporary.filePath(QStringLiteral("main.mjs")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("while (true) {} export function search() { return []; }");
        source.close();
        auto pkg = ProviderPackage::fromDirectory(temporary.path());
        pkg.setTrusted(true);
        QElapsedTimer elapsed;
        elapsed.start();
        JsProvider provider(pkg);
        QVERIFY(elapsed.elapsed() < 1500);
        QVERIFY(!provider.isLoaded());
        QVERIFY(provider.loadError().contains(QStringLiteral("CPU budget")));
    }

    void testUntrustedJsProviderRejected()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d/sample_js");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir);
        QVERIFY(pkg.isValid());
        QVERIFY(!pkg.isTrusted()); // untrusted by default

        JsProvider provider(pkg, this);
        QVERIFY(!provider.isLoaded());
        QVERIFY(provider.loadError().contains(QStringLiteral("Untrusted provider package")));

        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        const quint64 id = provider.search(QStringLiteral("test"));
        QVERIFY(spy.wait(500));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toULongLong(), id);
    }

    void testJsSearchAndPromiseHandling()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d/sample_js");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir);
        QVERIFY(pkg.isValid());
        pkg.setTrusted(true);

        JsProvider provider(pkg, this);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QCOMPARE(provider.id(), QStringLiteral("sample_js"));
        QCOMPARE(provider.name(), QStringLiteral("Sample JS Provider"));

        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        const quint64 id = provider.search(QStringLiteral("Plasma 6"));
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toULongLong(), id);

        const auto page = spy.first().at(1).value<SearchPage>();
        QCOMPARE(page.results.size(), 1);
        QCOMPARE(page.results.first().id, QStringLiteral("js-1"));
        QCOMPARE(page.results.first().title, QStringLiteral("JS Result: Plasma 6"));
        QCOMPARE(page.results.first().type, QStringLiteral("Scripted"));
    }

    void testJsPagination()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d/sample_js");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir);
        pkg.setTrusted(true);

        JsProvider provider(pkg, this);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));

        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        const quint64 id = provider.getPage(QStringLiteral("query"), QStringLiteral("tok-123"));
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toULongLong(), id);

        const auto page = spy.first().at(1).value<SearchPage>();
        QCOMPARE(page.results.size(), 1);
        QCOMPARE(page.results.first().id, QStringLiteral("js-paged-tok-123"));
        QCOMPARE(page.nextToken, QStringLiteral("next-page-tok"));
    }

    void testJsDetailsAndSeparateContent()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d/sample_js");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir);
        pkg.setTrusted(true);

        JsProvider provider(pkg, this);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));

        // 1. Details
        QSignalSpy detailsSpy(&provider, &ProviderApiV1::ProviderV1::detailsReady);
        const quint64 detId = provider.getDetails(QStringLiteral("js-1"));
        QVERIFY(detailsSpy.wait(1000));
        QCOMPARE(detailsSpy.count(), 1);
        QCOMPARE(detailsSpy.first().at(0).toULongLong(), detId);

        const auto details = detailsSpy.first().at(2).value<SearchDetails>();
        QCOMPARE(details.result.id, QStringLiteral("js-1"));
        QCOMPARE(details.result.title, QStringLiteral("Details for js-1"));
        QCOMPARE(details.metadata.attribution, QStringList{QStringLiteral("JS Community")});

        // 2. Separate Content
        QSignalSpy contentSpy(&provider, &ProviderApiV1::ProviderV1::contentReady);
        const quint64 contId = provider.getContent(QStringLiteral("js-1"));
        QVERIFY(contentSpy.wait(1000));
        QCOMPARE(contentSpy.count(), 1);
        QCOMPARE(contentSpy.first().at(0).toULongLong(), contId);

        const auto contentResp = contentSpy.first().at(1).value<ProviderApiV1::ContentResponse>();
        QCOMPARE(contentResp.resultId, QStringLiteral("js-1"));
        QCOMPARE(contentResp.content.format, ResultContent::Format::Markdown);
        QVERIFY(contentResp.content.description.contains(QStringLiteral("Fetched lazily for result js-1")));
    }

    void testJsScriptExceptionHandling()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d/sample_js");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir);
        pkg.setTrusted(true);

        JsProvider provider(pkg, this);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));

        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageFailed);
        const quint64 id = provider.search(QStringLiteral("throw_error"));
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toULongLong(), id);
        const QString errorMsg = spy.first().at(2).toString();
        QVERIFY(errorMsg.contains(QStringLiteral("Script error from search")));
    }

    void testJsCancellation()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d/sample_js");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir);
        pkg.setTrusted(true);

        JsProvider provider(pkg, this);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));

        QSignalSpy readySpy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        const quint64 id = provider.search(QStringLiteral("Plasma"));
        provider.cancel(id);

        // Even though promise would resolve, cancellation must drop the callback
        QTest::qWait(200);
        QCOMPARE(readySpy.count(), 0);
    }

    void testUnderlyingNetworkCancellationOnOperationCancel()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d/sample_js");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir);
        pkg.setTrusted(true);

        auto *mockNam = new FakeNetworkAccessManager(this);
        FakeNetworkReply *capturedReply = nullptr;
        mockNam->handler = [&](const QNetworkRequest &req, QNetworkAccessManager::Operation) -> FakeNetworkReply * {
            capturedReply = new FakeNetworkReply(req, mockNam);
            // Do not immediately finish; simulate slow network
            return capturedReply;
        };

        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNam);
        JsProvider provider(pkg, this, hostService);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));

        QSignalSpy readySpy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        const quint64 opId = provider.search(QStringLiteral("fetch_test"));
        QVERIFY(capturedReply != nullptr);
        QVERIFY(!capturedReply->isAborted());

        // Cancel operation: must abort underlying HTTP reply
        provider.cancel(opId);
        QVERIFY(capturedReply->isAborted());
        QTest::qWait(100);
        QCOMPARE(readySpy.count(), 0);
    }

    void testRapidCancellationAndOverlappingSearches()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d/sample_js");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir);
        pkg.setTrusted(true);

        auto *mockNam = new FakeNetworkAccessManager(this);
        QList<FakeNetworkReply *> replies;
        mockNam->handler = [&](const QNetworkRequest &req, QNetworkAccessManager::Operation) -> FakeNetworkReply * {
            auto *reply = new FakeNetworkReply(req, mockNam);
            replies.append(reply);
            return reply;
        };

        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNam);
        JsProvider provider(pkg, this, hostService);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));

        QSignalSpy readySpy(&provider, &ProviderApiV1::ProviderV1::pageReady);

        // Start search 1 and search 2
        const quint64 id1 = provider.search(QStringLiteral("fetch_test"));
        const quint64 id2 = provider.search(QStringLiteral("fetch_test"));
        QCOMPARE(replies.size(), 2);

        // Cancel search 1
        provider.cancel(id1);
        QVERIFY(replies[0]->isAborted());
        QVERIFY(!replies[1]->isAborted());

        // Resolve search 2
        replies[1]->respondSuccess(200, "[{\"id\":\"js-2\",\"title\":\"Result 2\"}]");
        QVERIFY(readySpy.wait(500));
        QCOMPARE(readySpy.count(), 1);
        QCOMPARE(readySpy.first().at(0).toULongLong(), id2);
    }

    void testDestructionWhileFetchesActive()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d/sample_js");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir);
        pkg.setTrusted(true);

        auto *mockNam = new FakeNetworkAccessManager(this);
        FakeNetworkReply *reply = nullptr;
        mockNam->handler = [&](const QNetworkRequest &req, QNetworkAccessManager::Operation) -> FakeNetworkReply * {
            reply = new FakeNetworkReply(req, mockNam);
            return reply;
        };

        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNam);
        auto *provider = new JsProvider(pkg, nullptr, hostService);
        QVERIFY(provider->isLoaded());

        provider->search(QStringLiteral("fetch_test"));
        QVERIFY(reply != nullptr);
        QVERIFY(!reply->isAborted());

        // Destroy provider: should abort active fetch without crashing
        delete provider;
        QVERIFY(reply->isAborted());
    }

    void testCloningForPerTabInstances()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d/sample_js");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir);
        pkg.setTrusted(true);

        auto master = std::make_unique<JsProvider>(pkg);
        QVERIFY(master->isLoaded());
        QPointer<ProviderHost::ProviderHostService> templateHost = master->findChild<ProviderHost::ProviderHostService *>();
        QVERIFY(templateHost);
        auto *cloned = master->clone(this);
        QVERIFY(cloned != nullptr);
        QCOMPARE(cloned->id(), QStringLiteral("sample_js"));

        auto *clonedJs = qobject_cast<JsProvider *>(cloned);
        QVERIFY(clonedJs != nullptr);
        QVERIFY(clonedJs->isLoaded());

        auto *cloneHost = clonedJs->findChild<ProviderHost::ProviderHostService *>();
        QVERIFY(cloneHost);
        QVERIFY(cloneHost->networkAccessManager() != templateHost->networkAccessManager());
        master.reset();
        QVERIFY(templateHost.isNull());
        // A clone remains usable after its template and transport are destroyed.
        QSignalSpy spy(clonedJs, &ProviderApiV1::ProviderV1::pageReady);
        const quint64 id = clonedJs->search(QStringLiteral("Plasma"));
        QVERIFY(spy.wait(500));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toULongLong(), id);

        delete cloned;
    }
};

QTEST_MAIN(TestJsProvider)
#include "test_jsprovider.moc"
