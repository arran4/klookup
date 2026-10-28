#ifndef SEARCHPROVIDER_H
#define SEARCHPROVIDER_H

#include "resultmetadata.h"
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

struct SearchResult {
    QString id;
    QString title;
    QString description;
    QString url;
    QString type;
    QVariantMap additionalData;
};
Q_DECLARE_METATYPE(SearchResult)

// Tokens belong to the provider. An empty nextToken means there is no more data.
struct SearchPage {
    QList<SearchResult> results;
    QString nextToken;
};
Q_DECLARE_METATYPE(SearchPage)

struct SearchDetails {
    SearchResult result;
    QString nextToken;
    ResultMetadata metadata = {};
};
Q_DECLARE_METATYPE(SearchDetails)

class SearchProvider : public QObject
{
    Q_OBJECT
public:
    explicit SearchProvider(QObject *parent = nullptr);
    virtual ~SearchProvider();

    virtual QString name() const = 0;
    virtual QString id() const = 0;

    virtual void search(const QString &query) = 0;

    bool isEnabled() const;
    void setEnabled(bool enabled);

    // Virtual clone function to allow creating discrete instances per tab
    virtual SearchProvider *clone(QObject *parent = nullptr) const = 0;

    // Authentication support
    virtual bool supportsAuthentication() const
    {
        return false;
    }
    virtual void setCredentials(const QString & /*username*/, const QString & /*password*/) {}

signals:
    void searchCompleted(const QString &providerId, const QList<SearchResult> &results);
    void searchFailed(const QString &providerId, const QString &errorString);

    void pageCompleted(quint64 requestId, const SearchPage &page);
    void detailsCompleted(quint64 requestId, const SearchDetails &details);
    void requestFailed(quint64 requestId, const QString &error, int retryAfterMs = 0);
    void authenticationRejected();

private:
    bool m_enabled = true;
};

#define SearchProvider_iid "org.kde.klookup.SearchProvider"
Q_DECLARE_INTERFACE(SearchProvider, "org.kde.klookup.SearchProvider")

#endif // SEARCHPROVIDER_H
