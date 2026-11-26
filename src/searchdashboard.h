#ifndef SEARCHDASHBOARD_H
#define SEARCHDASHBOARD_H

#include "providerclassification.h"
#include "recentactivity.h"
#include <QMap>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QWidget>
#include <QLineEdit>
#include <QCompleter>

class FlowLayout;
class QListWidget;
class QLabel;
class QPushButton;
class QComboBox;
class QToolButton;

class SearchDashboard : public QWidget
{
    Q_OBJECT

public:
    enum class ActivityFilter {
        Searches = 0,
        All = 1,
        Viewed = 2,
        Captured = 3
    };
    Q_ENUM(ActivityFilter)

    explicit SearchDashboard(QWidget *parent = nullptr);
    ~SearchDashboard();

    void focusQuery();
    void loadHistory(const QStringList &history);
    QStringList saveHistory() const;
    void setRecentActivity(RecentActivity *activity);
    void prepareQuery(const QString &query, bool focus = true);
    bool prepareCapturedQuery(const QString &query, bool focus = false, bool forceOverwrite = false);
    void clearQuery();
    bool hasUserEditedQuery() const;
    QString currentQueryText() const;
    void setAvailableScopes(const QList<QPair<QString, ProviderClassification>> &sources);
    QueryScope selectedScope() const;
    QSet<QString> selectedSourceIds() const;
    void clearScopeSelection();

    ActivityFilter activityFilter() const;
    void setActivityFilter(ActivityFilter filter);

signals:
    void searchRequested(const QString &query);
    void searchScopedRequested(const QString &query, const QueryScope &scope);
    void searchInNewWindowRequested(const QString &query);
    void searchInNewWindowScopedRequested(const QString &query, const QueryScope &scope);
    void recentActivated(const RecentItem &item);

private slots:
    void onSearchTriggered();
    void refreshRecent();
    void updateScopePreview();

private:
    QLineEdit *m_searchBar;
    bool m_userEditedQuery = false;
    QCompleter *m_completer;
    QStringList m_history;
    QPointer<RecentActivity> m_recent;
    QWidget *m_scopeWidget = nullptr;
    FlowLayout *m_scopeLayout = nullptr;
    QLabel *m_scopePreview = nullptr;
    QToolButton *m_clearScope = nullptr;
    QList<QPair<QString, ProviderClassification>> m_availableSources;
    QMap<QString, QToolButton *> m_scopeButtons;
    QSet<QString> m_selectedScopeFacets;
    QWidget *m_recentSection;
    QListWidget *m_recentList;
    QLabel *m_recentEmpty;
    QComboBox *m_filterCombo;
    QPushButton *m_removeRecent;
    QPushButton *m_clearRecent;
};

#endif // SEARCHDASHBOARD_H
