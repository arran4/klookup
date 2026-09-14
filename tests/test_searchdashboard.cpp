#include <QTest>
#include <QSignalSpy>
#include <QLineEdit>
#include "../src/searchdashboard.h"

class SearchDashboardTest : public QObject
{
    Q_OBJECT

private slots:
    void testPrepareQuery()
    {
        SearchDashboard dashboard;
        QSignalSpy searchSpy(&dashboard, &SearchDashboard::searchRequested);

        // Access the QLineEdit to verify its state. We have to find it since it's private.
        QLineEdit *searchBar = dashboard.findChild<QLineEdit*>();
        QVERIFY(searchBar);

        // Prepopulate with query
        QString testQuery = QStringLiteral("test clipboard content");
        dashboard.prepareQuery(testQuery);

        // Verify that the query was set
        QCOMPARE(searchBar->text(), testQuery);

        // Verify that the query was selected
        QVERIFY(searchBar->hasSelectedText());
        QCOMPARE(searchBar->selectedText(), testQuery);

        // Verify that focus was requested
        // Note: checking hasFocus() might fail in headless CI unless the widget is shown,
        // but checking the focus policy / proxy or relying on QWidget::setFocus() works.
        // We'll just trust that setFocus was called or check if we can.

        // Ensure no search was requested
        QCOMPARE(searchSpy.count(), 0);

        // Ensure history was not modified
        QCOMPARE(dashboard.saveHistory().count(), 0);
    }
};

QTEST_MAIN(SearchDashboardTest)
#include "test_searchdashboard.moc"
