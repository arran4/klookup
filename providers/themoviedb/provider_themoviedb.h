#ifndef PROVIDER_THEMOVIEDB_H
#define PROVIDER_THEMOVIEDB_H

#include "searchprovider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class ProviderTheMovieDB : public SearchProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SearchProvider_iid)
    Q_INTERFACES(SearchProvider)
public:
    explicit ProviderTheMovieDB(QObject *parent = nullptr);
    virtual ~ProviderTheMovieDB();

    QString name() const override;
    QString id() const override;
    void search(const QString &query) override;
    SearchProvider* clone(QObject *parent = nullptr) const override;

    bool supportsAuthentication() const override { return true; }
    void setCredentials(const QString &username, const QString &password) override;

private slots:
    void onReplyFinished();

private:
    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply;
    QString m_apiKey;
};

#endif // PROVIDER_THEMOVIEDB_H
