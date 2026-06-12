#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow> // Using QMainWindow to bypass KF6 absence locally, actual KF6 targets KXmlGuiWindow
#include <QTabWidget>
#include "searchprovider.h"
#include "searchdashboard.h"

class QSystemTrayIcon;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void performSearch(const QString &query);

protected:
    void closeEvent(QCloseEvent *event) override;

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
};

#endif // MAINWINDOW_H
