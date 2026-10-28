#ifndef RECENTACTIVITY_H
#define RECENTACTIVITY_H

#include "searchprovider.h"
#include <KSharedConfig>
#include <QDateTime>
#include <QObject>
#include <QUrl>

struct RecentItem {
    enum class Kind { Search, Viewed, Captured };
    QString key;
    Kind kind = Kind::Search;
    QString title;
    QString query;
    QString snippet;
    QString source;
    QString resultId;
    QUrl url;
    QDateTime timestamp;
    bool clipboard = false;
};
Q_DECLARE_METATYPE(RecentItem)

class RecentActivity : public QObject
{
    Q_OBJECT
public:
    explicit RecentActivity(KSharedConfig::Ptr config, QObject *parent = nullptr);
    QList<RecentItem> items() const
    {
        return m_items;
    }
    bool shown() const
    {
        return m_shown;
    }
    void reloadPolicy();
    void recordSearch(const QString &query, bool clipboard = false);
    void recordCapture(const QString &query);
    void recordViewed(const QString &providerId, const SearchResult &result);
    void remove(const QString &key);
    void clear();
    void expire(const QDateTime &now = QDateTime::currentDateTimeUtc());
signals:
    void changed();

private:
    void append(RecentItem item);
    void save();
    bool permitted(const RecentItem &item) const;
    KSharedConfig::Ptr m_config;
    QList<RecentItem> m_items;
    bool m_loaded = false;
    bool m_shown = true;
    bool m_recordClipboard = false;
    bool m_persistClipboard = false;
};
#endif
