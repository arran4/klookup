#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KXmlGuiWindow>
#include <QTabWidget>
#include "searchprovider.h"
#include "searchdashboard.h"

class QSystemTrayIcon;

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void performSearch(const QString &query);
    void processClipboardText(const QString &text, bool autoSearch);

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
    QList<SearchProvider*> m_providers;
    QSystemTrayIcon *m_trayIcon;
    QString m_lastAutoSearchClipboardText;

    friend class MainWindowTest;
};

#endif // MAINWINDOW_H
