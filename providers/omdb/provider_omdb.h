#ifndef PROVIDER_OMDB_H
#define PROVIDER_OMDB_H

#include "searchprovider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

class ProviderOMDb : public SearchProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
    Q_INTERFACES(SearchProvider)
public:
    explicit ProviderOMDb(QObject *parent = nullptr);
    virtual ~ProviderOMDb();

    QString name() const override;
    QString id() const override;
    void search(const QString &query) override;
    SearchProvider* clone(QObject *parent = nullptr) const override;

    bool supportsAuthentication() const override { return true; }
    void setCredentials(const QString &username, const QString &password) override;

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
    QString m_apiKey;
};

#endif // PROVIDER_OMDB_H
