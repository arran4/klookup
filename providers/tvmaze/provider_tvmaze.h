#ifndef PROVIDER_TVMAZE_H
#define PROVIDER_TVMAZE_H

#include "searchprovider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

class ProviderTvMaze : public SearchProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
    Q_INTERFACES(SearchProvider)
public:
    explicit ProviderTvMaze(QObject *parent = nullptr);
    explicit ProviderTvMaze(QObject *parent, QNetworkAccessManager *nam);
    virtual ~ProviderTvMaze();

    QString name() const override;
    QString id() const override;
    void search(const QString &query) override;
    SearchProvider* clone(QObject *parent = nullptr) const override;
    void setNetworkAccessManager(QNetworkAccessManager *nam) { m_nam = nam ? nam : new QNetworkAccessManager(this); }

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
};

#endif // PROVIDER_TVMAZE_H
