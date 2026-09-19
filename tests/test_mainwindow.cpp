#include <QTest>
#include <QSignalSpy>
#include <QClipboard>
#include <QGuiApplication>
#include <QTreeWidget>
#include <QSettings>
#include <QTimer>
#include "../src/mainwindow.h"
#include "../src/searchtab.h"

class MockProvider : public SearchProvider
{
    Q_OBJECT
public:
    explicit MockProvider(QObject *parent = nullptr) : SearchProvider(parent) {}
    QString name() const override { return QStringLiteral("Friendly Name"); }
    QString id() const override { return QStringLiteral("ugly_id_123"); }
    void search(const QString &query) override {
        Q_UNUSED(query);
        QList<SearchResult> results;
        SearchResult res;
        res.title = "Mock Result";
        results.append(res);
        emit searchCompleted(id(), results);
    }
    SearchProvider* clone(QObject *parent = nullptr) const override {
        return new MockProvider(parent);
    }
};

class ErrorMockProvider : public SearchProvider
{
    Q_OBJECT
public:
    explicit ErrorMockProvider(QObject *parent = nullptr) : SearchProvider(parent) {}
    QString name() const override { return QStringLiteral("Error Friendly Name"); }
    QString id() const override { return QStringLiteral("error_ugly_id_456"); }
    void search(const QString &query) override {
        Q_UNUSED(query);
        emit searchFailed(id(), "Mock error message");
    }
    SearchProvider* clone(QObject *parent = nullptr) const override {
        return new ErrorMockProvider(parent);
    }
};

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
        QCOMPARE(tabWidget->count(), 2); // consecutive auto-search is suppressed

        mainWindow.processClipboardText("beta", true);
        QCOMPARE(tabWidget->count(), 3);

        mainWindow.processClipboardText("alpha", true);
        QCOMPARE(tabWidget->count(), 4);

        mainWindow.processClipboardText("alpha", false);
        QCOMPARE(tabWidget->count(), 4); // Prepopulation does clear the chain

        mainWindow.processClipboardText("alpha", true);
        QCOMPARE(tabWidget->count(), 5); // Cleared state, so consecutive identical autoSearch works

        mainWindow.processClipboardText("gamma", true);
        QCOMPARE(tabWidget->count(), 6);
    }

    void testSaveSettingsOnQuit()
    {
        // First, clear existing settings
        QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
        settings.remove(QStringLiteral("OpenTabs"));

        MainWindow *mainWindow = new MainWindow;

        mainWindow->performSearch("test_quit_query_1");
        mainWindow->performSearch("test_quit_query_2");

        QTabWidget *tabWidget = mainWindow->findChild<QTabWidget*>();
        QVERIFY(tabWidget);
        QCOMPARE(tabWidget->count(), 3); // 1 dashboard + 2 search tabs

        // Hide to tray (close event when minimizeToTray is true)
        QCloseEvent closeEvent;
        QApplication::sendEvent(mainWindow, &closeEvent);

        // Verify settings are not saved on hide to tray
        int size = settings.beginReadArray("OpenTabs");
        QCOMPARE(size, 0);
        settings.endArray();

        // Since we cannot emit aboutToQuit directly, we just call the method that's connected to it.
        // Wait, aboutToQuit requires QPrivateSignal. Instead of emitting the signal, we can call saveSettings
        // by finding it, or we can use QTimer to quit the app and run exec().
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        qApp->exec();

        size = settings.beginReadArray("OpenTabs");
        QCOMPARE(size, 2);
        settings.endArray();

        delete mainWindow;

        // Clean up settings
        settings.remove(QStringLiteral("OpenTabs"));
    }

    void testProviderDisplayNameInResults()
    {
        MainWindow mainWindow;

        MockProvider *mock = new MockProvider(&mainWindow);
        ErrorMockProvider *errMock = new ErrorMockProvider(&mainWindow);
        mainWindow.m_providers.append(mock);
        mainWindow.m_providers.append(errMock);

        mainWindow.performSearch("test query");

        QTabWidget *tabWidget = mainWindow.findChild<QTabWidget*>();
        QVERIFY(tabWidget);

        QWidget *currentWidget = tabWidget->currentWidget();
        SearchTab *searchTab = qobject_cast<SearchTab*>(currentWidget);
        QVERIFY(searchTab);

        QTreeWidget *treeWidget = searchTab->findChild<QTreeWidget*>();
        QVERIFY(treeWidget);

        QCOMPARE(treeWidget->topLevelItemCount(), 2);

        bool foundSuccess = false;
        bool foundError = false;

        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
            QTreeWidgetItem *providerItem = treeWidget->topLevelItem(i);
            if (providerItem->text(0) == QStringLiteral("Friendly Name")) {
                foundSuccess = true;
            } else if (providerItem->text(0) == QStringLiteral("Error Friendly Name")) {
                foundError = true;
                QVERIFY(providerItem->childCount() == 1);
                QVERIFY(providerItem->child(0)->text(0).contains("Mock error message"));
            }
        }

        QVERIFY(foundSuccess);
        QVERIFY(foundError);
    }
};

QTEST_MAIN(MainWindowTest)
#include "test_mainwindow.moc"
