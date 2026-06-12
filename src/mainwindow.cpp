#include "mainwindow.h"
#include "searchtab.h"
#include "dbusadaptor.h"
#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QWidget>
#include <QDBusConnection>
#include <QMenuBar>
#include <QAction>
#include <QPluginLoader>
#include <QDir>
#include <QCoreApplication>
// Mock KWallet
#ifndef Q_OS_WASM
// #include <KWallet>
#endif
// Mock persistence
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabWidget(new QTabWidget(this))
    , m_dashboard(new SearchDashboard(this))
{
    m_tabWidget->setTabsClosable(true);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (index > 0) { // Don't close the dashboard tab
            QWidget *widget = m_tabWidget->widget(index);
            m_tabWidget->removeTab(index);
            widget->deleteLater();
        }
    });

    m_tabWidget->addTab(m_dashboard, QStringLiteral("Dashboard"));
    m_tabWidget->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr); // Dashboard not closable

    setCentralWidget(m_tabWidget);
    setWindowTitle(QStringLiteral("KLookup"));
    resize(800, 600);

    loadPlugins();

    connect(m_dashboard, &SearchDashboard::searchRequested, this, &MainWindow::performSearch);

    // Setup menus
    QAction *settingsAction = new QAction(QStringLiteral("Settings"), this);
    connect(settingsAction, &QAction::triggered, this, [this]() {
        SettingsDialog dialog(m_providers, this);
        dialog.exec();
    });
    menuBar()->addAction(settingsAction);

    // Set up DBus Adaptor
    new DBusAdaptor(this);
    QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.klookup"));
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/"), this);

    loadSettings();
}

MainWindow::~MainWindow()
{
    qDeleteAll(m_providers);
}

void MainWindow::loadPlugins()
{
    QDir pluginsDir(QCoreApplication::applicationDirPath());
#if defined(Q_OS_WIN)
    if (pluginsDir.dirName().toLower() == "debug" || pluginsDir.dirName().toLower() == "release")
        pluginsDir.cdUp();
#elif defined(Q_OS_MAC)
    if (pluginsDir.dirName() == "MacOS") {
        pluginsDir.cdUp();
        pluginsDir.cdUp();
        pluginsDir.cdUp();
    }
#endif

    // In our mock build environment, the plugins are nested differently due to subdirectories
    pluginsDir.cd("providers");

    const QStringList pluginPaths = {
        pluginsDir.absolutePath(),
        QCoreApplication::applicationDirPath() + "/providers/wikipedia",
        QCoreApplication::applicationDirPath() + "/providers/github",
        QCoreApplication::applicationDirPath() + "/providers/mangaupdates",
        QCoreApplication::applicationDirPath() + "/providers/anilist",
        QCoreApplication::applicationDirPath() + "/providers/tvmaze",
        QCoreApplication::applicationDirPath() + "/providers/lyrics_ovh",
        QCoreApplication::applicationDirPath() + "/providers/duckduckgo",
        QCoreApplication::applicationDirPath() + "/providers/openlibrary",
        QCoreApplication::applicationDirPath() + "/providers/themoviedb",
        QCoreApplication::applicationDirPath() + "/providers/omdb",
        QCoreApplication::applicationDirPath() + "/providers/myanimelist",
        QCoreApplication::applicationDirPath() + "/providers/mangabaka",
#ifndef NDEBUG
        pluginsDir.absolutePath(),
        QDir::currentPath() + "/providers/wikipedia",
        QDir::currentPath() + "/providers/github",
        QDir::currentPath() + "/providers/mangaupdates",
        QDir::currentPath() + "/providers/anilist",
        QDir::currentPath() + "/providers/tvmaze",
        QDir::currentPath() + "/providers/lyrics_ovh",
        QDir::currentPath() + "/providers/duckduckgo",
        QDir::currentPath() + "/providers/openlibrary",
        QDir::currentPath() + "/providers/themoviedb",
        QDir::currentPath() + "/providers/omdb",
        QDir::currentPath() + "/providers/myanimelist",
        QDir::currentPath() + "/providers/mangabaka",
#endif
    };

    for (const QString &path : pluginPaths) {
        QDir dir(path);
        const auto entryList = dir.entryList(QDir::Files);
        for (const QString &fileName : entryList) {
            QPluginLoader loader(dir.absoluteFilePath(fileName));
            QObject *plugin = loader.instance();
            if (plugin) {
                SearchProvider *provider = qobject_cast<SearchProvider*>(plugin);
                if (provider) {
                    // Load wallet credentials if supported
                    if (provider->supportsAuthentication()) {
                        /*
                        KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), winId(), KWallet::Wallet::Synchronous);
                        if (wallet && wallet->hasFolder(KWallet::Wallet::PasswordFolder())) {
                            wallet->setFolder(KWallet::Wallet::PasswordFolder());
                            QString token;
                            if (wallet->readPassword(QStringLiteral("KLookup_") + provider->id(), token) == 0) {
                                provider->setCredentials(QString(), token);
                            }
                            delete wallet;
                        }
                        */
                    }
                    m_providers.append(provider);
                } else {
                    plugin->deleteLater();
                }
            }
        }
    }
}

void MainWindow::performSearch(const QString &query)
{
    SearchTab *tab = new SearchTab(query, this);
    int index = m_tabWidget->addTab(tab, query);
    m_tabWidget->setCurrentIndex(index);

    for (SearchProvider *providerTemplate : m_providers) {
        if (!providerTemplate->isEnabled()) continue;

        SearchProvider *provider = providerTemplate->clone(tab);

        connect(provider, &SearchProvider::searchCompleted, tab, [tab, provider](const QString &providerId, const QList<SearchResult> &results) {
            tab->addResults(providerId, results);
            provider->deleteLater();
        });
        connect(provider, &SearchProvider::searchFailed, tab, [tab, provider](const QString &providerId, const QString &error) {
            tab->setError(providerId, error);
            provider->deleteLater();
        });

        provider->search(query);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::loadSettings()
{
    QSettings settings("KDE", "klookup");

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
    QSettings settings("KDE", "klookup");

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
}
