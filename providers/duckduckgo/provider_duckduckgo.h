#ifndef PROVIDER_DUCKDUCKGO_H
#define PROVIDER_DUCKDUCKGO_H

#include "searchprovider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

class ProviderDuckDuckGo : public SearchProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
    Q_INTERFACES(SearchProvider)
public:
    explicit ProviderDuckDuckGo(QObject *parent = nullptr);
    virtual ~ProviderDuckDuckGo();

    QString name() const override;
    QString id() const override;
    void search(const QString &query) override;
    SearchProvider* clone(QObject *parent = nullptr) const override;

private slots:
    void onReplyFinished(QNetworkReply *reply);

protected:
    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
};

#endif // PROVIDER_DUCKDUCKGO_H
