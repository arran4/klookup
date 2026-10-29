#include "mainwindow.h"
#include "appicon.h"
#include "clipboardinput.h"
#include "globalshortcuts.h"
#include "resultdetailwindow.h"
#include "searchwindow.h"
#include <QTabBar>
#include <KShortcutsDialog>
#include <QMessageBox>
#include <QTimer>
#include "searchtab.h"
#include "resultactions.h"
#include <QMimeData>
#include "dbusadaptor.h"
#include "settingsdialog.h"
#include "providerauthmanager.h"
#include "sourceurl.h"
#include <QDesktopServices>
#include <QVBoxLayout>
#include <QWidget>
#include <QDBusConnection>
#include <QPluginLoader>
#include <QDir>
#include <QCoreApplication>
#include <KActionCollection>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(KLOOKUP_PLUGINS, "klookup.plugins", QtWarningMsg)
#include <KStandardAction>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>
#include <QEvent>
#include <QClipboard>
#include <QApplication>
#include <QGuiApplication>
#include <KNotification>
#include <QSettings>
#include <KSharedConfig>
#include <KConfigGroup>

#ifndef Q_OS_WASM
#include <KWallet>
#endif

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
    , m_tabWidget(new QTabWidget(this))
    , m_dashboard(new SearchDashboard(this))
    , m_trayIcon(new QSystemTrayIcon(this))
{
    const QIcon icon = applicationIcon();
    QApplication::setWindowIcon(icon);
    m_trayIcon->setIcon(icon);
    m_tabWidget->setTabsClosable(true);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (index > 0) {
            QWidget *widget = m_tabWidget->widget(index);
            m_tabWidget->removeTab(index);
            widget->deleteLater();
        }
    });

    m_tabWidget->addTab(m_dashboard, QStringLiteral("Dashboard"));
    m_tabWidget->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    m_tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabWidget->tabBar(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        int index = m_tabWidget->tabBar()->tabAt(pos);
        if (index > 0) {
            QMenu menu(this);
            auto *newWindowAction = menu.addAction(QIcon::fromTheme(QStringLiteral("window-new")), QStringLiteral("Open in New Window"));
            menu.addSeparator();
            auto *closeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close")), QStringLiteral("Close Tab"));
            connect(newWindowAction, &QAction::triggered, this, [this, index] {
                openSearchInNewWindow(m_tabWidget->tabText(index));
            });
            connect(closeAction, &QAction::triggered, this, [this, index] {
                if (index > 0 && index < m_tabWidget->count()) {
                    QWidget *widget = m_tabWidget->widget(index);
                    m_tabWidget->removeTab(index);
                    widget->deleteLater();
                }
            });
            menu.exec(m_tabWidget->tabBar()->mapToGlobal(pos));
        }
    });

    setCentralWidget(m_tabWidget);
    setWindowTitle(QStringLiteral("KLookup"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(800, 600);

    setupActions();
    setupTrayIcon();

    // Perform config migration before plugins are loaded so provider states are restored properly
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup genGroup(config, QStringLiteral("General"));

    if (!genGroup.readEntry("MigratedToKConfig", false)) {
        QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));

        if (!genGroup.hasKey("MinimizeToTray") && settings.contains(QStringLiteral("MinimizeToTray"))) genGroup.writeEntry("MinimizeToTray", settings.value(QStringLiteral("MinimizeToTray"), true).toBool());
        if (!genGroup.hasKey("MonitorClipboard") && settings.contains(QStringLiteral("MonitorClipboard"))) genGroup.writeEntry("MonitorClipboard", settings.value(QStringLiteral("MonitorClipboard"), false).toBool());
        if (!genGroup.hasKey("AutoSearchClipboard") && settings.contains(QStringLiteral("AutoSearchClipboard"))) genGroup.writeEntry("AutoSearchClipboard", settings.value(QStringLiteral("AutoSearchClipboard"), false).toBool());
        if (!genGroup.hasKey("ClipboardOnlyWhenFocused") && settings.contains(QStringLiteral("ClipboardOnlyWhenFocused"))) genGroup.writeEntry("ClipboardOnlyWhenFocused", settings.value(QStringLiteral("ClipboardOnlyWhenFocused"), true).toBool());

        KConfigGroup historyGroup(config, QStringLiteral("History"));
        settings.beginGroup("History");
        if (!historyGroup.hasKey("SearchHistory") && settings.contains("SearchHistory")) historyGroup.writeEntry("SearchHistory", settings.value("SearchHistory").toStringList());
        settings.endGroup();

        KConfigGroup providersGroup(config, QStringLiteral("Providers"));
        for (const QString &key : settings.allKeys()) {
            if (key.endsWith(QStringLiteral("_Enabled"))) {
                if (!providersGroup.hasKey(key) && settings.contains(key)) providersGroup.writeEntry(key, settings.value(key, true).toBool());
            }
        }

        int size = settings.beginReadArray("OpenTabs");
        KConfigGroup tabGroup(config, QStringLiteral("OpenTabs"));
        if (!tabGroup.hasKey("Count")) {
            bool destinationHasTabs = false;
            int maxTab = -1;
            for (const QString &key : tabGroup.keyList()) {
                if (key.startsWith(QStringLiteral("Tab"))) {
                    bool ok;
                    int tabNum = key.mid(3).toInt(&ok);
                    if (ok && tabNum >= 0) {
                        destinationHasTabs = true;
                        if (tabNum > maxTab) {
                            maxTab = tabNum;
                        }
                    }
                }
            }
            if (!destinationHasTabs && size > 0) {
                for (int i = 0; i < size; ++i) {
                    settings.setArrayIndex(i);
                    QString query = settings.value("query").toString();
                    tabGroup.writeEntry(QStringLiteral("Tab%1").arg(i), query);
                }
                tabGroup.writeEntry("Count", size);
            } else if (destinationHasTabs) {
                tabGroup.writeEntry("Count", maxTab + 1);
            }
        }
        settings.endArray();

        genGroup.writeEntry("MigratedToKConfig", true);
        config->sync();
    }

    ClipboardPolicy::migrate(genGroup);
    config->sync();

    m_authManager = new ProviderAuthManager(this, this);
    m_recent = new RecentActivity(config, this);
    m_dashboard->setRecentActivity(m_recent);
    connect(m_dashboard, &SearchDashboard::recentActivated, this, [this](const RecentItem &item) {
        const auto url = validatedSourceUrl(item.url.toString(QUrl::FullyEncoded));
        if (item.kind == RecentItem::Kind::Viewed && !url.isEmpty())
            QDesktopServices::openUrl(url);
        else
            performSearch(item.query.isEmpty() ? item.title : item.query);
    });
    loadPlugins();

    connect(m_dashboard, &SearchDashboard::searchRequested, this, &MainWindow::performSearch);
    connect(m_dashboard, &SearchDashboard::searchInNewWindowRequested, this, &MainWindow::openSearchInNewWindow);
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::onClipboardChanged);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::saveSettings);

    new DBusAdaptor(this);
    QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.klookup"));
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/"), this);

    setXMLFile(QStringLiteral("klookupui.rc"));
    setupGUI();
    loadSettings();
    m_clipboardReady = true;
}

MainWindow::~MainWindow()
{
    // Provider roots are owned by QPluginLoader.  Do not delete them or unload
    // their libraries here: active search-tab clones can still be QObject
    // children while this destructor is running.  Qt unloads the libraries at
    // application shutdown after the object hierarchy is gone.
}

void MainWindow::setupActions()
{
    KStandardAction::preferences(this, &MainWindow::showSettingsDialog, actionCollection());
    KStandardAction::keyBindings(this, &MainWindow::configureShortcuts, actionCollection());
    auto *show = actionCollection()->addAction(QStringLiteral("show_search"));
    show->setText(QStringLiteral("Show/focus KLookup search"));
    show->setIcon(QIcon::fromTheme(QStringLiteral("edit-find")));
    auto *clipboard = actionCollection()->addAction(QStringLiteral("search_clipboard"));
    clipboard->setText(QStringLiteral("Search clipboard now"));
    clipboard->setIcon(QIcon::fromTheme(QStringLiteral("edit-paste")));
    clipboard->setToolTip(QStringLiteral("Explicitly look up current plain clipboard text, including URLs"));
    for (auto *action : {show, clipboard}) {
        action->setProperty("componentName", QStringLiteral("org.kde.klookup"));
        action->setProperty("componentDisplayName", QStringLiteral("KLookup"));
    }
    connect(show, &QAction::triggered, this, &MainWindow::showSearchViaShortcut);
    connect(clipboard, &QAction::triggered, this, &MainWindow::searchClipboardNow);
    m_globalShortcuts = new GlobalShortcuts({show, clipboard}, this);
    KStandardAction::quit(qApp, &QCoreApplication::quit, actionCollection());
}

void MainWindow::setupTrayIcon()
{
    QMenu *trayMenu = new QMenu(this);

    QAction *restoreAction = trayMenu->addAction(QIcon::fromTheme(QStringLiteral("window-restore")), QStringLiteral("Restore"));
    connect(restoreAction, &QAction::triggered, this, &MainWindow::restoreFromTray);

    trayMenu->addAction(actionCollection()->action(KStandardAction::name(KStandardAction::KeyBindings)));
    trayMenu->addAction(actionCollection()->action(KStandardAction::name(KStandardAction::Preferences)));
    trayMenu->addSeparator();
    trayMenu->addAction(actionCollection()->action(KStandardAction::name(KStandardAction::Quit)));

    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            if (isVisible()) {
                KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
                KConfigGroup genGroup(config, QStringLiteral("General"));
                if (genGroup.readEntry(QStringLiteral("ClearTabsOnClose"), false)) {
                    clearSearchTabs();
                }
                hide();
            } else {
                restoreFromTray();
            }
        }
    });
}

void MainWindow::configureShortcuts()
{
    m_globalShortcuts->refresh();
    if (m_globalShortcuts->status() == GlobalShortcuts::Status::Unavailable) {
        QMessageBox::information(this, QStringLiteral("Keyboard Shortcuts"),
            QStringLiteral("KDE global shortcuts are unavailable in this session. KLookup can still be opened from its launcher or tray."));
        return;
    }
    KShortcutsDialog dialog(KShortcutsEditor::GlobalAction, KShortcutsEditor::LetterShortcutsDisallowed, this);
    dialog.addCollection(actionCollection(), QStringLiteral("KLookup"));
    dialog.configure(true);
}

void MainWindow::showSearch()
{
    m_tabWidget->setCurrentIndex(0);
    showNormal();
    raise();
    activateWindow();
    m_dashboard->focusQuery();
}

void MainWindow::showSearchViaShortcut()
{
    showSearch();
    QTimer::singleShot(0, this, &MainWindow::notifyClipboardShortcutUsed);
}

void MainWindow::searchClipboardNow()
{
    const QString query = ClipboardInput::explicitQuery(QGuiApplication::clipboard()->mimeData());
    // Reserve this text before activation so focus-trigger policy cannot add a
    // duplicate automatic tab for the same explicit invocation.
    if (!query.isEmpty()) {
        m_lastAutoSearchClipboardText = query;
        m_lastCapturedClipboardText = query;
    }
    showNormal();
    raise();
    activateWindow();
    if (!query.isEmpty()) performSearch(query);
    else showSearch();
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(m_providers, this, m_authManager);
    connect(&dialog, &SettingsDialog::settingsApplied, m_recent, &RecentActivity::reloadPolicy);
    dialog.exec();

    m_recent->reloadPolicy();
    // We re-check clipboard monitoring if settings change
}

void MainWindow::onClipboardChanged()
{
    handleClipboardEvent(ClipboardPolicy::Event::ClipboardChanged);
}

void MainWindow::handleClipboardEvent(ClipboardPolicy::Event event)
{
    if (!m_clipboardReady) return;
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("General"));
    const auto policy = ClipboardPolicy::read(group);
    if (!policy.permitsCapture(isVisible() && !isMinimized(), isActiveWindow())) return;
    const auto *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || mime->hasFormat(QString::fromLatin1(SourceUrlClipboardMime))) return;
    const auto input = ClipboardInput::classifyMimeData(mime);
    if (!input.permitsAutomaticCapture()) return;
    const bool changed = input.text != m_lastCapturedClipboardText;
    m_lastCapturedClipboardText = input.text;
    if (policy.searchesOn(event)) processClipboardText(input.text, true);
    else if (changed) processClipboardText(input.text, false);
}

void MainWindow::restoreFromTray()
{
    const bool opening = !isVisible();
    showNormal();
    raise();
    activateWindow();
    if (opening) handleClipboardEvent(ClipboardPolicy::Event::TrayOpened);
}

void MainWindow::notifyClipboardShortcutUsed()
{
    handleClipboardEvent(ClipboardPolicy::Event::GlobalShortcut);
}

void MainWindow::changeEvent(QEvent *event)
{
    KXmlGuiWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        const bool focused = isActiveWindow();
        const bool gained = focused && !m_hadFocus;
        m_hadFocus = focused;
        if (gained) handleClipboardEvent(ClipboardPolicy::Event::FocusGained);
    }
}

void MainWindow::processClipboardText(const QString &rawText, bool autoSearch)
{
    const auto input = ClipboardInput::classifyText(rawText);
    if (!input.permitsAutomaticCapture()) return;
    const QString text = input.text;
    if (autoSearch) {
        if (text == m_lastAutoSearchClipboardText) {
            return;
        }
        m_lastAutoSearchClipboardText = text;

        if (clipboardSearchNotificationsEnabled()) {
            emitClipboardNotification(text);
        }

        m_recent->recordCapture(text);
        performSearchInternal(text, true, true);
    } else {
        m_recent->recordCapture(text);
        // Just prepopulate
        m_lastAutoSearchClipboardText.clear();
        // Passive capture updates the entry without switching tabs or focus.
        m_dashboard->prepareQuery(text, false);
    }
}

void MainWindow::loadPlugins()
{
    QSet<QString> loadedProviderIds;

    QStringList pluginPaths = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/../lib64/plugins/klookup"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/plugins/klookup"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/klookup"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/providers"),
    };

    qCDebug(KLOOKUP_PLUGINS) << "Starting provider plugin discovery.";

    for (const QString &libPath : QCoreApplication::libraryPaths()) {
        pluginPaths.append(libPath + QStringLiteral("/klookup/providers"));
        pluginPaths.append(libPath + QStringLiteral("/klookup"));
    }

    for (const QString &path : pluginPaths) {
        QDir dir(path);
        if (!dir.exists()) {
            qCDebug(KLOOKUP_PLUGINS) << "Checked plugin directory (does not exist):" << path;
            continue;
        }

        qCDebug(KLOOKUP_PLUGINS) << "Checked plugin directory (exists):" << path;

        const auto entryList = dir.entryList(QDir::Files);
        for (const QString &fileName : entryList) {
            if (!fileName.endsWith(QStringLiteral(".so")) && !fileName.endsWith(QStringLiteral(".dll")) && !fileName.endsWith(QStringLiteral(".dylib"))) {
                continue;
            }

            qCDebug(KLOOKUP_PLUGINS) << "Evaluating candidate plugin library:" << fileName;

            auto *loader = new QPluginLoader(dir.absoluteFilePath(fileName), this);
            QObject *plugin = loader->instance();
            if (plugin) {
                SearchProvider *provider = qobject_cast<SearchProvider*>(plugin);
                if (provider) {
                    if (!loadedProviderIds.contains(provider->id())) {
                        loadedProviderIds.insert(provider->id());
                        qCDebug(KLOOKUP_PLUGINS) << "Successfully loaded provider:" << provider->id() << "Display name:" << provider->name();

                        loadWalletCredentials(provider);
                        m_providers.append(provider);
                        m_providerLoaders.append(loader);
                    } else {
                        qCDebug(KLOOKUP_PLUGINS) << "Skipped duplicate provider ID:" << provider->id() << "from file:" << fileName;
                        loader->unload();
                        delete loader;
                    }
                } else {
                    qCDebug(KLOOKUP_PLUGINS) << "Plugin loaded but casting to SearchProvider failed for file:" << fileName;
                    loader->unload();
                    delete loader;
                }
            } else {
                qCDebug(KLOOKUP_PLUGINS) << "QPluginLoader load failed for file:" << fileName << "Error:" << loader->errorString();
                delete loader;
            }
        }
    }

    for (SearchProvider *provider : m_providers) {
        restoreProviderState(provider);
    }

    qCDebug(KLOOKUP_PLUGINS) << "Plugin discovery finished. Total loaded providers:" << m_providers.size();
}

void MainWindow::restoreProviderState(SearchProvider* provider)
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup providersGroup(config, QStringLiteral("Providers"));
    provider->setEnabled(providersGroup.readEntry(provider->id() + QStringLiteral("_Enabled"), true));
}
void MainWindow::performSearch(const QString &query)
{
    performSearchInternal(query, false, true);
}
void MainWindow::performSearchInternal(const QString &query, bool clipboard, bool recordActivity)
{
    if (recordActivity)
        m_recent->recordSearch(query, clipboard);
    SearchTab *tab = new SearchTab(query, this);
    tab->setProperty("automaticClipboardQuery", clipboard);
    connect(tab, &SearchTab::resultViewed, m_recent, &RecentActivity::recordViewed);
    connect(tab, &SearchTab::openSearchInNewWindowRequested, this, &MainWindow::openSearchInNewWindow);
    connect(tab, &SearchTab::detailWindowCreated, this, [this](ResultDetailWindow *win) {
        if (win) {
            m_detailWindows.append(win);
            connect(win, &ResultDetailWindow::windowClosing, this, [this, win] {
                m_detailWindows.removeAll(win);
            });
        }
    });
    int index = m_tabWidget->addTab(tab, query);
    m_tabWidget->setCurrentIndex(index);

    for (SearchProvider *providerTemplate : m_providers) {
        if (!providerTemplate->isEnabled()) continue;

        tab->startProvider(providerTemplate->clone(tab), m_authManager->sessionFor(providerTemplate));
    }
}

bool MainWindow::queryClose()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup genGroup(config, QStringLiteral("General"));
    bool minimizeToTray = genGroup.readEntry(QStringLiteral("MinimizeToTray"), true);
    bool clearTabsOnClose = genGroup.readEntry(QStringLiteral("ClearTabsOnClose"), false);

    if (clearTabsOnClose) {
        clearSearchTabs();
    }

    if (minimizeToTray && m_trayIcon->isVisible() && !qApp->isSavingSession()) {
        hide();
        return false;
    }
    // A normal close and session shutdown can destroy the window before the
    // application emits aboutToQuit.  Save while all session widgets are live;
    // aboutToQuit provides the same guarantee for explicit Quit actions.
    saveSettings();
    for (auto win : m_searchWindows) {
        if (win) win->close();
    }
    for (auto win : m_detailWindows) {
        if (win) win->close();
    }
    return true;
}

void MainWindow::clearSearchTabs()
{
    while (m_tabWidget->count() > 1) {
        QWidget *widget = m_tabWidget->widget(1);
        m_tabWidget->removeTab(1);
        delete widget;
    }
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup tabGroup(config, QStringLiteral("OpenTabs"));
    tabGroup.deleteGroup();
    tabGroup.writeEntry("Count", 0);
    config->sync();
}

SearchWindow *MainWindow::openSearchInNewWindow(const QString &query)
{
    auto *window = new SearchWindow(query, m_providers, m_authManager);
    m_searchWindows.append(window);
    connect(window, &SearchWindow::windowClosing, this, [this, window] {
        m_searchWindows.removeAll(window);
    });
    if (auto *tab = window->searchTab()) {
        connect(tab, &SearchTab::resultViewed, m_recent, &RecentActivity::recordViewed);
        connect(tab, &SearchTab::openSearchInNewWindowRequested, this, [this](const QString &q) {
            openSearchInNewWindow(q);
        });
        connect(tab, &SearchTab::detailWindowCreated, this, [this](ResultDetailWindow *win) {
            if (win) {
                m_detailWindows.append(win);
                connect(win, &ResultDetailWindow::windowClosing, this, [this, win] {
                    m_detailWindows.removeAll(win);
                });
            }
        });
    }
    window->show();
    window->raise();
    window->activateWindow();
    return window;
}

ResultDetailWindow *MainWindow::openResultDetailsInNewWindow(const SearchResult &result,
                                                            const QString &providerName,
                                                            const ResultMetadata &metadata,
                                                            const QString &contentHtml)
{
    auto *detail = new ResultDetailWindow(result, providerName, metadata, contentHtml);
    m_detailWindows.append(detail);
    connect(detail, &ResultDetailWindow::windowClosing, this, [this, detail] {
        m_detailWindows.removeAll(detail);
    });
    detail->show();
    detail->raise();
    detail->activateWindow();
    return detail;
}

QList<SearchWindow *> MainWindow::searchWindows() const
{
    const_cast<MainWindow *>(this)->m_searchWindows.removeIf([](const QPointer<SearchWindow> &win) {
        return !win;
    });
    QList<SearchWindow *> result;
    for (const auto &win : m_searchWindows) {
        if (win) {
            result.append(win.data());
        }
    }
    return result;
}

QList<ResultDetailWindow *> MainWindow::detailWindows() const
{
    const_cast<MainWindow *>(this)->m_detailWindows.removeIf([](const QPointer<ResultDetailWindow> &win) {
        return !win;
    });
    QList<ResultDetailWindow *> result;
    for (const auto &win : m_detailWindows) {
        if (win) {
            result.append(win.data());
        }
    }
    return result;
}

void MainWindow::loadSettings()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));

    KConfigGroup tabGroup(config, QStringLiteral("OpenTabs"));
    int size = tabGroup.readEntry("Count", 0);
    for (int i = 0; i < size; ++i) {
        QString query = tabGroup.readEntry(QStringLiteral("Tab%1").arg(i), QString());
        if (!query.isEmpty()) {
            performSearchInternal(query, false, false);
        }
    }

    KConfigGroup historyGroup(config, QStringLiteral("History"));
    m_dashboard->loadHistory(historyGroup.readEntry("SearchHistory", QStringList()));
}

void MainWindow::saveSettings()
{
    if (m_settingsSaved) {
        return;
    }

    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));

    KConfigGroup tabGroup(config, QStringLiteral("OpenTabs"));
    tabGroup.deleteGroup();

    int writeIndex = 0;
    for (int i = 1; i < m_tabWidget->count(); ++i) {
        // Automatic clipboard queries are session-only. Otherwise the tab session
        // would persist captured text even with recent-clipboard storage disabled.
        if (m_tabWidget->widget(i)->property("automaticClipboardQuery").toBool())
            continue;
        QString query = m_tabWidget->tabText(i);
        tabGroup.writeEntry(QStringLiteral("Tab%1").arg(writeIndex++), query);
    }
    tabGroup.writeEntry("Count", writeIndex);

    KConfigGroup historyGroup(config, QStringLiteral("History"));
    historyGroup.writeEntry("SearchHistory", m_dashboard->saveHistory());

    config->sync();
    m_settingsSaved = true;
}

bool MainWindow::clipboardSearchNotificationsEnabled() const
{
    // Explicitly suppressed by default as per issue #39 policy.
    // If we want it configurable later, we add the config check here.
    return false;
}

void MainWindow::emitClipboardNotification(const QString &text)
{
    KNotification *notification = new KNotification(QStringLiteral("ClipboardSearch"), KNotification::CloseOnTimeout, this);
    notification->setComponentName(QStringLiteral("org.kde.klookup"));
    notification->setText(QStringLiteral("Searching for: ") + text);
    notification->sendEvent();
}

void MainWindow::loadWalletCredentials(SearchProvider *provider)
{
    // Register authentication state only. Opening even an unlocked KWallet can
    // ask for permission; restoring secrets is an explicit Settings action.
    m_authManager->sessionFor(provider);
}

bool MainWindow::isWalletOpen() const
{
#ifndef Q_OS_WASM
    return KWallet::Wallet::isOpen(KWallet::Wallet::LocalWallet());
#else
    return false;
#endif
}

void MainWindow::readWalletPasswordAndSet(SearchProvider *provider)
{
    Q_UNUSED(provider);
    // Kept as a compatibility/testing seam. Never open wallets during lookup.
}
