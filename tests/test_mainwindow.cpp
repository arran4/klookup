#include <QTest>
#include <QSignalSpy>
#include <QClipboard>
#include <QGuiApplication>
#include <QTreeWidget>
#include "../src/mainwindow.h"
#include "../src/searchtab.h"

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
    }

    void testClipboardDeduplication()
    {
        MainWindow mainWindow;

        QTabWidget *tabWidget = mainWindow.findChild<QTabWidget*>();
        QVERIFY(tabWidget);
        QCOMPARE(tabWidget->count(), 1);

        mainWindow.processClipboardText("alpha", true);
        QCOMPARE(tabWidget->count(), 2);

        mainWindow.processClipboardText("alpha", true);
        QCOMPARE(tabWidget->count(), 2);

        mainWindow.processClipboardText("beta", true);
        QCOMPARE(tabWidget->count(), 3);

        mainWindow.processClipboardText("alpha", true);
        QCOMPARE(tabWidget->count(), 4);

        mainWindow.processClipboardText("alpha", false);
        QCOMPARE(tabWidget->count(), 4);

        mainWindow.processClipboardText("alpha", true);
        QCOMPARE(tabWidget->count(), 4); // Prepopulate doesn't clear state, so consecutive identical autoSearch is still suppressed

        mainWindow.processClipboardText("gamma", true);
        QCOMPARE(tabWidget->count(), 5);
    }
};

QTEST_MAIN(MainWindowTest)
#include "test_mainwindow.moc"
