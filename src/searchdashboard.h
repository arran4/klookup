#ifndef SEARCHDASHBOARD_H
#define SEARCHDASHBOARD_H

#include "recentactivity.h"
#include <QCompleter>
#include <QLineEdit>
#include <QPointer>
#include <QWidget>
class QListWidget;
class QLabel;
class QPushButton;

class SearchDashboard : public QWidget
{
    Q_OBJECT

public:
    explicit SearchDashboard(QWidget *parent = nullptr);
    ~SearchDashboard();

    void loadHistory(const QStringList &history);
    QStringList saveHistory() const;
    void setRecentActivity(RecentActivity *activity);
    void prepareQuery(const QString &query, bool focus = true);

signals:
    void searchRequested(const QString &query);
    void recentActivated(const RecentItem &item);

private slots:
    void onSearchTriggered();
    void refreshRecent();

private:
    QLineEdit *m_searchBar;
    QCompleter *m_completer;
    QStringList m_history;
    QPointer<RecentActivity> m_recent;
    QWidget *m_recentSection;
    QListWidget *m_recentList;
    QLabel *m_recentEmpty;
    QPushButton *m_removeRecent;
    QPushButton *m_clearRecent;
};

#endif // SEARCHDASHBOARD_H
