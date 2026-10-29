#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KXmlGuiWindow>
#include <QTabWidget>
#include <functional>
#include "searchprovider.h"
#include "providerpackage.h"
#include "clipboardpolicy.h"
#include "searchdashboard.h"
#include "searchwindow.h"
#include "resultdetailwindow.h"

class QSystemTrayIcon;
class QPluginLoader;
class ProviderAuthManager;
class GlobalShortcuts;
class RecentActivity;
class QNetworkAccessManager;

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void performSearch(const QString &query);
    void processClipboardText(const QString &text, bool autoSearch);

    void showSearch();
    void showSearchViaShortcut();
    void searchClipboardNow();
    void notifyClipboardShortcutUsed();
    void restoreFromTray();
    void clearSearchTabs();

    SearchWindow *openSearchInNewWindow(const QString &query);
    ResultDetailWindow *openResultDetailsInNewWindow(const SearchResult &result,
                                                    const QString &providerName = QString(),
                                                    const ResultMetadata &metadata = ResultMetadata(),
                                                    const QString &contentHtml = QString());
    QList<SearchWindow *> searchWindows() const;
    QList<ResultDetailWindow *> detailWindows() const;

    void restoreProviderState(SearchProvider* provider);

    const QList<SearchProvider*> &providers() const { return m_providers; }
    SearchProvider *provider(const QString &id) const;
    SearchProvider *fallbackProvider(const QString &id) const;
    bool switchToFallback(const QString &id);
    void loadPlugins(const QStringList &customPackagePaths = {}, const QStringList &customPluginPaths = {});
    void setNetworkAccessManagerFactory(std::function<QNetworkAccessManager *(QObject *)> factory);
    void setPackageTrusted(const QString &id, bool trusted);
    bool isPackageTrusted(const QString &id) const;
    const QList<ProviderPackageStatus> &packages() const { return m_packages; }

signals:
    void packageReloaded(const QString &id);

protected:
    bool queryClose() override;
    void changeEvent(QEvent *event) override;

private slots:
    void onClipboardChanged();
    void showSettingsDialog();
    void configureShortcuts();

private:
    void performSearchInternal(const QString &query, bool clipboard, bool recordActivity);
    void loadSettings();
    void saveSettings();
    void setupActions();
    void setupTrayIcon();
    void handleClipboardEvent(ClipboardPolicy::Event event);

    QTabWidget *m_tabWidget;
    SearchDashboard *m_dashboard;
    RecentActivity *m_recent = nullptr;
    // Plugin root objects are owned by their QPluginLoader instances.  Keep each
    // accepted loader alive for as long as its provider is used.
    QList<QPluginLoader *> m_providerLoaders;
    QList<SearchProvider*> m_providers;
    QHash<QString, SearchProvider *> m_fallbackProviders;
    QList<SearchProvider *> m_scriptedAdapters;
    QList<ProviderPackageStatus> m_packages;
    QStringList m_customPackagePaths;
    QStringList m_customPluginPaths;
    std::function<QNetworkAccessManager *(QObject *)> m_namFactory;
    QSystemTrayIcon *m_trayIcon;
    QString m_lastAutoSearchClipboardText;
    QString m_lastCapturedClipboardText;
    bool m_clipboardReady = false;
    bool m_hadFocus = false;
    bool m_settingsSaved = false;
    ProviderAuthManager *m_authManager;
    GlobalShortcuts *m_globalShortcuts = nullptr;
    QList<QPointer<SearchWindow>> m_searchWindows;
    QList<QPointer<ResultDetailWindow>> m_detailWindows;

protected:
    virtual void loadWalletCredentials(SearchProvider *provider);
    virtual bool clipboardSearchNotificationsEnabled() const;
    virtual void emitClipboardNotification(const QString &text);

    // Narrow virtual wallet operations for testing
    virtual bool isWalletOpen() const;
    virtual void readWalletPasswordAndSet(SearchProvider *provider);

    friend class MainWindowTest;
};

#endif // MAINWINDOW_H
