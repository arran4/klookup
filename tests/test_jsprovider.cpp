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
