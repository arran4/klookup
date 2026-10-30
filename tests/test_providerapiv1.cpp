// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providerapiv1.h"
#include "searchtab.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTextBrowser>
#include <QTreeWidget>

using namespace ProviderApiV1;

class MockPlainLegacyProvider : public SearchProvider
{
    Q_OBJECT
public:
    explicit MockPlainLegacyProvider(QObject *parent = nullptr) : SearchProvider(parent) {}

    QString id() const override { return QStringLiteral("mock-plain"); }
    QString name() const override { return QStringLiteral("Mock Plain"); }

    void search(const QString &query) override
    {
        SearchResult item;
        item.id = QStringLiteral("plain-1");
        item.title = QStringLiteral("Plain Result for ") + query;
        emit searchCompleted(id(), {item});
    }

    SearchProvider *clone(QObject *parent = nullptr) const override
    {
        return new MockPlainLegacyProvider(parent);
    }
};

class MockLegacyProvider : public SearchProvider, public SearchProviderContinuation
{
    Q_OBJECT
    Q_INTERFACES(SearchProvider SearchProviderContinuation)
public:
    explicit MockLegacyProvider(QObject *parent = nullptr) : SearchProvider(parent) {}

    QString id() const override { return QStringLiteral("mock-legacy"); }
    QString name() const override { return QStringLiteral("Mock Legacy"); }

    void search(const QString &query) override
    {
        lastQuery = query;
        SearchResult item;
        item.id = QStringLiteral("res-1");
        item.title = QStringLiteral("Result for ") + query;
        emit searchCompleted(id(), {item});
    }

    SearchProvider *clone(QObject *parent = nullptr) const override
    {
        return new MockLegacyProvider(parent);
    }

    void requestPage(const QString &query, const QString &token, quint64 requestId) override
    {
        lastQuery = query;
        lastToken = token;
        lastRequestId = requestId;
        if (suppressResponses) return;

        SearchPage page;
        SearchResult item;
        item.id = token.isEmpty() ? QStringLiteral("res-1") : (QStringLiteral("paged-") + token);
        item.title = QStringLiteral("Paged Result");
        page.results = {item};
        page.nextToken = QStringLiteral("next-token");
        emit pageCompleted(requestId, page);
    }

    bool supportsDetails() const override { return true; }

    void requestDetails(const QString &resultId, const QString &token, quint64 requestId) override
    {
        Q_UNUSED(token);
        lastResultId = resultId;
        lastRequestId = requestId;
        if (suppressResponses) return;

        SearchDetails details;
        details.result.id = resultId;
        details.result.title = QStringLiteral("Details for ") + resultId;
        details.content.format = ResultContent::Format::Markdown;
        details.content.description = QStringLiteral("# Markdown Content for ") + resultId;
        emit detailsCompleted(requestId, details);
    }

    void cancelRequests() override
    {
        cancelled = true;
    }

    QString lastQuery;
    QString lastToken;
    QString lastResultId;
    quint64 lastRequestId = 0;
    bool suppressResponses = false;
    bool cancelled = false;
};

class MockV1Provider : public ProviderV1
{
    Q_OBJECT
public:
    explicit MockV1Provider(QObject *parent = nullptr) : ProviderV1(parent) {}

    QString id() const override { return QStringLiteral("mock-v1"); }
    QString name() const override { return QStringLiteral("Mock V1"); }
    QString description() const override { return QStringLiteral("Mock V1 Description"); }

    Capabilities capabilities() const override
    {
        Capabilities caps;
        caps.pagination = true;
        caps.details = true;
        caps.separateContent = true;
        caps.hierarchy = true;
        return caps;
    }

    ProviderV1 *clone(QObject *parent = nullptr) const override
    {
        auto *copy = new MockV1Provider(parent);
        return copy;
    }

    quint64 search(const QString &query, quint64 requestId = 0) override
    {
        const quint64 id = requestId != 0 ? requestId : ++counter;
        lastQuery = query;
        SearchPage page;
        SearchResult item;
        item.id = QStringLiteral("v1-res-1");
        item.title = QStringLiteral("V1 Result for ") + query;
        page.results = {item};
        emit pageReady(id, page);
        return id;
    }

    quint64 getPage(const QString &query, const QString &pageToken, quint64 requestId = 0) override
    {
        const quint64 id = requestId != 0 ? requestId : ++counter;
        lastQuery = query;
        lastToken = pageToken;
        if (!suppressResponses) {
            SearchPage page;
            SearchResult item;
            item.id = pageToken.isEmpty() ? QStringLiteral("v1-res-1") : (QStringLiteral("v1-paged-") + pageToken);
            item.title = pageToken.isEmpty() ? (QStringLiteral("V1 Result for ") + query) : QStringLiteral("V1 Paged Result");
            page.results = {item};
            emit pageReady(id, page);
        }
        return id;
    }

    quint64 getDetails(const QString &resultId, quint64 requestId = 0) override
    {
        const quint64 id = requestId != 0 ? requestId : ++counter;
        lastResultId = resultId;
        SearchDetails det;
        det.result.id = resultId;
        det.result.title = QStringLiteral("V1 Details");
        emit detailsReady(id, resultId, det);
        return id;
    }

    quint64 getContent(const QString &resultId, const QString &contentToken = QString(), quint64 requestId = 0) override
    {
        Q_UNUSED(contentToken);
        const quint64 id = requestId != 0 ? requestId : ++counter;
        lastResultId = resultId;
        ContentResponse resp;
        resp.resultId = resultId;
        resp.content.format = ResultContent::Format::Markdown;
        resp.content.description = QStringLiteral("## Rich Content");
        emit contentReady(id, resp);
        return id;
    }

    void cancel(quint64 requestId) override
    {
        cancelledIds.append(requestId);
    }

    bool suppressResponses = false;
    quint64 counter = 0;
    QString lastQuery;
    QString lastToken;
    QString lastResultId;
    QList<quint64> cancelledIds;
};

class TestProviderApiV1 : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testJsonRpcEnvelopes()
    {
        QJsonObject params;
        params[QStringLiteral("query")] = QStringLiteral("Plasma 6");
        const auto req = toJsonRpcRequest(42, QStringLiteral("search"), params);
        QCOMPARE(req.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
        QCOMPARE(req.value(QStringLiteral("id")).toInt(), 42);
        QCOMPARE(req.value(QStringLiteral("method")).toString(), QStringLiteral("search"));
        QCOMPARE(req.value(QStringLiteral("params")).toObject().value(QStringLiteral("query")).toString(), QStringLiteral("Plasma 6"));

        QJsonObject result;
        result[QStringLiteral("status")] = QStringLiteral("ok");
        const auto resp = toJsonRpcSuccess(42, result);
        QCOMPARE(resp.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
        QCOMPARE(resp.value(QStringLiteral("id")).toInt(), 42);
        QCOMPARE(resp.value(QStringLiteral("result")).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("ok"));

        const auto err = toJsonRpcError(42, ErrorCode::Cancelled, QStringLiteral("Cancelled"));
        QCOMPARE(err.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
        QCOMPARE(err.value(QStringLiteral("id")).toInt(), 42);
        const auto errObj = err.value(QStringLiteral("error")).toObject();
        QCOMPARE(errObj.value(QStringLiteral("code")).toInt(), -32000);
        QCOMPARE(errObj.value(QStringLiteral("message")).toString(), QStringLiteral("Cancelled"));
    }

    void testSearchResultSerialization()
    {
        SearchResult res;
        res.id = QStringLiteral("plasma-6");
        res.title = QStringLiteral("KDE Plasma 6");
        res.description = QStringLiteral("Desktop Environment");
        res.url = QStringLiteral("https://kde.org/");
        res.type = QStringLiteral("Desktop");
        res.parentId = QStringLiteral("kde-root");
        res.additionalData[QStringLiteral("framework")] = QStringLiteral("KF6");

        SearchResult child;
        child.id = QStringLiteral("kwin");
        child.title = QStringLiteral("KWin");
        child.parentId = QStringLiteral("plasma-6");
        res.children.append(child);

        const auto json = toJson(res);
        bool ok = false;
        const auto roundtrip = searchResultFromJson(json, &ok);
        QVERIFY(ok);
        QCOMPARE(roundtrip.id, res.id);
        QCOMPARE(roundtrip.title, res.title);
        QCOMPARE(roundtrip.description, res.description);
        QCOMPARE(roundtrip.url, res.url);
        QCOMPARE(roundtrip.type, res.type);
        QCOMPARE(roundtrip.parentId, res.parentId);
        QCOMPARE(roundtrip.additionalData.value(QStringLiteral("framework")).toString(), QStringLiteral("KF6"));
        QCOMPARE(roundtrip.children.size(), 1);
        QCOMPARE(roundtrip.children.first().id, QStringLiteral("kwin"));
        QCOMPARE(roundtrip.children.first().parentId, QStringLiteral("plasma-6"));
    }

    void testSearchPageSerialization()
    {
        SearchPage page;
        SearchResult item1;
        item1.id = QStringLiteral("1");
        item1.title = QStringLiteral("One");
        page.results.append(item1);
        page.nextToken = QStringLiteral("cursor-token-123");

        ResultContent content;
        content.format = ResultContent::Format::Markdown;
        content.description = QStringLiteral("# Description One");
        page.content.insert(item1.id, content);

        const auto json = toJson(page);
        bool ok = false;
        const auto roundtrip = searchPageFromJson(json, &ok);
        QVERIFY(ok);
        QCOMPARE(roundtrip.results.size(), 1);
        QCOMPARE(roundtrip.results.first().title, QStringLiteral("One"));
        QCOMPARE(roundtrip.nextToken, QStringLiteral("cursor-token-123"));
        QVERIFY(roundtrip.content.contains(QStringLiteral("1")));
        QCOMPARE(roundtrip.content[QStringLiteral("1")].format, ResultContent::Format::Markdown);
        QCOMPARE(roundtrip.content[QStringLiteral("1")].description, QStringLiteral("# Description One"));
    }

    void testContentBlockAndResultContentSerialization()
    {
        ContentBlock h1;
        h1.type = ContentBlock::Type::Heading;
        h1.level = 2;
        h1.text = QStringLiteral("Heading 2");

        ContentBlock code;
        code.type = ContentBlock::Type::CodeBlock;
        code.language = QStringLiteral("cpp");
        code.text = QStringLiteral("int main() { return 0; }");

        ContentBlock table;
        table.type = ContentBlock::Type::Table;
        table.tableRows = {{QStringLiteral("Col1"), QStringLiteral("Col2")}, {QStringLiteral("A"), QStringLiteral("B")}};

        ContentBlock fields;
        fields.type = ContentBlock::Type::FieldList;
        fields.fields = {{QStringLiteral("Author"), QStringLiteral("KDE Team")}};

        ContentBlock img;
        img.type = ContentBlock::Type::Image;
        img.imageUrl = QUrl(QStringLiteral("https://kde.org/plasma.png"));
        img.caption = QStringLiteral("Plasma Preview");

        ResultContent content;
        content.format = ResultContent::Format::StructuredBlocks;
        content.blocks = {h1, code, table, fields, img};

        const auto json = toJson(content);
        bool ok = false;
        const auto roundtrip = resultContentFromJson(json, &ok);
        QVERIFY(ok);
        QCOMPARE(roundtrip.format, ResultContent::Format::StructuredBlocks);
        QCOMPARE(roundtrip.blocks.size(), 5);
        QCOMPARE(roundtrip.blocks[0].type, ContentBlock::Type::Heading);
        QCOMPARE(roundtrip.blocks[0].level, 2);
        QCOMPARE(roundtrip.blocks[1].type, ContentBlock::Type::CodeBlock);
        QCOMPARE(roundtrip.blocks[1].language, QStringLiteral("cpp"));
        QCOMPARE(roundtrip.blocks[2].type, ContentBlock::Type::Table);
        QCOMPARE(roundtrip.blocks[2].tableRows.size(), 2);
        QCOMPARE(roundtrip.blocks[3].type, ContentBlock::Type::FieldList);
        QCOMPARE(roundtrip.blocks[3].fields.first().second, QStringLiteral("KDE Team"));
        QCOMPARE(roundtrip.blocks[4].type, ContentBlock::Type::Image);
        QCOMPARE(roundtrip.blocks[4].caption, QStringLiteral("Plasma Preview"));
    }

    void testMetadataSerialization()
    {
        ResultMetadata meta;
        meta.attribution = {QStringLiteral("KDE Community")};
        MetadataNode sec;
        sec.label = QStringLiteral("General");
        MetadataValue val;
        val.kind = MetadataValue::Kind::Link;
        val.text = QStringLiteral("KDE Home");
        val.url = QUrl(QStringLiteral("https://kde.org/"));
        sec.values.append(val);
        meta.sections.append(sec);

        const auto json = toJson(meta);
        bool ok = false;
        const auto roundtrip = resultMetadataFromJson(json, &ok);
        QVERIFY(ok);
        QCOMPARE(roundtrip.attribution, QStringList{QStringLiteral("KDE Community")});
        QCOMPARE(roundtrip.sections.size(), 1);
        QCOMPARE(roundtrip.sections.first().label, QStringLiteral("General"));
        QCOMPARE(roundtrip.sections.first().values.size(), 1);
        QCOMPARE(roundtrip.sections.first().values.first().kind, MetadataValue::Kind::Link);
        QCOMPARE(roundtrip.sections.first().values.first().url.toString(), QStringLiteral("https://kde.org/"));
    }

    void testProtocolFixtures()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/provider_api_v1");

        // 1. Search Request
        QFile reqFile(fixtureDir + QStringLiteral("/search_request.json"));
        QVERIFY(reqFile.open(QIODevice::ReadOnly));
        auto doc = QJsonDocument::fromJson(reqFile.readAll());
        QVERIFY(!doc.isNull());
        QCOMPARE(doc.object().value(QStringLiteral("method")).toString(), QStringLiteral("search"));
        QCOMPARE(doc.object().value(QStringLiteral("params")).toObject().value(QStringLiteral("query")).toString(), QStringLiteral("Plasma 6"));

        // 2. Search Response
        QFile respFile(fixtureDir + QStringLiteral("/search_response.json"));
        QVERIFY(respFile.open(QIODevice::ReadOnly));
        doc = QJsonDocument::fromJson(respFile.readAll());
        QVERIFY(!doc.isNull());
        bool ok = false;
        auto page = searchPageFromJson(doc.object().value(QStringLiteral("result")).toObject(), &ok);
        QVERIFY(ok);
        QCOMPARE(page.results.size(), 2);
        QCOMPARE(page.results.first().id, QStringLiteral("item-plasma6"));
        QCOMPARE(page.results.first().children.size(), 1);
        QCOMPARE(page.results.first().children.first().id, QStringLiteral("item-kwin"));
        QCOMPARE(page.nextToken, QStringLiteral("page-cursor-token-2"));

        // 3. Get Details Response
        QFile detailsFile(fixtureDir + QStringLiteral("/get_details_response.json"));
        QVERIFY(detailsFile.open(QIODevice::ReadOnly));
        doc = QJsonDocument::fromJson(detailsFile.readAll());
        QVERIFY(!doc.isNull());
        auto details = searchDetailsFromJson(doc.object().value(QStringLiteral("result")).toObject(), &ok);
        QVERIFY(ok);
        QCOMPARE(details.result.id, QStringLiteral("item-plasma6"));
        QCOMPARE(details.metadata.attribution, QStringList{QStringLiteral("KDE Community")});
        QCOMPARE(details.metadata.sections.size(), 1);

        // 4. Get Content Response
        QFile contentFile(fixtureDir + QStringLiteral("/get_content_response.json"));
        QVERIFY(contentFile.open(QIODevice::ReadOnly));
        doc = QJsonDocument::fromJson(contentFile.readAll());
        QVERIFY(!doc.isNull());
        auto contentResp = contentResponseFromJson(doc.object().value(QStringLiteral("result")).toObject(), &ok);
        QVERIFY(ok);
        QCOMPARE(contentResp.resultId, QStringLiteral("item-plasma6"));
        QCOMPARE(contentResp.content.format, ResultContent::Format::Markdown);
        QVERIFY(contentResp.content.description.contains(QStringLiteral("Plasma 6 represents")));
    }

    void testLegacyProviderV1Adapter()
    {
        // 1. Plain legacy provider (searchCompleted without continuation)
        auto *plainLegacy = new MockPlainLegacyProvider(this);
        LegacyProviderV1Adapter plainAdapter(plainLegacy, this);
        QCOMPARE(plainAdapter.id(), QStringLiteral("mock-plain"));
        QVERIFY(!plainAdapter.capabilities().pagination);
        QVERIFY(!plainAdapter.capabilities().details);

        QSignalSpy plainSpy(&plainAdapter, &ProviderV1::pageReady);
        const quint64 plainSearchId = plainAdapter.search(QStringLiteral("plasma"));
        QCOMPARE(plainSpy.count(), 1);
        QCOMPARE(plainSpy.first().at(0).toULongLong(), plainSearchId);
        const auto plainPage = plainSpy.first().at(1).value<SearchPage>();
        QCOMPARE(plainPage.results.size(), 1);
        QCOMPARE(plainPage.results.first().id, QStringLiteral("plain-1"));

        // 2. Continuation legacy provider
        auto *legacy = new MockLegacyProvider(this);
        LegacyProviderV1Adapter adapter(legacy, this);

        QCOMPARE(adapter.id(), QStringLiteral("mock-legacy"));
        QCOMPARE(adapter.name(), QStringLiteral("Mock Legacy"));
        QVERIFY(adapter.capabilities().pagination);
        QVERIFY(adapter.capabilities().details);

        // Test search
        QSignalSpy pageSpy(&adapter, &ProviderV1::pageReady);
        const quint64 searchId = adapter.search(QStringLiteral("test search"));
        QCOMPARE(pageSpy.count(), 1);
        QCOMPARE(pageSpy.first().at(0).toULongLong(), searchId);
        const auto page = pageSpy.first().at(1).value<SearchPage>();
        QCOMPARE(page.results.size(), 1);
        QCOMPARE(page.results.first().id, QStringLiteral("res-1"));

        // Test getPage
        pageSpy.clear();
        const quint64 pageId = adapter.getPage(QStringLiteral("test"), QStringLiteral("tok1"));
        QCOMPARE(pageSpy.count(), 1);
        QCOMPARE(pageSpy.first().at(0).toULongLong(), pageId);
        const auto paged = pageSpy.first().at(1).value<SearchPage>();
        QCOMPARE(paged.results.first().id, QStringLiteral("paged-tok1"));

        // Test getDetails
        QSignalSpy detailsSpy(&adapter, &ProviderV1::detailsReady);
        const quint64 detailsId = adapter.getDetails(QStringLiteral("res-1"));
        QCOMPARE(detailsSpy.count(), 1);
        QCOMPARE(detailsSpy.first().at(0).toULongLong(), detailsId);
        const auto details = detailsSpy.first().at(2).value<SearchDetails>();
        QCOMPARE(details.result.id, QStringLiteral("res-1"));

        // Test getContent (served from cached details)
        QSignalSpy contentSpy(&adapter, &ProviderV1::contentReady);
        const quint64 contentId = adapter.getContent(QStringLiteral("res-1"));
        QCOMPARE(contentSpy.count(), 1);
        QCOMPARE(contentSpy.first().at(0).toULongLong(), contentId);
        const auto contentResp = contentSpy.first().at(1).value<ContentResponse>();
        QCOMPARE(contentResp.resultId, QStringLiteral("res-1"));
        QCOMPARE(contentResp.content.format, ResultContent::Format::Markdown);
        QVERIFY(contentResp.content.description.contains(QStringLiteral("Markdown Content for res-1")));

        // Test cancellation
        pageSpy.clear();
        legacy->suppressResponses = true;
        const quint64 pendingId = adapter.getPage(QStringLiteral("test"), QStringLiteral("pending"));
        QCOMPARE(pageSpy.count(), 0);
        adapter.cancel(pendingId);
        QVERIFY(legacy->cancelled);
        // If legacy sends response after cancellation, adapter must drop it
        SearchPage dropped;
        dropped.results = {SearchResult{}};
        emit legacy->pageCompleted(legacy->lastRequestId, dropped);
        QCOMPARE(pageSpy.count(), 0);
    }

    void testProviderV1ToLegacyAdapter()
    {
        auto *v1 = new MockV1Provider(this);
        ProviderV1ToLegacyAdapter legacyAdapter(v1, this);

        QCOMPARE(legacyAdapter.id(), QStringLiteral("mock-v1"));
        QCOMPARE(legacyAdapter.name(), QStringLiteral("Mock V1"));
        QVERIFY(legacyAdapter.supportsDetails());

        // Test search
        QSignalSpy searchSpy(&legacyAdapter, &SearchProvider::searchCompleted);
        legacyAdapter.search(QStringLiteral("plasma"));
        QCOMPARE(searchSpy.count(), 1);
        const auto resList = searchSpy.first().at(1).value<QList<SearchResult>>();
        QCOMPARE(resList.size(), 1);
        QCOMPARE(resList.first().id, QStringLiteral("v1-res-1"));

        // Test requestPage
        QSignalSpy pageSpy(&legacyAdapter, &SearchProvider::pageCompleted);
        legacyAdapter.requestPage(QStringLiteral("plasma"), QStringLiteral("tok-abc"), 99);
        QCOMPARE(pageSpy.count(), 1);
        QCOMPARE(pageSpy.first().at(0).toULongLong(), 99ULL);
        const auto page = pageSpy.first().at(1).value<SearchPage>();
        QCOMPARE(page.results.first().id, QStringLiteral("v1-paged-tok-abc"));

        // Test requestDetails
        QSignalSpy detailsSpy(&legacyAdapter, &SearchProvider::detailsCompleted);
        legacyAdapter.requestDetails(QStringLiteral("v1-res-1"), QString(), 101);
        QCOMPARE(detailsSpy.count(), 1);
        QCOMPARE(detailsSpy.first().at(0).toULongLong(), 101ULL);
        const auto det = detailsSpy.first().at(1).value<SearchDetails>();
        QCOMPARE(det.result.id, QStringLiteral("v1-res-1"));

        // Test cloning
        auto *clonedAdapter = legacyAdapter.clone(this);
        QVERIFY(clonedAdapter != nullptr);
        QCOMPARE(clonedAdapter->id(), QStringLiteral("mock-v1"));
        delete clonedAdapter;

        // Test factory cloning
        ProviderV1ToLegacyAdapter factoryAdapter([](QObject *p) { return new MockV1Provider(p); }, this);
        auto *clonedFromFactory = factoryAdapter.clone(this);
        QVERIFY(clonedFromFactory != nullptr);
        QCOMPARE(clonedFromFactory->id(), QStringLiteral("mock-v1"));
        delete clonedFromFactory;

        // Test cancel
        v1->suppressResponses = true;
        legacyAdapter.requestPage(QStringLiteral("plasma"), QStringLiteral("cancel-tok"), 202);
        legacyAdapter.cancelRequests();
        QVERIFY(v1->cancelledIds.contains(202));
    }

    void testSearchTabLifecycleWithV1Adapter()
    {
        // Prove ProviderV1ToLegacyAdapter genuinely works in the normal SearchTab lifecycle
        ProviderV1ToLegacyAdapter templateAdapter([](QObject *p) { return new MockV1Provider(p); });

        SearchTab tab(QStringLiteral("plasma"));

        // Normal SearchTab startup clones the template
        auto *tabProvider = templateAdapter.clone(&tab);
        QVERIFY(tabProvider != nullptr);
        tab.startProvider(tabProvider, nullptr);

        auto *tree = tab.findChild<QTreeWidget *>();
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->topLevelItemCount(), 1);

        QTreeWidgetItem *group = tree->topLevelItem(0);
        QCOMPARE(group->text(0), QStringLiteral("Mock V1"));
        QVERIFY(group->childCount() >= 1);

        QTreeWidgetItem *item = group->child(0);
        QCOMPARE(item->text(0), QStringLiteral("V1 Result for plasma"));

        // Click result to trigger details loading
        emit tree->itemClicked(item, 0);

        auto *detailsView = tab.findChild<QTextBrowser *>();
        QVERIFY(detailsView != nullptr);
        QVERIFY(detailsView->toHtml().contains(QStringLiteral("V1 Details")));

        // Since MockV1Provider has separateContent = true, "Load more content" / nextToken is active
        auto *loadBtn = tab.findChild<QPushButton *>(QStringLiteral("loadDetails"));
        QVERIFY(loadBtn != nullptr);
        QVERIFY(!loadBtn->isHidden());
        QVERIFY(loadBtn->text().contains(QStringLiteral("Load more content")));

        // Click "Load more content" to fetch separate content
        emit loadBtn->clicked();
        QVERIFY(detailsView->toHtml().contains(QStringLiteral("Rich Content")));
    }
};

QTEST_MAIN(TestProviderApiV1)
#include "test_providerapiv1.moc"
