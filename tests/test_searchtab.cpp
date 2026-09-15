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
};

QTEST_MAIN(SearchTabTest)
#include "test_searchtab.moc"
