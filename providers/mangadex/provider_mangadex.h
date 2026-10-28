#ifndef PROVIDER_MANGADEX_H
#define PROVIDER_MANGADEX_H

#include "searchcontinuation.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

class ProviderMangaDex : public SearchProvider, public SearchProviderContinuation
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
    Q_INTERFACES(SearchProvider SearchProviderContinuation)
public:
    explicit ProviderMangaDex(QObject *parent = nullptr);
    virtual ~ProviderMangaDex();

    QString name() const override;
    QString id() const override;
    void search(const QString &query) override;
    void requestPage(const QString &query, const QString &token, quint64 requestId) override;
    bool supportsDetails() const override
    {
        return true;
    }
    void requestDetails(const QString &resultId, const QString &token, quint64 requestId) override;
    void cancelRequests() override;
    SearchProvider *clone(QObject *parent = nullptr) const override;

private:
    SearchResult parseResult(const QJsonObject &obj) const;
    ResultMetadata parseMetadata(const QJsonObject &obj) const;

protected:
    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_detailReply;
};

#endif // PROVIDER_MANGADEX_H
