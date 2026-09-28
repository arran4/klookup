#include <QTest>
#include <QTreeWidget>
#include "../src/searchtab.h"
#include "../src/searchprovider.h"

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

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget*>();
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

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget*>();
        QVERIFY(treeWidget);

        QCOMPARE(treeWidget->topLevelItemCount(), 1);
        QTreeWidgetItem *providerItem = treeWidget->topLevelItem(0);
        QCOMPARE(providerItem->text(0), providerName);
        QCOMPARE(providerItem->childCount(), 1);
        QVERIFY(providerItem->child(0)->text(0).contains("Connection failed"));
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

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget*>();
        QTreeWidgetItem *providerItem = treeWidget->topLevelItem(0);
        QTreeWidgetItem *childItem = providerItem->child(0);

        QTextBrowser *detailsView = tab.findChild<QTextBrowser*>();
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
            {"javascript:alert(1)", false},
            {"file:///etc/passwd", false},
            {"https://example.com\" onclick=\"alert(1)", false}, // Invalid URL due to spaces and quotes when parsed by QUrl
            {"", false},
            {"malformed::/url", false}
        };

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget*>();
        QTextBrowser *detailsView = tab.findChild<QTextBrowser*>();

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

            QString html = detailsView->toHtml();
            if (tc.shouldBeClickable) {
                // Should contain a valid link
                QVERIFY(html.contains("href="));
            } else {
                // If not empty, it should say Invalid or unsupported URL and contain no link for that url
                if (!tc.url.isEmpty()) {
                    QVERIFY(html.contains("Invalid or unsupported URL"));
                }
                QVERIFY(!html.contains("href="));
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

        QTreeWidget *treeWidget = tab.findChild<QTreeWidget*>();
        QTreeWidgetItem *providerItem = treeWidget->topLevelItem(0);
        QTreeWidgetItem *childItem = providerItem->child(0);

        QTextBrowser *detailsView = tab.findChild<QTextBrowser*>();

        emit treeWidget->itemClicked(childItem, 0);

        QString html = detailsView->toHtml();
        QVERIFY(!html.contains("<b>Key</b>")); // it should be escaped
        QVERIFY(html.contains("&lt;b&gt;Key&lt;/b&gt;"));
        QVERIFY(!html.contains("<script>"));
        QVERIFY(html.contains("&lt;script&gt;"));
    }
};

QTEST_MAIN(SearchTabTest)
#include "test_searchtab.moc"
