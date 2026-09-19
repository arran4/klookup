#include "mainwindow.h"
#include "searchtab.h"
#include "dbusadaptor.h"
#include "settingsdialog.h"
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
#include <QClipboard>
#include <QApplication>
#include <QGuiApplication>
#include <KNotification>
#include <QSettings>
#include <QStyle>

#ifndef Q_OS_WASM
#include <KWallet>
#endif

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
    , m_tabWidget(new QTabWidget(this))
    , m_dashboard(new SearchDashboard(this))
    , m_trayIcon(new QSystemTrayIcon(this))
{
    QIcon icon = QIcon::fromTheme(QStringLiteral("dictionary"));
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        if (icon.isNull()) {
            icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
        }
    }
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

    setCentralWidget(m_tabWidget);
    setWindowTitle(QStringLiteral("KLookup"));
    resize(800, 600);

    setupActions();
    setupTrayIcon();
    loadPlugins();

    connect(m_dashboard, &SearchDashboard::searchRequested, this, &MainWindow::performSearch);
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::onClipboardChanged);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::saveSettings);

    new DBusAdaptor(this);
    QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.klookup"));
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/"), this);

    setXMLFile(QStringLiteral("klookupui.rc"));
    setupGUI();
    loadSettings();
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
    KStandardAction::quit(qApp, &QCoreApplication::quit, actionCollection());
}

void MainWindow::setupTrayIcon()
{
    QMenu *trayMenu = new QMenu(this);

    QAction *restoreAction = trayMenu->addAction(QStringLiteral("Restore"));
    connect(restoreAction, &QAction::triggered, this, &MainWindow::showNormal);

    trayMenu->addAction(actionCollection()->action(KStandardAction::name(KStandardAction::Preferences)));
    // Removed locally to use KF6
    trayMenu->addSeparator();
    trayMenu->addAction(actionCollection()->action(KStandardAction::name(KStandardAction::Quit)));
    // Removed locally to use KF6

    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            setVisible(!isVisible());
        }
    });
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(m_providers, this);
    dialog.exec();

    // We re-check clipboard monitoring if settings change
}

void MainWindow::onClipboardChanged()
{
    QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
    bool monitorClipboard = settings.value(QStringLiteral("MonitorClipboard"), false).toBool();
    bool autoSearch = settings.value(QStringLiteral("AutoSearchClipboard"), false).toBool();
    bool onlyWhenFocused = settings.value(QStringLiteral("ClipboardOnlyWhenFocused"), true).toBool();

    if (!monitorClipboard) return;
    if (onlyWhenFocused && !isActiveWindow()) return;

    QString text = QGuiApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) return;

    processClipboardText(text, autoSearch);
}

void MainWindow::processClipboardText(const QString &text, bool autoSearch)
{
    if (autoSearch) {
        if (text == m_lastAutoSearchClipboardText) {
            return;
        }
        m_lastAutoSearchClipboardText = text;

        KNotification *notification = new KNotification(QStringLiteral("ClipboardSearch"), KNotification::CloseOnTimeout, this);
        notification->setComponentName(QStringLiteral("org.kde.klookup"));
        notification->setText(QStringLiteral("Searching for: ") + text);
        notification->sendEvent();

        performSearch(text);
        showNormal();
        raise();
        activateWindow();
    } else {
        // Just prepopulate
        m_lastAutoSearchClipboardText.clear();
        m_tabWidget->setCurrentIndex(0); // Switch to dashboard
        m_dashboard->prepareQuery(text);
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

                        // Load wallet credentials if supported
                        if (provider->supportsAuthentication()) {
#ifndef Q_OS_WASM
                            KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), winId(), KWallet::Wallet::Synchronous);
                            if (wallet) {
                                if (wallet->hasFolder(QStringLiteral("KLookup"))) {
                                    wallet->setFolder(QStringLiteral("KLookup"));
                                    QString token;
                                    if (wallet->readPassword(provider->id(), token) == 0 && !token.isEmpty()) {
                                        provider->setCredentials(QString(), token);
                                    }
                                }
                                delete wallet;
                            }
#endif
                        }
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
    qCDebug(KLOOKUP_PLUGINS) << "Plugin discovery finished. Total loaded providers:" << m_providers.size();
}

void MainWindow::performSearch(const QString &query)
{
    SearchTab *tab = new SearchTab(query, this);
    int index = m_tabWidget->addTab(tab, query);
    m_tabWidget->setCurrentIndex(index);

    for (SearchProvider *providerTemplate : m_providers) {
        if (!providerTemplate->isEnabled()) continue;

        SearchProvider *provider = providerTemplate->clone(tab);

        QString providerName = provider->name();

        connect(provider, &SearchProvider::searchCompleted, tab, [tab, provider, providerName](const QString &providerId, const QList<SearchResult> &results) {
            Q_UNUSED(providerId);
            tab->addResults(providerName, results);
            provider->deleteLater();
        });
        connect(provider, &SearchProvider::searchFailed, tab, [tab, provider, providerName](const QString &providerId, const QString &error) {
            Q_UNUSED(providerId);
            tab->setError(providerName, error);
            provider->deleteLater();
        });

        provider->search(query);
    }
}

bool MainWindow::queryClose()
{
    QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
    bool minimizeToTray = settings.value(QStringLiteral("MinimizeToTray"), true).toBool();

    if (minimizeToTray && m_trayIcon->isVisible() && !qApp->isSavingSession()) {
        hide();
        return false;
    }
    // A normal close and session shutdown can destroy the window before the
    // application emits aboutToQuit.  Save while all session widgets are live;
    // aboutToQuit provides the same guarantee for explicit Quit actions.
    saveSettings();
    return true;
}

void MainWindow::loadSettings()
{
    QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));

    int size = settings.beginReadArray("OpenTabs");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QString query = settings.value("query").toString();
        if (!query.isEmpty()) {
            performSearch(query);
        }
    }
    settings.endArray();

    settings.beginGroup("History");
    m_dashboard->loadHistory(settings.value("SearchHistory").toStringList());
    settings.endGroup();
}

void MainWindow::saveSettings()
{
    if (m_settingsSaved) {
        return;
    }

    QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));

    settings.remove(QStringLiteral("OpenTabs"));

    settings.beginWriteArray("OpenTabs");
    int writeIndex = 0;
    for (int i = 1; i < m_tabWidget->count(); ++i) {
        QString query = m_tabWidget->tabText(i);
        settings.setArrayIndex(writeIndex++);
        settings.setValue("query", query);
    }
    settings.endArray();

    settings.beginGroup("History");
    settings.setValue("SearchHistory", m_dashboard->saveHistory());
    settings.endGroup();
    settings.sync();
    m_settingsSaved = true;
}
