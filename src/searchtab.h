#ifndef SEARCHTAB_H
#define SEARCHTAB_H

#include <QWidget>
#include <QListWidget>
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
    void onResultClicked(QListWidgetItem *item);

private:
    QString m_query;
    QListWidget *m_resultsList;
    QTextBrowser *m_detailsView;
    QHash<QListWidgetItem*, SearchResult> m_itemDataMap;
};

#endif // SEARCHTAB_H
