#include <QTest>
#include <QSignalSpy>
#include <QClipboard>
#include <QGuiApplication>
#include <QTreeWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QLineEdit>
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

    QTemporaryDir m_settingsRoot;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_settingsRoot.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsRoot.path());
        QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
        QVERIFY2(settings.fileName().contains(QStringLiteral("KDE/klookup.conf")), qPrintable(settings.fileName()));
        settings.clear();
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

    void testSaveSettingsSerializesTabsAndHistory()
    {
        QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
        settings.beginWriteArray(QStringLiteral("OpenTabs"));
        settings.setArrayIndex(3);
        settings.setValue(QStringLiteral("query"), QStringLiteral("stale"));
        settings.endArray();

        MainWindow *mainWindow = new MainWindow;
        mainWindow->performSearch("test_quit_query_1");
        mainWindow->performSearch("test_quit_query_2");

        auto *dashboard = mainWindow->findChild<SearchDashboard *>();
        auto *searchBar = dashboard ? dashboard->findChild<QLineEdit *>() : nullptr;
        QVERIFY(searchBar);
        searchBar->setText(QStringLiteral("history entry"));
        QMetaObject::invokeMethod(searchBar, "returnPressed");

        QTabWidget *tabWidget = mainWindow->findChild<QTabWidget*>();
        QVERIFY(tabWidget);
        QCOMPARE(tabWidget->count(), 5); // dashboard + stale + two tabs + history search

        mainWindow->saveSettings();

        const int size = settings.beginReadArray("OpenTabs");
        QCOMPARE(size, 4);
        settings.setArrayIndex(1);
        QCOMPARE(settings.value(QStringLiteral("query")).toString(), QStringLiteral("test_quit_query_1"));
        settings.setArrayIndex(3);
        QCOMPARE(settings.value(QStringLiteral("query")).toString(), QStringLiteral("history entry"));
        settings.endArray();

        settings.beginGroup(QStringLiteral("History"));
        QCOMPARE(settings.value(QStringLiteral("SearchHistory")).toStringList(), QStringList { QStringLiteral("history entry") });
        settings.endGroup();

        delete mainWindow;
        settings.clear();
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
