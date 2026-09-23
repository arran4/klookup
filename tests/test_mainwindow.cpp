#include <QTest>
#include <QSignalSpy>
#include <QClipboard>
#include <QGuiApplication>
#include <QTreeWidget>
#include <KSharedConfig>
#include <KConfigGroup>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include "../src/mainwindow.h"
#include "../src/searchtab.h"
#include "../src/settingsdialog.h"

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
        QVERIFY(m_settingsRoot.isValid());
        // A unique per-run root is required; Qt test mode may redirect to ~/.qttest/config.
        qputenv("XDG_CONFIG_HOME", m_settingsRoot.path().toUtf8());
        QStandardPaths::setTestModeEnabled(false);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsRoot.path());

        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        // KConfig might just return "klookuprc" for name(), let's just make sure it's valid
        QVERIFY(config);

        // Also ensure QSettings is clean for migration testing
        QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
        settings.clear();
    }

    void testMigration()
    {
        QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
        settings.clear();
        settings.setValue("MinimizeToTray", false);
        settings.setValue("MonitorClipboard", true);
        settings.beginGroup("History");
        settings.setValue("SearchHistory", QStringList() << "old search");
        settings.endGroup();
        settings.setValue("ugly_id_123_Enabled", false);
        settings.sync();

        // Clear KConfig
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        config->deleteGroup("General");
        config->deleteGroup("History");
        config->deleteGroup("Providers");
        config->sync();

        MainWindow mainWindow; // Should trigger loadSettings migration

        KConfigGroup genGroup(config, "General");
        QVERIFY(genGroup.readEntry("MigratedToKConfig", false));
        QCOMPARE(genGroup.readEntry("MinimizeToTray", true), false);
        QCOMPARE(genGroup.readEntry("MonitorClipboard", false), true);

        KConfigGroup historyGroup(config, "History");
        QCOMPARE(historyGroup.readEntry("SearchHistory", QStringList()), QStringList() << "old search");

        KConfigGroup provGroup(config, "Providers");
        QCOMPARE(provGroup.readEntry("ugly_id_123_Enabled", true), false);
    }

    void testMigrationNoOverwrite()
    {
        // Clear config first
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        config->deleteGroup("General");
        config->deleteGroup("History");
        config->deleteGroup("Providers");
        config->deleteGroup("OpenTabs");
        config->sync();

        // Setup KConfig to already have a value and migration marker
        KConfigGroup genGroup(config, "General");
        genGroup.writeEntry("MinimizeToTray", false);

        KConfigGroup tabGroup(config, "OpenTabs");
        tabGroup.writeEntry("Tab0", "some destination tab");
        // explicitly DO NOT write Count

        KConfigGroup historyGroup(config, "History");
        historyGroup.writeEntry("SearchHistory", QStringList() << "dest history");

        KConfigGroup provGroup(config, "Providers");
        provGroup.writeEntry("ugly_id_123_Enabled", false);

        config->sync();

        // Setup QSettings to have conflicting value
        QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
        settings.clear();
        settings.setValue("MinimizeToTray", true);
        settings.setValue("MonitorClipboard", true); // Missing destination key
        settings.setValue("ugly_id_123_Enabled", true);
        settings.beginGroup("History");
        settings.setValue("SearchHistory", QStringList() << "legacy history");
        settings.endGroup();

        settings.beginWriteArray("OpenTabs");
        settings.setArrayIndex(0);
        settings.setValue("query", "legacy tab");
        settings.endArray();

        settings.sync();

        MainWindow mainWindow; // Should NOT trigger overwrite

        // KConfig value should remain untouched
        QCOMPARE(genGroup.readEntry("MinimizeToTray", true), false);

        // Missing destination key should be imported
        QCOMPARE(genGroup.readEntry("MonitorClipboard", false), true);

        QCOMPARE(historyGroup.readEntry("SearchHistory", QStringList()), QStringList() << "dest history");
        QCOMPARE(provGroup.readEntry("ugly_id_123_Enabled", true), false);

        // The destination tab should have its count recovered
        QCOMPARE(tabGroup.readEntry("Count", 0), 1);
        QCOMPARE(tabGroup.readEntry("Tab0", QString()), QStringLiteral("some destination tab"));

        // Verify UI restored correctly
        QTabWidget *tabWidget = mainWindow.findChild<QTabWidget*>();
        QVERIFY(tabWidget);
        // Dashboard (1) + destination tab (1) = 2
        QCOMPARE(tabWidget->count(), 2);
        QCOMPARE(tabWidget->tabText(1), QStringLiteral("some destination tab"));

        SearchDashboard *dashboard = mainWindow.findChild<SearchDashboard*>();
        QVERIFY(dashboard);
        QCOMPARE(dashboard->saveHistory(), QStringList() << "dest history");

        mainWindow.close();

        // Verify idempotency
        MainWindow mainWindow2;
        QCOMPARE(genGroup.readEntry("MinimizeToTray", true), false);

        QTabWidget *tabWidget2 = mainWindow2.findChild<QTabWidget*>();
        QVERIFY(tabWidget2);
        QCOMPARE(tabWidget2->count(), 2);
        QCOMPARE(tabWidget2->tabText(1), QStringLiteral("some destination tab"));

        SearchDashboard *dashboard2 = mainWindow2.findChild<SearchDashboard*>();
        QVERIFY(dashboard2);
        QCOMPARE(dashboard2->saveHistory(), QStringList() << "dest history");

        genGroup.deleteEntry("MigratedToKConfig"); // Cleanup
        config->sync();
    }

    void testMigrationSparseTabs()
    {
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        config->deleteGroup("OpenTabs");
        KConfigGroup tabGroup(config, "OpenTabs");
        tabGroup.writeEntry("TabFoo", "garbage");
        tabGroup.writeEntry("Tab5", "valid sparse tab");
        config->sync();

        QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
        settings.clear();
        settings.sync();

        MainWindow mainWindow;

        // Count should be 6 because max index is 5.
        QCOMPARE(tabGroup.readEntry("Count", 0), 6);
        QCOMPARE(tabGroup.readEntry("Tab5", QString()), QStringLiteral("valid sparse tab"));
        QCOMPARE(tabGroup.readEntry("TabFoo", QString()), QStringLiteral("garbage"));

        QTabWidget *tabs = mainWindow.findChild<QTabWidget*>();
        // Dashboard (1) + only ONE real tab restored for Tab5 (1) = 2
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->tabText(1), QStringLiteral("valid sparse tab"));
    }

    void testMigrationEmptyLegacy()
    {
        // Ensure completely empty legacy does not create garbage defaults in KConfig
        QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
        settings.clear();
        settings.sync();

        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        config->deleteGroup("General");
        config->deleteGroup("History");
        config->deleteGroup("Providers");
        config->deleteGroup("OpenTabs");
        config->sync();

        MainWindow mainWindow;

        KConfigGroup genGroup(config, "General");
        QVERIFY(!genGroup.hasKey("MinimizeToTray"));
        QVERIFY(!genGroup.hasKey("MonitorClipboard"));

        KConfigGroup tabGroup(config, "OpenTabs");
        QVERIFY(!tabGroup.hasKey("Count"));
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
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        config->deleteGroup("OpenTabs");
        config->deleteGroup("History");
        config->sync();

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
        QCOMPARE(tabWidget->count(), 4); // dashboard + two tabs + history search

        // Add obsolete on-disk array entries after window restoration, not before.
        KConfigGroup tabGroup(config, QStringLiteral("OpenTabs"));
        tabGroup.writeEntry(QStringLiteral("Tab5"), QStringLiteral("stale"));
        config->sync();

        mainWindow->saveSettings();

        QCOMPARE(tabGroup.readEntry("Count", 0), 3);
        QCOMPARE(tabGroup.readEntry(QStringLiteral("Tab0"), QString()), QStringLiteral("test_quit_query_1"));
        QCOMPARE(tabGroup.readEntry(QStringLiteral("Tab1"), QString()), QStringLiteral("test_quit_query_2"));
        QCOMPARE(tabGroup.readEntry(QStringLiteral("Tab2"), QString()), QStringLiteral("history entry"));
        QVERIFY(tabGroup.readEntry(QStringLiteral("Tab5"), QString()).isEmpty());

        KConfigGroup historyGroup(config, QStringLiteral("History"));
        QCOMPARE(historyGroup.readEntry(QStringLiteral("SearchHistory"), QStringList()), QStringList { QStringLiteral("history entry") });

        delete mainWindow;
    }

    void testProviderEnablement()
    {
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        KConfigGroup provGroup(config, "Providers");
        provGroup.writeEntry("ugly_id_123_Enabled", false); // Mock provider id
        provGroup.writeEntry("error_ugly_id_456_Enabled", true); // Mock provider id
        config->sync();

        // Production initialization path
        MainWindow mainWindow;

        // Inject mocks
        MockProvider *mock = new MockProvider(&mainWindow);
        ErrorMockProvider *errMock = new ErrorMockProvider(&mainWindow);
        mainWindow.m_providers.append(mock);
        mainWindow.m_providers.append(errMock);

        mainWindow.restoreProviderState(mock);
        mainWindow.restoreProviderState(errMock);

        QCOMPARE(mock->isEnabled(), false);
        QCOMPARE(errMock->isEnabled(), true);

        mainWindow.performSearch("test query");

        QTabWidget *tabWidget = mainWindow.findChild<QTabWidget*>();
        SearchTab *searchTab = qobject_cast<SearchTab*>(tabWidget->currentWidget());
        QTreeWidget *treeWidget = searchTab->findChild<QTreeWidget*>();

        QCOMPARE(treeWidget->topLevelItemCount(), 1);
        QCOMPARE(treeWidget->topLevelItem(0)->text(0), QStringLiteral("Error Friendly Name"));
    }

    void testSettingsDialogLifecycle()
    {
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        KConfigGroup genGroup(config, "General");
        genGroup.writeEntry("MinimizeToTray", true);
        KConfigGroup initProvGroup(config, "Providers");
        initProvGroup.writeEntry("ugly_id_123_Enabled", true);
        config->sync();

        QList<SearchProvider*> providers;
        MockProvider mock;
        providers.append(&mock);

        // Test Cancel
        SettingsDialog *dialog = new SettingsDialog(providers);
        dialog->show();
        QCheckBox *minimizeCheck = dialog->findChild<QCheckBox*>();
        QVERIFY(minimizeCheck);
        minimizeCheck->setChecked(false);
        QListWidget* listWidget = dialog->findChild<QListWidget*>();
        QVERIFY(listWidget);
        QVERIFY(listWidget->count() > 0);
        listWidget->item(0)->setCheckState(Qt::Unchecked);
        dialog->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(genGroup.readEntry("MinimizeToTray", false), true);
        KConfigGroup provGroup(config, "Providers");
        QCOMPARE(provGroup.readEntry("ugly_id_123_Enabled", true), true); // Unchanged
        delete dialog;

        // Test Apply
        dialog = new SettingsDialog(providers);
        dialog->show(); // Needs to be shown for isVisible checks
        minimizeCheck = dialog->findChild<QCheckBox*>();
        minimizeCheck->setChecked(false);

        listWidget = dialog->findChild<QListWidget*>();
        if (listWidget && listWidget->count() > 0) {
            listWidget->item(0)->setCheckState(Qt::Unchecked);
        }

        dialog->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(genGroup.readEntry("MinimizeToTray", true), false);


        QCOMPARE(provGroup.readEntry("ugly_id_123_Enabled", true), false);
        QCOMPARE(mock.isEnabled(), false);

        QVERIFY(dialog->isVisible()); // Apply should not close
        dialog->reject();
        delete dialog;

        // Prove it persists by re-reconstructing
        MainWindow mainWindow;
        MockProvider *mock2 = new MockProvider(&mainWindow);
        mainWindow.m_providers.append(mock2);
        mainWindow.restoreProviderState(mock2);
        QCOMPARE(mock2->isEnabled(), false);
        mainWindow.performSearch("test query 2");
        QTabWidget *tabWidget = mainWindow.findChild<QTabWidget*>();
        SearchTab *searchTab = qobject_cast<SearchTab*>(tabWidget->currentWidget());
        QTreeWidget *treeWidget = searchTab->findChild<QTreeWidget*>();
        QCOMPARE(treeWidget->topLevelItemCount(), 0); // Skipped

        // Test OK
        dialog = new SettingsDialog(providers);
        dialog->show();
        minimizeCheck = dialog->findChild<QCheckBox*>();
        minimizeCheck->setChecked(true);
        listWidget = dialog->findChild<QListWidget*>();
        QVERIFY(listWidget);
        QVERIFY(listWidget->count() > 0);
        listWidget->item(0)->setCheckState(Qt::Checked);
        dialog->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(genGroup.readEntry("MinimizeToTray", false), true);
        QCOMPARE(provGroup.readEntry("ugly_id_123_Enabled", false), true);
        QCOMPARE(mock.isEnabled(), true);
        QVERIFY(!dialog->isVisible()); // OK should close
        delete dialog;

        // Prove OK persists
        MainWindow mainWindow3;
        MockProvider *mock3 = new MockProvider(&mainWindow3);
        mainWindow3.m_providers.append(mock3);
        mainWindow3.restoreProviderState(mock3);
        QCOMPARE(mock3->isEnabled(), true);
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
