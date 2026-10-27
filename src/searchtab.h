#ifndef SEARCHTAB_H
#define SEARCHTAB_H

#include "searchprovider.h"
#include <QDateTime>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QWidget>

class QPushButton;

class SearchTab : public QWidget
{
    Q_OBJECT
public:
    explicit SearchTab(const QString &query, QWidget *parent = nullptr);
    ~SearchTab() override;
    void startProvider(SearchProvider *provider);
    void addResults(const QString &providerName, const QList<SearchResult> &results);
    void setError(const QString &providerName, const QString &errorMsg);

public slots:
    void loadMore(const QString &providerId);
    void loadDetails(bool refresh = false);

private slots:
    void onResultClicked(QTreeWidgetItem *item, int column);

private:
    struct ProviderState {
        QPointer<SearchProvider> provider;
        QTreeWidgetItem *group = nullptr;
        QTreeWidgetItem *status = nullptr;
        QMap<QString, QTreeWidgetItem *> items;
        QString nextToken;
        QSet<QString> seenTokens;
        quint64 pending = 0;
        bool started = false;
        QDateTime retryAt;
    };
    QTreeWidgetItem *groupForName(const QString &name);
    void appendResults(QTreeWidgetItem *group, const QList<SearchResult> &results, ProviderState *state = nullptr);
    void pageStatus(ProviderState &state, const QString &text, bool actionable);
    void renderResult(QTreeWidgetItem *item);
    void updateDetailsControl();
    QString m_query;
    QTreeWidget *m_resultsTree;
    QTextBrowser *m_detailsView;
    QPushButton *m_detailsButton;
    QMap<QString, ProviderState> m_states;
    quint64 m_nextRequest = 0;
    quint64 m_detailRequest = 0;
    QString m_detailProvider;
    QString m_detailId;
    QDateTime m_detailRetryAt;
    QTreeWidgetItem *m_selected = nullptr;
};
#endif
