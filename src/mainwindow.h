#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KXmlGuiWindow>
#include <QTabWidget>
#include <functional>
#include "searchprovider.h"
#include "searchdashboard.h"

class QSystemTrayIcon;
class QPluginLoader;
class ProviderAuthManager;

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void performSearch(const QString &query);
    void processClipboardText(const QString &text, bool autoSearch);

    void restoreProviderState(SearchProvider* provider);

protected:
    bool queryClose() override;

private slots:
    void onClipboardChanged();
    void showSettingsDialog();

private:
    void loadPlugins();
    void loadSettings();
    void saveSettings();
    void setupActions();
    void setupTrayIcon();

    QTabWidget *m_tabWidget;
    SearchDashboard *m_dashboard;
    // Plugin root objects are owned by their QPluginLoader instances.  Keep each
    // accepted loader alive for as long as its provider is used.
    QList<QPluginLoader *> m_providerLoaders;
    QList<SearchProvider*> m_providers;
    QSystemTrayIcon *m_trayIcon;
    QString m_lastAutoSearchClipboardText;
    bool m_settingsSaved = false;
    ProviderAuthManager *m_authManager;

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
