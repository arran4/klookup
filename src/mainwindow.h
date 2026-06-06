#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include "searchprovider.h"
#include "searchdashboard.h"

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

private:
    void loadSettings();
    void saveSettings();

    QTabWidget *m_tabWidget;
    SearchDashboard *m_dashboard;
    QList<SearchProvider*> m_providers;
};

#endif // MAINWINDOW_H
