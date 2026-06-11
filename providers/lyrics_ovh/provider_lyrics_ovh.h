#ifndef PROVIDER_LYRICS_OVH_H
#define PROVIDER_LYRICS_OVH_H

#include "searchprovider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class ProviderLyricsOvh : public SearchProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SearchProvider_iid)
    Q_INTERFACES(SearchProvider)
public:
    explicit ProviderLyricsOvh(QObject *parent = nullptr);
    virtual ~ProviderLyricsOvh();

    QString name() const override;
    QString id() const override;
    void search(const QString &query) override;
    SearchProvider* clone(QObject *parent = nullptr) const override;

private slots:
    void onReplyFinished();

private:
    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply;
};

#endif // PROVIDER_LYRICS_OVH_H
