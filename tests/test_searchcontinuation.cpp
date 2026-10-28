#include "../src/searchcontinuation.h"
#include "../src/searchtab.h"
#include <QMenu>
#include <QPushButton>
#include <QtTest>

class ContinuationMock : public SearchProvider, public SearchProviderContinuation
{
    Q_OBJECT
    Q_INTERFACES(SearchProvider SearchProviderContinuation)
public:
    explicit ContinuationMock(const QString &id, QObject *parent = nullptr) : SearchProvider(parent), providerId(id) {}
    QString providerId;
    QString name() const override
    {
        return QStringLiteral("Shared display name");
    }
    QString id() const override
    {
        return providerId;
    }
    void search(const QString &) override
    {
        QFAIL("Continuation providers must use requestPage");
    }
    SearchProvider *clone(QObject *parent) const override
    {
        return new ContinuationMock(providerId, parent);
    }
    struct Request {
        QString query;
        QString token;
        quint64 id;
    };
    QList<Request> pages;
    QList<Request> details;
    bool *cancelled = nullptr;
    bool detailSupport = true;
    void requestPage(const QString &query, const QString &token, quint64 id) override
    {
        pages.append({query, token, id});
    }
    bool supportsDetails() const override
    {
        return detailSupport;
    }
    void requestDetails(const QString &id, const QString &token, quint64 request) override
    {
        details.append({id, token, request});
    }
    void cancelRequests() override
    {
        if (cancelled)
            *cancelled = true;
    }
    static SearchResult result(const QString &id, const QString &description = {})
    {
        SearchResult result;
        result.id = id;
        result.title = QStringLiteral("Result ") + id;
        result.description = description;
        return result;
    }
    void finish(const QStringList &ids, const QString &token = {})
    {
        SearchPage page;
        page.nextToken = token;
        for (const auto &id : ids)
            page.results.append(result(id));
        emit pageCompleted(pages.last().id, page);
    }
};

class LegacyMock : public SearchProvider
{
    Q_OBJECT
public:
    QString name() const override
    {
        return QStringLiteral("Legacy");
    }
    QString id() const override
    {
        return QStringLiteral("legacy");
    }
    SearchProvider *clone(QObject *parent) const override
    {
        auto *p = new LegacyMock;
        p->setParent(parent);
        return p;
    }
    void search(const QString &) override
    {
        SearchResult result;
        result.title = QStringLiteral("ID-less result");
        emit searchCompleted(id(), {result});
    }
};

class SearchContinuationTest : public QObject
{
    Q_OBJECT
private slots:
    void pagesDeduplicatePreserveOrderAndIsolateProviders()
    {
        SearchTab tab(QStringLiteral("query"));
        auto *alpha = new ContinuationMock(QStringLiteral("alpha"));
        auto *beta = new ContinuationMock(QStringLiteral("beta"));
        tab.startProvider(alpha);
        tab.startProvider(beta);
        QCOMPARE(alpha->pages.size(), 1);
        QCOMPARE(beta->pages.size(), 1);
        tab.loadMore(alpha->id());
        QCOMPARE(alpha->pages.size(), 1); // in-flight requests coalesce
        alpha->finish({QStringLiteral("1"), QStringLiteral("2")}, QStringLiteral("opaque cursor /?"));
        beta->finish({QStringLiteral("1")});
        auto *tree = tab.findChild<QTreeWidget *>();
        QCOMPARE(tree->topLevelItemCount(), 2); // identity is independent of display name
        auto *group = tree->topLevelItem(0);
        QCOMPARE(group->childCount(), 3); // two results + continuation control
        tab.loadMore(alpha->id());
        QCOMPARE(alpha->pages.last().token, QStringLiteral("opaque cursor /?"));
        QCOMPARE(alpha->pages.last().query, QStringLiteral("query"));
        QCOMPARE(beta->pages.size(), 1);
        alpha->finish({QStringLiteral("2"), QStringLiteral("3")}, QStringLiteral("20"));
        QCOMPARE(group->childCount(), 4);
        QCOMPARE(group->child(2)->text(0), QStringLiteral("Result 3"));
        tab.loadMore(alpha->id());
        QCOMPARE(alpha->pages.last().token, QStringLiteral("20"));
        alpha->finish({QStringLiteral("4")});
        tab.loadMore(alpha->id());
        QCOMPARE(alpha->pages.size(), 3);
        QCOMPARE(group->childCount(), 5);
        QCOMPARE(group->child(4)->text(0), QStringLiteral("No more results"));
    }
    void errorRetryKeepsTokenAndRejectsStaleResponses()
    {
        SearchTab tab(QStringLiteral("query"));
        auto *provider = new ContinuationMock(QStringLiteral("alpha"));
        tab.startProvider(provider);
        const auto first = provider->pages.last().id;
        provider->finish({QStringLiteral("1")}, QStringLiteral("cursor"));
        tab.loadMore(provider->id());
        auto *tree = tab.findChild<QTreeWidget *>();
        emit provider->pageCompleted(first, SearchPage{{ContinuationMock::result(QStringLiteral("stale"))}, {}});
        QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
        emit provider->requestFailed(provider->pages.last().id, QStringLiteral("HTTP 429"), 30);
        tab.loadMore(provider->id());
        QCOMPARE(provider->pages.size(), 2);
        QTest::qWait(40);
        tab.loadMore(provider->id());
        QCOMPARE(provider->pages.size(), 3);
        QCOMPARE(provider->pages.last().token, QStringLiteral("cursor"));
        provider->finish({QStringLiteral("2")});
        QCOMPARE(tree->topLevelItem(0)->childCount(), 3);
    }
    void detailsDoNotOverwriteChangedSelectionAndContinueIndependently()
    {
        SearchTab tab(QStringLiteral("query"));
        auto *provider = new ContinuationMock(QStringLiteral("alpha"));
        tab.startProvider(provider);
        provider->finish({QStringLiteral("1"), QStringLiteral("2")}, QStringLiteral("next"));
        auto *tree = tab.findChild<QTreeWidget *>();
        auto *group = tree->topLevelItem(0);
        tree->setCurrentItem(group->child(0));
        tab.loadDetails();
        tab.loadDetails();
        QCOMPARE(provider->details.size(), 1);
        tab.loadMore(provider->id()); // page and detail lanes are independent
        QCOMPARE(provider->pages.size(), 2);
        tree->setCurrentItem(group->child(1));
        emit provider->detailsCompleted(
            provider->details.last().id,
            SearchDetails{ContinuationMock::result(QStringLiteral("1"), QStringLiteral("Full first description")),
                          QStringLiteral("chapter two")});
        auto *view = tab.findChild<QTextBrowser *>();
        QVERIFY(!view->toPlainText().contains(QStringLiteral("Full first description")));
        QCOMPARE(provider->details.last().query, QStringLiteral("2"));
        emit provider->detailsCompleted(
            provider->details.last().id,
            SearchDetails{ContinuationMock::result(QStringLiteral("2"), QStringLiteral("Full second description")), {}});
        tree->setCurrentItem(group->child(0));
        QVERIFY(view->toPlainText().contains(QStringLiteral("Full first description")));
        auto *button = tab.findChild<QPushButton *>(QStringLiteral("loadDetails"));
        QCOMPARE(button->text(), QStringLiteral("Load more content"));
        tab.loadDetails();
        QCOMPARE(provider->details.last().token, QStringLiteral("chapter two"));
        emit provider->requestFailed(provider->details.last().id, QStringLiteral("Detail timeout"));
        QVERIFY(view->toPlainText().contains(QStringLiteral("Detail timeout")));
        tab.loadDetails();
        QCOMPARE(provider->details.size(), 4);
        emit provider->detailsCompleted(
            provider->details.last().id,
            SearchDetails{ContinuationMock::result(QStringLiteral("1"), QStringLiteral("Complete content")), {}});
        QVERIFY(button->isHidden());
        QCOMPARE(provider->pages.size(), 2);
    }
    void unsupportedAndEmptyAndRepeatedTokens()
    {
        SearchTab tab(QStringLiteral("query"));
        auto *provider = new ContinuationMock(QStringLiteral("alpha"));
        provider->detailSupport = false;
        tab.startProvider(provider);
        provider->finish({QStringLiteral("1")}, QStringLiteral("cursor"));
        auto *tree = tab.findChild<QTreeWidget *>();
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        tab.loadDetails();
        QVERIFY(provider->details.isEmpty());
        QVERIFY(tab.findChild<QPushButton *>(QStringLiteral("loadDetails"))->isHidden());
        tab.loadMore(provider->id());
        provider->finish({}, QStringLiteral("cursor"));
        tab.loadMore(provider->id());
        QCOMPARE(provider->pages.size(), 2); // repeated token cannot loop
        auto *empty = new ContinuationMock(QStringLiteral("empty"));
        tab.startProvider(empty);
        empty->finish({});
        QCOMPARE(tree->topLevelItem(1)->child(0)->text(0), QStringLiteral("No results"));
    }
    void nestedMetadataHasTotalBudget()
    {
        QVariantList leaves;
        for (int i = 0; i < 200; ++i)
            leaves.append(QStringLiteral("value"));
        QVariantMap branches;
        for (int i = 0; i < 200; ++i)
            branches.insert(QString::number(i), leaves);
        const auto node = Metadata::fromVariant(QStringLiteral("Large metadata"), branches);
        int count = 0;
        std::function<void(const MetadataNode &)> visit = [&](const MetadataNode &entry) {
            count += 1 + entry.values.size();
            for (const auto &child : entry.children)
                visit(child);
        };
        visit(node);
        QVERIFY(count < 4100);
        QVERIFY(!node.children.isEmpty());
        QVERIFY(node.children.last().label.contains(QStringLiteral("limit")));
    }
    void typedMetadataIsCachedAndSafelyRendered()
    {
        SearchTab tab(QStringLiteral("query"));
        auto *provider = new ContinuationMock(QStringLiteral("alpha"));
        tab.startProvider(provider);
        provider->finish({QStringLiteral("1")});
        auto *tree = tab.findChild<QTreeWidget *>();
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        QCOMPARE(provider->details.size(), 1); // selected result only
        ResultMetadata metadata;
        MetadataNode category{QStringLiteral("Activities"), {}, {}};
        category.children.append(
            Metadata::text(QStringLiteral("Leisure"), {QStringLiteral("Travel"), QStringLiteral("<script>untrusted</script>")}));
        MetadataNode sources{QStringLiteral("Sources"), {}, {}};
        sources.values = {
            MetadataValue{QStringLiteral("Safe source"), QUrl(QStringLiteral("https://example.org/source")), MetadataValue::Kind::Link},
            MetadataValue{QStringLiteral("Untrusted source"), QUrl(QStringLiteral("javascript:alert(1)")), MetadataValue::Kind::Link}};
        metadata.sections = {category, sources};
        metadata.attribution = {QStringLiteral("Provider credit")};
        emit provider->detailsCompleted(provider->details.last().id,
                                        SearchDetails{ContinuationMock::result(QStringLiteral("1")), {}, metadata});
        auto *view = tab.findChild<QTreeWidget *>(QStringLiteral("structuredMetadata"));
        QVERIFY(view);
        QCOMPARE(view->topLevelItemCount(), 3);
        QCOMPARE(view->topLevelItem(0)->text(0), QStringLiteral("Activities"));
        QVERIFY(view->topLevelItem(0)->child(0)->text(1).contains(QStringLiteral("<script>untrusted</script>")));
        QVERIFY(!view->topLevelItem(0)->child(0)->toolTip(1).contains(QStringLiteral("<script>")));
        QVERIFY(view->topLevelItem(0)->child(0)->toolTip(1).contains(QStringLiteral("&lt;script&gt;")));
        QCOMPARE(view->topLevelItem(1)->childCount(), 1); // unsafe URL becomes text, never a link
        QCOMPARE(view->topLevelItem(1)->child(0)->data(1, Qt::UserRole).toString(), QStringLiteral("https://example.org/source"));
        QCOMPARE(view->topLevelItem(2)->text(1), QStringLiteral("Provider credit"));
        tree->setCurrentItem(tree->topLevelItem(0));
        QVERIFY(view->isHidden());
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        QCOMPARE(provider->details.size(), 1); // cache reuse
        QCOMPARE(view->topLevelItemCount(), 3);
    }
    void refreshIsResultSpecificAndRespectsRequests()
    {
        SearchTab tab(QStringLiteral("query"));
        auto *provider = new ContinuationMock(QStringLiteral("alpha"));
        auto *other = new ContinuationMock(QStringLiteral("beta"));
        tab.startProvider(provider);
        tab.startProvider(other);
        provider->finish({QStringLiteral("1"), QStringLiteral("2")});
        other->finish({QStringLiteral("other")});
        auto *tree = tab.findChild<QTreeWidget *>();
        auto *target = tree->topLevelItem(0)->child(1);
        tree->setCurrentItem(target);
        // Structured metadata can request initial details on selection. Finish
        // that request before exercising an explicit refresh of the same row.
        const auto initialDetails = provider->details.size();
        if (initialDetails) {
            QCOMPARE(provider->details.last().query, QStringLiteral("2"));
            emit provider->detailsCompleted(provider->details.last().id, SearchDetails{ContinuationMock::result(QStringLiteral("2")), {}});
        }
        auto *menu = tab.createResultMenu(target);
        auto *refresh = menu->findChild<QAction *>(QStringLiteral("refresh_details"));
        QVERIFY(refresh->isEnabled());
        refresh->trigger();
        QCOMPARE(provider->details.size(), initialDetails + 1);
        QCOMPARE(provider->details.last().query, QStringLiteral("2"));
        QCOMPARE(provider->pages.size(), 1);
        QCOMPARE(other->pages.size(), 1);
        QVERIFY(other->details.isEmpty());
        auto *busy = tab.createResultMenu(target);
        QVERIFY(!busy->findChild<QAction *>(QStringLiteral("refresh_details"))->isEnabled());
        emit provider->requestFailed(provider->details.last().id, QStringLiteral("Refresh failed"));
        QVERIFY(tab.findChild<QTextBrowser *>()->toPlainText().contains(QStringLiteral("Refresh failed")));
        auto *retry = tab.createResultMenu(target);
        QVERIFY(retry->findChild<QAction *>(QStringLiteral("refresh_details"))->isEnabled());
        retry->findChild<QAction *>(QStringLiteral("refresh_details"))->trigger();
        QCOMPARE(provider->details.size(), initialDetails + 2);
        emit provider->detailsCompleted(
            provider->details.last().id,
            SearchDetails{ContinuationMock::result(QStringLiteral("2"), QStringLiteral("Refreshed content")), {}});
        QCOMPARE(tree->currentItem(), target);
        QVERIFY(tab.findChild<QTextBrowser *>()->toPlainText().contains(QStringLiteral("Refreshed content")));
    }

    void legacyAndTabDestruction()
    {
        auto *tab = new SearchTab(QStringLiteral("query"));
        QPointer<LegacyMock> legacy = new LegacyMock;
        tab->startProvider(legacy);
        auto *tree = tab->findChild<QTreeWidget *>();
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("ID-less result"));
        bool cancelled = false;
        QPointer<ContinuationMock> provider = new ContinuationMock(QStringLiteral("pending"));
        provider->cancelled = &cancelled;
        tab->startProvider(provider);
        delete tab;
        QVERIFY(cancelled);
        QVERIFY(provider.isNull());
        QVERIFY(legacy.isNull());
    }
};
QTEST_MAIN(SearchContinuationTest)
#include "test_searchcontinuation.moc"
