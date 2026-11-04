#include "../src/searchprovider.h"
#include "../src/searchtab.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QLabel>
#include <QMouseEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTreeWidget>

class SearchTabTest : public QObject
{
    Q_OBJECT

private slots:
    void testProviderDisplayName()
    {
        SearchTab tab("test query");

        QList<SearchResult> results;
        SearchResult res;
        res.title = "Result 1";
        results.append(res);

        // Emulate the lambda in MainWindow::performSearch which passes providerName instead of ID
        QString providerName = "Friendly Display Name";
        tab.addResults(providerName, results);

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget *>();
        QVERIFY(treeWidget);

        QCOMPARE(treeWidget->topLevelItemCount(), 1);
        QTreeWidgetItem *providerItem = treeWidget->topLevelItem(0);
        QCOMPARE(providerItem->text(0), providerName);
        QCOMPARE(providerItem->childCount(), 1);
        QCOMPARE(providerItem->child(0)->text(0), QString("Result 1"));
    }

    void testProviderDisplayNameError()
    {
        SearchTab tab("test query 2");

        QString providerName = "Another Friendly Name";
        tab.setError(providerName, "Connection failed");

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget *>();
        QVERIFY(treeWidget);

        QCOMPARE(treeWidget->topLevelItemCount(), 1);
        QTreeWidgetItem *providerItem = treeWidget->topLevelItem(0);
        QCOMPARE(providerItem->text(0), providerName);
        QCOMPARE(providerItem->childCount(), 1);
        QVERIFY(providerItem->child(0)->text(0).contains("Connection failed"));
    }

    void testPlainDescription()
    {
        SearchTab tab("test query");
        QList<SearchResult> results;
        SearchResult res;
        res.title = "Result 1";
        res.description = "A normal, ordinary description with punctuation: period. comma, etc!";
        results.append(res);
        tab.addResults("Provider", results);

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget *>();
        QTreeWidgetItem *providerItem = treeWidget->topLevelItem(0);
        QTreeWidgetItem *childItem = providerItem->child(0);

        QTextBrowser *detailsView = tab.findChild<QTextBrowser *>();
        QVERIFY(detailsView);

        emit treeWidget->itemClicked(childItem, 0);

        QTextDocument *doc = detailsView->document();
        QString plainText = doc->toPlainText();
        QVERIFY(plainText.contains(res.description));
    }

    void testDescriptionEscaping()
    {
        SearchTab tab("test query");
        QList<SearchResult> results;
        SearchResult res;
        res.title = "Result 1";
        res.description = "<script>alert(1);</script> & \"some text\"";
        results.append(res);
        tab.addResults("Provider", results);

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget *>();
        QTreeWidgetItem *providerItem = treeWidget->topLevelItem(0);
        QTreeWidgetItem *childItem = providerItem->child(0);

        QTextBrowser *detailsView = tab.findChild<QTextBrowser *>();
        QVERIFY(detailsView);

        // Emit itemClicked signal manually or just call the slot if possible.
        // We can simulate a click on the tree widget.
        emit treeWidget->itemClicked(childItem, 0);

        QString html = detailsView->toHtml();
        QVERIFY(!html.contains("<script>"));
        QVERIFY(html.contains("&lt;script&gt;alert(1);&lt;/script&gt;"));
    }

    void testUrlValidation()
    {
        SearchTab tab("test query");

        struct TestCase {
            QString url;
            bool shouldBeClickable;
        };

        QList<TestCase> cases = {
            {"https://example.com/test?q=1", true},
            {"http://example.com", true},
            {"https://user:token@example.com/", false},
            {"javascript:alert(1)", false},
            {"file:///etc/passwd", false},
            {"https://example.com\" onclick=\"alert(1)", false}, // Invalid URL due to spaces and quotes when parsed by QUrl
            {"", false},
            {"malformed::/url", false},
            {"https://", false},          // Valid scheme, but no host
            {"http:", false},             // Valid scheme, no host
            {"https:///path/only", false} // Valid scheme, no host
        };

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget *>();
        QTextBrowser *detailsView = tab.findChild<QTextBrowser *>();

        for (const TestCase &tc : cases) {
            QList<SearchResult> results;
            SearchResult res;
            res.title = "Result";
            res.url = tc.url;
            results.append(res);
            tab.addResults("Provider", results);

            QTreeWidgetItem *providerItem = treeWidget->findItems("Provider", Qt::MatchExactly).first();
            QTreeWidgetItem *childItem = providerItem->child(providerItem->childCount() - 1);

            emit treeWidget->itemClicked(childItem, 0);

            QTextDocument *doc = detailsView->document();
            bool foundAnchor = false;
            QString anchorHref;

            for (QTextBlock block = doc->begin(); block != doc->end(); block = block.next()) {
                for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                    QTextFragment fragment = it.fragment();
                    QTextCharFormat format = fragment.charFormat();
                    if (format.isAnchor()) {
                        foundAnchor = true;
                        anchorHref = format.anchorHref();
                    }
                }
            }

            if (tc.shouldBeClickable) {
                QVERIFY2(foundAnchor, "Expected to find an anchor in the rendered document");
                QUrl qurl(tc.url, QUrl::StrictMode);
                QString expectedHref = QString::fromUtf8(qurl.toEncoded());
                QCOMPARE(anchorHref, expectedHref);
            } else {
                QVERIFY2(!foundAnchor, "Expected NO anchor for invalid/unsupported URL");
            }
        }
    }

    void testProviderMetadata()
    {
        SearchTab tab("test query");
        QList<SearchResult> results;
        SearchResult res;
        res.title = "Result 1";
        res.additionalData["<b>Key</b>"] = "<script>alert(1)</script>";
        results.append(res);
        tab.addResults("Provider", results);

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget *>();
        QTreeWidgetItem *providerItem = treeWidget->topLevelItem(0);
        QTreeWidgetItem *childItem = providerItem->child(0);

        QTextBrowser *detailsView = tab.findChild<QTextBrowser *>();

        emit treeWidget->itemClicked(childItem, 0);

        QString html = detailsView->toHtml();
        QVERIFY(!html.contains("<b>Key</b>")); // it should be escaped
        QVERIFY(html.contains("&lt;b&gt;Key&lt;/b&gt;"));
        QVERIFY(!html.contains("<script>"));
        QVERIFY(html.contains("&lt;script&gt;"));
    }
    void nestedLegacyMetadataFallback()
    {
        SearchTab tab(QStringLiteral("query"));
        SearchResult result;
        result.title = QStringLiteral("Legacy result");
        result.additionalData.insert(QStringLiteral("Creators"), QStringList{QStringLiteral("One"), QStringLiteral("Two")});
        result.additionalData.insert(
            QStringLiteral("Tags"),
            QVariantMap{{QStringLiteral("Theme"), QVariantList{QStringLiteral("Journey"), QStringLiteral("<b>plain</b>")}}});
        tab.addResults(QStringLiteral("Provider"), {result});
        auto *tree = tab.findChild<QTreeWidget *>();
        emit tree->itemClicked(tree->topLevelItem(0)->child(0), 0);
        const auto plain = tab.findChild<QTextBrowser *>()->toPlainText();
        QVERIFY(plain.contains(QStringLiteral("One")));
        QVERIFY(plain.contains(QStringLiteral("Two")));
        QVERIFY(plain.contains(QStringLiteral("Theme")));
        QVERIFY(plain.contains(QStringLiteral("Journey")));
        QVERIFY(plain.contains(QStringLiteral("<b>plain</b>")));
        QVERIFY(Metadata::safeWebUrl(QStringLiteral("https://user:secret@example.org/")).isEmpty());
        QVERIFY(Metadata::safeWebUrl(QStringLiteral("file:///etc/passwd")).isEmpty());
    }

    void testEmptyStatePlaceholder()
    {
        SearchTab tab(QStringLiteral("test query"));
        tab.resize(800, 600);
        tab.show();

        auto *emptyWidget = tab.emptyStateWidget();
        QVERIFY(emptyWidget != nullptr);
        QVERIFY(emptyWidget->isVisible());
        QCOMPARE(emptyWidget->accessibleName(), QStringLiteral("No result selected"));
        QVERIFY(emptyWidget->accessibleDescription().contains(QStringLiteral("Select a search result")));

        auto *title = emptyWidget->findChild<QLabel *>(QStringLiteral("emptyStateTitle"));
        QVERIFY(title != nullptr);
        QCOMPARE(title->text(), QStringLiteral("No result selected"));

        auto *desc = emptyWidget->findChild<QLabel *>(QStringLiteral("emptyStateDescription"));
        QVERIFY(desc != nullptr);
        QVERIFY(desc->text().contains(QStringLiteral("Select a search result on the left")));

        SearchResult res;
        res.id = QStringLiteral("r1");
        res.title = QStringLiteral("Title 1");
        res.description = QStringLiteral("Desc 1");
        tab.addResults(QStringLiteral("Provider"), {res});

        auto *tree = tab.findChild<QTreeWidget *>();
        QVERIFY(tree != nullptr);
        auto *group = tree->topLevelItem(0);
        QVERIFY(group != nullptr && group->childCount() > 0);

        tree->setCurrentItem(group->child(0));
        emit tree->itemClicked(group->child(0), 0);

        QVERIFY(!emptyWidget->isVisible());
        auto *detailsView = tab.findChild<QTextBrowser *>();
        QVERIFY(detailsView != nullptr);
        QVERIFY(detailsView->toHtml().contains(QStringLiteral("Title 1")));

        emit tree->itemClicked(group, 0);
        QVERIFY(emptyWidget->isVisible());
    }

    void testSplitterResizingAndProportions()
    {
        SearchTab tab(QStringLiteral("test query"));
        tab.resize(800, 600);
        tab.show();

        auto *mainSplitter = tab.mainSplitter();
        QVERIFY(mainSplitter != nullptr);
        QCOMPARE(mainSplitter->orientation(), Qt::Horizontal);

        auto *detailSplitter = tab.detailSplitter();
        QVERIFY(detailSplitter != nullptr);
        QCOMPARE(detailSplitter->orientation(), Qt::Vertical);

        mainSplitter->setSizes({300, 500});
        QVERIFY(qAbs(mainSplitter->sizes()[0] - 300) <= 5);
        QVERIFY(qAbs(mainSplitter->sizes()[1] - 500) <= 5);

        auto *handle = mainSplitter->handle(1);
        QVERIFY(handle != nullptr);
        QTest::mouseDClick(handle, Qt::LeftButton);

        const auto resetSizes = mainSplitter->sizes();
        QCOMPARE(resetSizes.size(), 2);
        QVERIFY(resetSizes[0] > 0);
        QVERIFY(resetSizes[1] > resetSizes[0]);

        mainSplitter->setSizes({400, 400});
        QKeyEvent enterKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(handle, &enterKey);
        QCOMPARE(mainSplitter->sizes(), resetSizes);
    }

    void testPersistenceAcrossTabs()
    {
        KConfigGroup layoutGroup(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
        layoutGroup.deleteGroup();
        layoutGroup.sync();

        QList<int> saved;
        {
            SearchTab tab1(QStringLiteral("query 1"));
            tab1.resize(800, 600);
            tab1.show();

            tab1.mainSplitter()->setSizes({320, 480});
            tab1.saveLayout();

            saved = layoutGroup.readEntry(QStringLiteral("SearchTabSplitter"), QList<int>());
            QCOMPARE(saved.size(), 2);
            QVERIFY(qAbs(saved[0] - 320) <= 5);
            QVERIFY(qAbs(saved[1] - 480) <= 5);
        }

        {
            SearchTab tab2(QStringLiteral("query 2"));
            tab2.resize(800, 600);
            tab2.show();

            QCOMPARE(tab2.mainSplitter()->sizes(), saved);
        }

        layoutGroup.writeEntry(QStringLiteral("SearchTabSplitter"), QList<int>{-50, 0});
        layoutGroup.sync();

        {
            SearchTab tab3(QStringLiteral("query 3"));
            tab3.resize(800, 600);
            tab3.show();

            const auto fallbackSizes = tab3.mainSplitter()->sizes();
            QCOMPARE(fallbackSizes.size(), 2);
            QVERIFY(fallbackSizes[0] > 0);
            QVERIFY(fallbackSizes[1] > 0);
        }
    }

    void testNarrowLayout()
    {
        SearchTab tab(QStringLiteral("narrow query"));
        tab.resize(380, 400);
        tab.show();

        auto *mainSplitter = tab.mainSplitter();
        QVERIFY(mainSplitter != nullptr);

        SearchResult res;
        res.id = QStringLiteral("narrow-1");
        res.title = QStringLiteral("Narrow Result");
        tab.addResults(QStringLiteral("Provider"), {res});

        auto *tree = tab.findChild<QTreeWidget *>();
        QVERIFY(tree != nullptr);
        emit tree->itemClicked(tree->topLevelItem(0)->child(0), 0);

        auto *detailsView = tab.findChild<QTextBrowser *>();
        QVERIFY(detailsView != nullptr);
        QVERIFY(detailsView->isVisible());
        QVERIFY(detailsView->toHtml().contains(QStringLiteral("Narrow Result")));
        QVERIFY(mainSplitter->sizes()[0] > 0);
        QVERIFY(mainSplitter->sizes()[1] > 0);
    }

    void testAsynchronousUpdatesPreserveSelectionAndLayout()
    {
        SearchTab tab(QStringLiteral("async query"));
        tab.resize(800, 600);
        tab.show();

        SearchResult res1;
        res1.id = QStringLiteral("res-1");
        res1.title = QStringLiteral("First Result");
        tab.addResults(QStringLiteral("Provider A"), {res1});

        auto *tree = tab.findChild<QTreeWidget *>();
        QVERIFY(tree != nullptr);
        auto *item1 = tree->topLevelItem(0)->child(0);
        tree->setCurrentItem(item1);
        emit tree->itemClicked(item1, 0);

        tab.mainSplitter()->setSizes({340, 460});
        const auto expectedMainSizes = tab.mainSplitter()->sizes();

        SearchResult res2;
        res2.id = QStringLiteral("res-2");
        res2.title = QStringLiteral("Second Result");
        tab.addResults(QStringLiteral("Provider B"), {res2});

        QCOMPARE(tree->currentItem(), item1);
        QCOMPARE(tab.mainSplitter()->sizes(), expectedMainSizes);
        auto *detailsView = tab.findChild<QTextBrowser *>();
        QVERIFY(detailsView->toHtml().contains(QStringLiteral("First Result")));
    }

    void testRichMangaBakaDetailMetadataAndDetailSplitter()
    {
        SearchTab tab(QStringLiteral("mangabaka query"));
        tab.resize(800, 600);
        tab.show();

        SearchResult res;
        res.id = QStringLiteral("mangabaka-one-piece");
        res.title = QStringLiteral("One Piece");
        res.type = QStringLiteral("Manga");
        res.description = QStringLiteral("A legendary pirate adventure spanning hundreds of chapters.");
        res.url = QStringLiteral("https://example.org/mangabaka/one-piece");

        ResultMetadata meta;
        MetadataNode authorSec;
        authorSec.label = QStringLiteral("Authors");
        authorSec.values.append({QStringLiteral("Eiichiro Oda"), QUrl(), MetadataValue::Kind::Text});

        MetadataNode taxonomySec;
        taxonomySec.label = QStringLiteral("Taxonomy");
        MetadataNode genresNode;
        genresNode.label = QStringLiteral("Genres");
        genresNode.values.append({QStringLiteral("Action"), QUrl(), MetadataValue::Kind::Text});
        genresNode.values.append({QStringLiteral("Adventure"), QUrl(), MetadataValue::Kind::Text});
        genresNode.values.append({QStringLiteral("Comedy"), QUrl(), MetadataValue::Kind::Text});
        taxonomySec.children.append(genresNode);

        MetadataNode linksSec;
        linksSec.label = QStringLiteral("Source");
        linksSec.values.append({QStringLiteral("Canonical source"), QUrl(QStringLiteral("https://example.org/one-piece")), MetadataValue::Kind::Link});

        meta.sections = {authorSec, taxonomySec, linksSec};
        meta.attribution = {QStringLiteral("Data from MangaBaka")};

        tab.addResults(QStringLiteral("MangaBaka"), {res});

        auto *tree = tab.findChild<QTreeWidget *>();
        auto *item = tree->topLevelItem(0)->child(0);
        item->setData(0, Qt::UserRole + 4, QVariant::fromValue(meta)); // MetadataRole

        tree->setCurrentItem(item);
        emit tree->itemClicked(item, 0);

        auto *metaView = tab.findChild<QTreeWidget *>(QStringLiteral("structuredMetadata"));
        QVERIFY(metaView != nullptr);
        QVERIFY(metaView->isVisible());
        QVERIFY(metaView->topLevelItemCount() >= 3);

        auto *detailSplitter = tab.detailSplitter();
        QVERIFY(detailSplitter != nullptr);

        detailSplitter->setSizes({220, 280});
        tab.saveLayout();

        detailSplitter->setSizes({500, 0});
        QCOMPARE(detailSplitter->sizes().value(1), 0);

        auto *handle = detailSplitter->handle(1);
        QVERIFY(handle != nullptr);
        QTest::mouseDClick(handle, Qt::LeftButton);

        QVERIFY(detailSplitter->sizes().value(1) > 0);
    }
};

QTEST_MAIN(SearchTabTest)
#include "test_searchtab.moc"
