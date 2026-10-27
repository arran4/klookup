#ifndef SEARCHDASHBOARD_H
#define SEARCHDASHBOARD_H

#include <QWidget>
#include <QLineEdit>
#include <QCompleter>

class SearchDashboard : public QWidget
{
    Q_OBJECT

public:
    explicit SearchDashboard(QWidget *parent = nullptr);
    ~SearchDashboard();

    void focusQuery();
    void loadHistory(const QStringList &history);
    QStringList saveHistory() const;
    void prepareQuery(const QString &query, bool focus = true);

signals:
    void searchRequested(const QString &query);

private slots:
    void onSearchTriggered();

private:
    QLineEdit *m_searchBar;
    QCompleter *m_completer;
    QStringList m_history;
};

#endif // SEARCHDASHBOARD_H
