#ifndef SEARCHDASHBOARD_H
#define SEARCHDASHBOARD_H

#include <QWidget>
#include <QLineEdit>
#include <QListWidget>

class SearchDashboard : public QWidget
{
    Q_OBJECT

public:
    explicit SearchDashboard(QWidget *parent = nullptr);
    ~SearchDashboard();

    void loadHistory(const QStringList &history);
    QStringList saveHistory() const;

signals:
    void searchRequested(const QString &query);

private slots:
    void onSearchTriggered();
    void onHistoryItemClicked(QListWidgetItem *item);

private:
    QLineEdit *m_searchBar;
    QListWidget *m_historyList;
};

#endif // SEARCHDASHBOARD_H
