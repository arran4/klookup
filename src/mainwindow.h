#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KXmlGuiWindow>
#include <QTabWidget>
#include <functional>
#include "searchprovider.h"
#include "clipboardpolicy.h"
#include "searchdashboard.h"

class QSystemTrayIcon;
class QPluginLoader;
class ProviderAuthManager;
class GlobalShortcuts;
class RecentActivity;

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

    void restoreProviderState(SearchProvider* provider);

protected:
    bool queryClose() override;
    void changeEvent(QEvent *event) override;

private slots:
    void onClipboardChanged();
    void showSettingsDialog();
    void configureShortcuts();

private:
    void performSearchInternal(const QString &query, bool clipboard, bool recordActivity);
    void loadPlugins();
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
    QSystemTrayIcon *m_trayIcon;
    QString m_lastAutoSearchClipboardText;
    QString m_lastCapturedClipboardText;
    bool m_clipboardReady = false;
    bool m_hadFocus = false;
    bool m_settingsSaved = false;
    ProviderAuthManager *m_authManager;
    GlobalShortcuts *m_globalShortcuts = nullptr;

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
