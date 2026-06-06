#include "mainwindow.h"
#include "searchtab.h"
#include "provider_wikipedia.h"
#include "provider_github.h"
#include "provider_mangaupdates.h"
#include "dbusadaptor.h"
#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QWidget>
#include <QDBusConnection>
#include <QMenuBar>
#include <QAction>
// Mock persistence for now
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

    m_providers.append(new ProviderWikipedia(this));
    m_providers.append(new ProviderGithub(this));
    m_providers.append(new ProviderMangaUpdates(this));

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

    settings.beginWriteArray("OpenTabs");
    int writeIndex = 0;
    for (int i = 1; i < m_tabWidget->count(); ++i) {
        QString query = m_tabWidget->tabText(i);
        settings.setArrayIndex(writeIndex++);
        settings.setValue("query", query);
    }
    settings.endArray();
    settings.remove(QStringLiteral("OpenTabs"));

    settings.beginGroup("History");
    settings.setValue("SearchHistory", m_dashboard->saveHistory());
    settings.endGroup();
}
