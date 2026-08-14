#ifndef PROVIDER_TVMAZE_H
#define PROVIDER_TVMAZE_H

#include "searchprovider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class ProviderTvMaze : public SearchProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
    Q_INTERFACES(SearchProvider)
public:
    explicit ProviderTvMaze(QObject *parent = nullptr);
    virtual ~ProviderTvMaze();

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

#endif // PROVIDER_TVMAZE_H
