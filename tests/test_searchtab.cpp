#include "../src/searchprovider.h"
#include "../src/searchtab.h"
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
};

QTEST_MAIN(SearchTabTest)
#include "test_searchtab.moc"
