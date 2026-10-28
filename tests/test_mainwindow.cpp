#include <QTest>
#include <QSignalSpy>
#include <QClipboard>
#include <QMimeData>
#include <QComboBox>
#include "../src/resultactions.h"
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
#include <QLabel>
#include <QDialogButtonBox>
#include "../src/mainwindow.h"
#include "../src/searchtab.h"
#include "../src/settingsdialog.h"
#include "../src/dbusadaptor.h"

class TestableMainWindow : public MainWindow
{
public:
    explicit TestableMainWindow(QWidget *parent = nullptr) : MainWindow(parent) {}

    bool m_isOpenCalled = false;
    bool m_openWalletCalled = false;
    bool m_clipboardNotificationEmitted = false;

    // Make it public so we can invoke it from the test
    using MainWindow::loadWalletCredentials;

protected:
    void emitClipboardNotification(const QString &text) override {
        Q_UNUSED(text);
        m_clipboardNotificationEmitted = true;
    }

    bool isWalletOpen() const override {
        // cast away constness to mutate the tracker in this mock
        const_cast<TestableMainWindow*>(this)->m_isOpenCalled = true;
        return m_mockIsOpenValue;
    }

    void readWalletPasswordAndSet(SearchProvider *provider) override {
        Q_UNUSED(provider);
        m_openWalletCalled = true;
    }

public:
    bool clipboardSearchNotificationsEnabled() const override {
        if (m_overrideClipboardNotificationsEnabled) {
            return m_mockClipboardNotificationsEnabled;
        }
        return MainWindow::clipboardSearchNotificationsEnabled();
    }

public:
    bool m_mockIsOpenValue = false;
    bool m_overrideClipboardNotificationsEnabled = false;
    bool m_mockClipboardNotificationsEnabled = false;
};

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

class TestableSettingsDialog : public SettingsDialog
{
    Q_OBJECT
public:
    explicit TestableSettingsDialog(const QList<SearchProvider*> &providers, QWidget *parent = nullptr)
        : SettingsDialog(providers, parent) {}

    // Wallet overrides
    bool m_isWalletAvailable = true;
    bool m_openWalletSuccess = true;
    bool m_walletHasFolder = true;
    bool m_walletCreateFolderSuccess = true;
    bool m_walletSetFolderSuccess = true;
    int m_walletWritePasswordReturn = 0;

    int m_openWalletCallCount = 0;
    bool isWalletAvailable() const override { return m_isWalletAvailable; }
    bool openWallet() override { m_openWalletCallCount++; return m_openWalletSuccess; }
    bool walletHasFolder(const QString&) override { return m_walletHasFolder; }
    bool walletCreateFolder(const QString&) override { return m_walletCreateFolderSuccess; }
    bool walletSetFolder(const QString&) override { return m_walletSetFolderSuccess; }
    int walletWritePassword(const QString&, const QString&) override { return m_walletWritePasswordReturn; }
    void closeWallet() override {} // Do nothing

    // UI overrides
    QString m_dummyToken = QStringLiteral("secret_dummy_token_999");
    bool m_dummyOk = true;
    QString promptForToken(const QString&, const QString&, bool *ok) override {
        *ok = m_dummyOk;
        return m_dummyToken;
    }

    QString m_lastWarningTitle;
    QString m_lastWarningText;
    void displayWarning(const QString& title, const QString& text) override {
        m_lastWarningTitle = title;
        m_lastWarningText = text;
    }

    QString m_lastInfoTitle;
    QString m_lastInfoText;
    void displayInfo(const QString& title, const QString& text) override {
        m_lastInfoTitle = title;
        m_lastInfoText = text;
    }
};

class AuthMockProvider : public SearchProvider
{
    Q_OBJECT
public:
    explicit AuthMockProvider(QObject *parent = nullptr) : SearchProvider(parent) {}
    QString name() const override { return QStringLiteral("Auth Mock"); }
    QString id() const override { return QStringLiteral("auth_mock_123"); }
    void search(const QString &query) override { Q_UNUSED(query); }
    SearchProvider* clone(QObject *parent = nullptr) const override { return new AuthMockProvider(parent); }

    bool supportsAuthentication() const override { return true; }
    void setCredentials(const QString &username, const QString &password) override {
        Q_UNUSED(username);
        m_savedToken = password;
    }

    QString m_savedToken;
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

    void testClipboardBackgroundActivity()
    {
        TestableMainWindow mainWindow;
        mainWindow.showMinimized();
        QVERIFY(!mainWindow.isActiveWindow());
        QVERIFY(!mainWindow.m_clipboardNotificationEmitted);

        // This exercises the production MainWindow::clipboardSearchNotificationsEnabled()
        mainWindow.processClipboardText("background test", true);

        QVERIFY(!mainWindow.isActiveWindow());
        QCOMPARE(mainWindow.windowState() & Qt::WindowMinimized, Qt::WindowMinimized);
        QVERIFY(!mainWindow.m_clipboardNotificationEmitted); // No notification by default

        // Prove the seam protects a real branch: if enabled, it should emit
        mainWindow.m_overrideClipboardNotificationsEnabled = true;
        mainWindow.m_mockClipboardNotificationsEnabled = true;
        mainWindow.processClipboardText("another test", true);
        QVERIFY(mainWindow.m_clipboardNotificationEmitted);
    }

    void testDBusPerformSearchIsPassive()
    {
        MainWindow mainWindow;
        mainWindow.showMinimized();
        QVERIFY(!mainWindow.isActiveWindow());

        DBusAdaptor adaptor(&mainWindow);
        adaptor.performSearch("dbus search test");

        QVERIFY(!mainWindow.isActiveWindow());
        QCOMPARE(mainWindow.windowState() & Qt::WindowMinimized, Qt::WindowMinimized);
    }

    void testWalletNotOpenedWhenClosed()
    {
        TestableMainWindow mainWindow;
        AuthMockProvider authMock;

        // Test case 1: Wallet is closed (mock returns false)
        mainWindow.m_mockIsOpenValue = false;
        mainWindow.m_isOpenCalled = false;
        mainWindow.m_openWalletCalled = false;

        mainWindow.loadWalletCredentials(&authMock);

        QVERIFY(!mainWindow.m_isOpenCalled);
        QVERIFY(!mainWindow.m_openWalletCalled); // Startup never asks for wallet authorization

        // Test case 2: Wallet is open (mock returns true)
        mainWindow.m_mockIsOpenValue = true;
        mainWindow.m_isOpenCalled = false;
        mainWindow.m_openWalletCalled = false;

        mainWindow.loadWalletCredentials(&authMock);

        QVERIFY(!mainWindow.m_isOpenCalled);
        QVERIFY(!mainWindow.m_openWalletCalled); // An unlocked wallet can still prompt, so restoration is explicit
    }

    void testClipboardUrlFilteringAndExplicitSearch()
    {
        MainWindow mainWindow;
        auto *tabs = mainWindow.findChild<QTabWidget *>();
        auto *dashboard = mainWindow.findChild<SearchDashboard *>();
        auto *entry = dashboard->findChild<QLineEdit *>();
        const int count = tabs->count();
        entry->setText(QStringLiteral("Existing query"));
        mainWindow.processClipboardText(QStringLiteral("https://example.org/source"), false);
        QCOMPARE(entry->text(), QStringLiteral("Existing query"));
        mainWindow.processClipboardText(QStringLiteral("https://example.org/source"), true);
        QCOMPARE(tabs->count(), count);
        QVERIFY(dashboard->saveHistory().isEmpty());
        mainWindow.performSearch(QStringLiteral("https://example.org/source"));
        QCOMPARE(tabs->count(), count + 1); // deliberate URL lookup bypasses automatic policy
        mainWindow.processClipboardText(QStringLiteral("  ordinary words  "), false);
        QCOMPARE(entry->text(), QStringLiteral("ordinary words"));
        mainWindow.processClipboardText(QStringLiteral("ordinary words"), true);
        QCOMPARE(tabs->count(), count + 2);
        mainWindow.processClipboardText(QStringLiteral(" ordinary words "), true);
        QCOMPARE(tabs->count(), count + 2);
    }

    void testClipboardMimeChanges()
    {
        MainWindow mainWindow;
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("General"));
        group.writeEntry(QStringLiteral("MonitorClipboard"), true);
        group.writeEntry(QStringLiteral("AutoSearchClipboard"), false);
        group.writeEntry(QStringLiteral("ClipboardOnlyWhenFocused"), false);
        group.writeEntry(QStringLiteral("ClipboardCaptureMode"), int(ClipboardPolicy::Capture::Running));
        group.writeEntry(QStringLiteral("ClipboardSearchTrigger"), int(ClipboardPolicy::Trigger::Never));
        group.sync();
        auto *dashboard = mainWindow.findChild<SearchDashboard *>();
        auto *entry = dashboard->findChild<QLineEdit *>();
        entry->setText(QStringLiteral("Original"));
        auto *mime = new QMimeData;
        mime->setHtml(QStringLiteral("<p>HTML-only input</p>"));
        QGuiApplication::clipboard()->setMimeData(mime);
        QCOMPARE(entry->text(), QStringLiteral("Original"));
        QGuiApplication::clipboard()->setText(QStringLiteral("https://example.org/source"));
        QCOMPARE(entry->text(), QStringLiteral("Original"));
        QGuiApplication::clipboard()->setText(QStringLiteral("Accepted plain input"));
        QCOMPARE(entry->text(), QStringLiteral("Accepted plain input"));
        QVERIFY(dashboard->saveHistory().isEmpty());
        group.writeEntry(QStringLiteral("MonitorClipboard"), false);
        group.sync();
    }

    void testVisibilityAndTriggerEvents()
    {
        using namespace ClipboardPolicy;
        MainWindow window;
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("General"));
        auto *tabs = window.findChild<QTabWidget *>();
        auto *dashboard = window.findChild<SearchDashboard *>();
        auto *entry = dashboard->findChild<QLineEdit *>();
        const int initial = tabs->count();
        write(group, {Capture::Visible, Trigger::ClipboardChanged});
        QGuiApplication::clipboard()->setText(QStringLiteral("hidden clipboard"));
        QCOMPARE(tabs->count(), initial); // tray-hidden process is not a visible window
        window.show();
        QCoreApplication::processEvents();
        QWidget other;
        other.show();
        other.activateWindow();
        QTRY_VERIFY(!window.isActiveWindow());
        QGuiApplication::clipboard()->setText(QStringLiteral("visible clipboard"));
        QCOMPARE(tabs->count(), initial + 1);
        QVERIFY(!window.isActiveWindow()); // visible capture never steals focus
        window.handleClipboardEvent(Event::ClipboardChanged);
        QCOMPARE(tabs->count(), initial + 1);
        write(group, {Capture::Running, Trigger::TrayOpened});
        window.hide();
        QGuiApplication::clipboard()->setText(QStringLiteral("quiet background capture"));
        QCOMPARE(tabs->count(), initial + 1);
        QCOMPARE(entry->text(), QStringLiteral("quiet background capture"));
        QCOMPARE(tabs->currentIndex(), initial); // capture does not switch away from results
        QVERIFY(!window.isVisible());
        window.restoreFromTray();
        QCOMPARE(tabs->count(), initial + 2);
        window.restoreFromTray();
        QCOMPARE(tabs->count(), initial + 2); // already shown is not a tray-open transition
        window.hide();
        window.restoreFromTray();
        QCOMPARE(tabs->count(), initial + 2); // same approved text deduplicates
        write(group, {Capture::Running, Trigger::GlobalShortcut});
        QGuiApplication::clipboard()->setText(QStringLiteral("shortcut trigger"));
        QCOMPARE(tabs->count(), initial + 2);
        window.notifyClipboardShortcutUsed();
        QCOMPARE(tabs->count(), initial + 3);
        window.notifyClipboardShortcutUsed();
        QCOMPARE(tabs->count(), initial + 3);
        write(group, {Capture::Never, Trigger::GlobalShortcut});
        QGuiApplication::clipboard()->setText(QStringLiteral("unapproved clipboard"));
        window.notifyClipboardShortcutUsed();
        QCOMPARE(tabs->count(), initial + 3);
        write(group, {Capture::Running, Trigger::FocusGained});
        QGuiApplication::clipboard()->setText(QStringLiteral("focus trigger"));
        window.handleClipboardEvent(Event::FocusGained);
        QCOMPARE(tabs->count(), initial + 4);
        window.handleClipboardEvent(Event::FocusGained);
        QCOMPARE(tabs->count(), initial + 4);
        write(group, {Capture::Never, Trigger::Never});
    }

    void testClipboardSettingsApplyCancel()
    {
        using namespace ClipboardPolicy;
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("General"));
        write(group, {Capture::Never, Trigger::Never});
        group.sync();
        SettingsDialog cancelled({});
        auto *capture = cancelled.findChild<QComboBox *>(QStringLiteral("clipboardCaptureMode"));
        auto *trigger = cancelled.findChild<QComboBox *>(QStringLiteral("clipboardSearchTrigger"));
        QVERIFY(capture && trigger);
        capture->setCurrentIndex(int(Capture::Running));
        trigger->setCurrentIndex(int(Trigger::ClipboardChanged));
        cancelled.reject();
        QCOMPARE(read(group).capture, Capture::Never);
        QCOMPARE(read(group).trigger, Trigger::Never);
        SettingsDialog applied({});
        capture = applied.findChild<QComboBox *>(QStringLiteral("clipboardCaptureMode"));
        trigger = applied.findChild<QComboBox *>(QStringLiteral("clipboardSearchTrigger"));
        capture->setCurrentIndex(int(Capture::Visible));
        trigger->setCurrentIndex(int(Trigger::TrayOpened));
        applied.button(QDialogButtonBox::Apply)->click();
        QCOMPARE(read(group).capture, Capture::Visible);
        QCOMPARE(read(group).trigger, Trigger::TrayOpened);
        SettingsDialog reopened({});
        QCOMPARE(reopened.findChild<QComboBox *>(QStringLiteral("clipboardCaptureMode"))->currentIndex(), int(Capture::Visible));
        QCOMPARE(reopened.findChild<QComboBox *>(QStringLiteral("clipboardSearchTrigger"))->currentIndex(), int(Trigger::TrayOpened));
        write(group, {Capture::Never, Trigger::Never});
    }

    void testEmptyClipboardChange()
    {
        MainWindow window;
        auto *tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs);
        const int count = tabs->count();
        QGuiApplication::clipboard()->clear();
        QVERIFY(QMetaObject::invokeMethod(&window, "onClipboardChanged", Qt::DirectConnection));
        QCOMPARE(tabs->count(), count);
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
        QVERIFY(tabWidget);
        SearchTab *searchTab = qobject_cast<SearchTab*>(tabWidget->currentWidget());
        QVERIFY(searchTab);
        QTreeWidget *treeWidget = searchTab->findChild<QTreeWidget*>();
        QVERIFY(treeWidget);

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
        QCheckBox *minimizeCheck = dialog->findChild<QCheckBox*>(QStringLiteral("minimizeToTray"));
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
        QCOMPARE(mock.isEnabled(), true); // Unchanged runtime state
        delete dialog;

        // Test Apply
        dialog = new SettingsDialog(providers);
        dialog->show(); // Needs to be shown for isVisible checks
        minimizeCheck = dialog->findChild<QCheckBox*>(QStringLiteral("minimizeToTray"));
        minimizeCheck->setChecked(false);

        listWidget = dialog->findChild<QListWidget*>();
        QVERIFY(listWidget && listWidget->count() > 0);
        listWidget->item(0)->setCheckState(Qt::Unchecked);

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
        QVERIFY(tabWidget);
        SearchTab *searchTab = qobject_cast<SearchTab*>(tabWidget->currentWidget());
        QVERIFY(searchTab);
        QTreeWidget *treeWidget = searchTab->findChild<QTreeWidget*>();
        QVERIFY(treeWidget);
        QCOMPARE(treeWidget->topLevelItemCount(), 0); // Skipped

        // Test OK
        dialog = new SettingsDialog(providers);
        dialog->show();
        minimizeCheck = dialog->findChild<QCheckBox*>(QStringLiteral("minimizeToTray"));
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

        mainWindow3.performSearch("test query 3");
        tabWidget = mainWindow3.findChild<QTabWidget*>();
        QVERIFY(tabWidget);
        searchTab = qobject_cast<SearchTab*>(tabWidget->currentWidget());
        QVERIFY(searchTab);
        treeWidget = searchTab->findChild<QTreeWidget*>();
        QVERIFY(treeWidget);
        QCOMPARE(treeWidget->topLevelItemCount(), 1); // Participated

        // Verify unknown provider defaults to enabled
        provGroup.deleteEntry("error_ugly_id_456_Enabled");
        QVERIFY(!provGroup.hasKey("error_ugly_id_456_Enabled"));
        ErrorMockProvider errMock;
        mainWindow3.restoreProviderState(&errMock);
        QCOMPARE(errMock.isEnabled(), true);

        // Verify an unrelated stale ID does not affect configured provider
        provGroup.writeEntry("some_stale_id_Enabled", false);
        mainWindow3.restoreProviderState(mock3);
        QCOMPARE(mock3->isEnabled(), true);
    }

    void testCredentialPersistenceFailures()
    {
        AuthMockProvider mock;
        QList<SearchProvider*> providers;
        providers.append(&mock);

        TestableSettingsDialog dialog(providers);

        QListWidget *listWidget = dialog.findChild<QListWidget*>();
        QVERIFY(listWidget);
        QVERIFY(listWidget->count() > 0);
        listWidget->setCurrentRow(0);
        QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("providerAuthMode"))->text().contains(QStringLiteral("Manually supplied")));

        // Reset
        auto resetMocks = [&]() {
            mock.m_savedToken.clear();
            dialog.m_lastInfoText.clear();
            dialog.m_lastInfoTitle.clear();
            dialog.m_lastWarningText.clear();
            dialog.m_lastWarningTitle.clear();
            dialog.m_isWalletAvailable = true;
            dialog.m_openWalletSuccess = true;
            dialog.m_walletHasFolder = true;
            dialog.m_walletCreateFolderSuccess = true;
            dialog.m_walletSetFolderSuccess = true;
            dialog.m_walletWritePasswordReturn = 0;
            dialog.m_dummyOk = true;
            dialog.m_openWalletCallCount = 0;
        };

        // Test Cancel (No input)
        resetMocks();
        dialog.m_dummyOk = false;
        QMetaObject::invokeMethod(&dialog, "showLoginDialog");
        QVERIFY(mock.m_savedToken.isEmpty()); // Should not have applied
        QVERIFY(dialog.m_lastInfoTitle.isEmpty());
        QVERIFY(dialog.m_lastWarningTitle.isEmpty());
        QCOMPARE(dialog.m_openWalletCallCount, 0);

        // Test Success (Folder Exists)
        resetMocks();
        QMetaObject::invokeMethod(&dialog, "showLoginDialog");
        QCOMPARE(mock.m_savedToken, dialog.m_dummyToken);
        QCOMPARE(dialog.m_lastInfoTitle, QStringLiteral("Saved"));
        QVERIFY(dialog.m_lastInfoText.contains("Credentials saved for"));

        // Test Success (Folder doesn't exist, create succeeds)
        resetMocks();
        dialog.m_walletHasFolder = false;
        QMetaObject::invokeMethod(&dialog, "showLoginDialog");
        QCOMPARE(mock.m_savedToken, dialog.m_dummyToken);
        QCOMPARE(dialog.m_lastInfoTitle, QStringLiteral("Saved"));

        // Test OpenFailed
        resetMocks();
        dialog.m_openWalletSuccess = false;
        QMetaObject::invokeMethod(&dialog, "showLoginDialog");
        QCOMPARE(mock.m_savedToken, dialog.m_dummyToken);
        QCOMPARE(dialog.m_lastWarningTitle, QStringLiteral("Storage Error"));
        QVERIFY(dialog.m_lastWarningText.contains("Failed to open the wallet."));
        QVERIFY(dialog.m_lastWarningText.contains("Credentials will only be used for this session."));

        // Test FolderFailed (Create failed)
        resetMocks();
        dialog.m_walletHasFolder = false;
        dialog.m_walletCreateFolderSuccess = false;
        QMetaObject::invokeMethod(&dialog, "showLoginDialog");
        QCOMPARE(mock.m_savedToken, dialog.m_dummyToken);
        QCOMPARE(dialog.m_lastWarningTitle, QStringLiteral("Storage Error"));
        QVERIFY(dialog.m_lastWarningText.contains("Failed to create or access the required wallet folder."));
        QVERIFY(dialog.m_lastWarningText.contains("Credentials will only be used for this session."));

        // Test FolderFailed (Set failed)
        resetMocks();
        dialog.m_walletHasFolder = true;
        dialog.m_walletSetFolderSuccess = false;
        QMetaObject::invokeMethod(&dialog, "showLoginDialog");
        QCOMPARE(mock.m_savedToken, dialog.m_dummyToken);
        QCOMPARE(dialog.m_lastWarningTitle, QStringLiteral("Storage Error"));
        QVERIFY(dialog.m_lastWarningText.contains("Failed to create or access the required wallet folder."));
        QVERIFY(dialog.m_lastWarningText.contains("Credentials will only be used for this session."));

        // Test WriteFailed
        resetMocks();
        dialog.m_walletWritePasswordReturn = -1;
        QMetaObject::invokeMethod(&dialog, "showLoginDialog");
        QCOMPARE(mock.m_savedToken, dialog.m_dummyToken);
        QCOMPARE(dialog.m_lastWarningTitle, QStringLiteral("Storage Error"));
        QVERIFY(dialog.m_lastWarningText.contains("Failed to save credentials to the wallet."));
        QVERIFY(dialog.m_lastWarningText.contains("Credentials will only be used for this session."));

        // Test Unavailable
        resetMocks();
        dialog.m_isWalletAvailable = false;
        QMetaObject::invokeMethod(&dialog, "showLoginDialog");
        QCOMPARE(mock.m_savedToken, dialog.m_dummyToken);
        QCOMPARE(dialog.m_lastWarningTitle, QStringLiteral("Storage Unavailable"));
        QVERIFY(dialog.m_lastWarningText.contains("Credential storage is not available on this platform."));
        QVERIFY(dialog.m_lastWarningText.contains("Credentials will only be used for this session."));
    }

    void testOwnCopyUrlDoesNotBounceIntoLegacyCapture()
    {
        MainWindow window;
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("General"));
        group.writeEntry(QStringLiteral("MonitorClipboard"), true);
        group.writeEntry(QStringLiteral("AutoSearchClipboard"), true);
        group.writeEntry(QStringLiteral("ClipboardOnlyWhenFocused"), false);
        auto *tabs = window.findChild<QTabWidget *>();
        const int initial = tabs->count();
        SearchResult result;
        result.url = QStringLiteral("https://example.org/source");
        ResultActions commands(result, false);
        commands.copyUrl->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(tabs->count(), initial);
        QCOMPARE(QGuiApplication::clipboard()->text(), result.url);
        group.writeEntry(QStringLiteral("MonitorClipboard"), false);
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
