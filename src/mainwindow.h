#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KXmlGuiWindow>
#include <QTabWidget>
#include "searchprovider.h"
#include "searchdashboard.h"

class QSystemTrayIcon;
class QPluginLoader;

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

    friend class MainWindowTest;
};

#endif // MAINWINDOW_H
