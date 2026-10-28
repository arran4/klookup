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
class ProviderAuthSession;
class SourceLink;
class QMenu;

class SearchTab : public QWidget
{
    Q_OBJECT
public:
    explicit SearchTab(const QString &query, QWidget *parent = nullptr);
    ~SearchTab() override;
    QMenu *createResultMenu(QTreeWidgetItem *item);
    void startProvider(SearchProvider *provider, ProviderAuthSession *authentication = nullptr);
    void addResults(const QString &providerName, const QList<SearchResult> &results);
    void setError(const QString &providerName, const QString &errorMsg);

public slots:
    void loadMore(const QString &providerId);
    void loadDetails(bool refresh = false);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onResultClicked(QTreeWidgetItem *item, int column);

private:
    struct ProviderState {
        QPointer<SearchProvider> provider;
        QPointer<ProviderAuthSession> authentication;
        QTreeWidgetItem *group = nullptr;
        QTreeWidgetItem *status = nullptr;
        QMap<QString, QTreeWidgetItem *> items;
        QString nextToken;
        QSet<QString> seenTokens;
        quint64 pending = 0;
        bool started = false;
        QDateTime retryAt;
        QDateTime detailRetryAt;
    };
    QTreeWidgetItem *groupForName(const QString &name);
    void appendResults(QTreeWidgetItem *group, const QList<SearchResult> &results, ProviderState *state = nullptr);
    void showResultMenu(QTreeWidgetItem *item, const QPoint &globalPosition);
    void pageStatus(ProviderState &state, const QString &text, bool actionable);
    void renderResult(QTreeWidgetItem *item);
    void renderMetadata(const ResultMetadata &metadata);
    void loadInitialDetails();
    void updateDetailsControl();
    QString m_query;
    QTreeWidget *m_resultsTree;
    QTextBrowser *m_detailsView;
    QTreeWidget *m_metadataView;
    SourceLink *m_sourceLink;
    QPushButton *m_detailsButton;
    QMap<QString, ProviderState> m_states;
    quint64 m_nextRequest = 0;
    quint64 m_detailRequest = 0;
    QString m_detailProvider;
    QString m_detailId;
    QTreeWidgetItem *m_selected = nullptr;
};
#endif
