#ifndef SEARCHTAB_H
#define SEARCHTAB_H

#include <QWidget>
#include <QTreeWidget>
#include <QTextBrowser>
#include "searchprovider.h"

class SearchTab : public QWidget
{
    Q_OBJECT

public:
    explicit SearchTab(const QString &query, QWidget *parent = nullptr);
    ~SearchTab();

    void addResults(const QString &providerName, const QList<SearchResult> &results);
    void setError(const QString &providerName, const QString &errorMsg);

private slots:
    void onResultClicked(QTreeWidgetItem *item, int column);

private:
    QString m_query;
    QTreeWidget *m_resultsTree;
    QTextBrowser *m_detailsView;
};

#endif // SEARCHTAB_H
