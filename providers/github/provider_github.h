#ifndef PROVIDER_GITHUB_H
#define PROVIDER_GITHUB_H

#include "searchprovider.h"
#include "providercheck.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

class ProviderGithub : public SearchProvider, public ProviderCheckV1
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
    Q_INTERFACES(SearchProvider ProviderCheckV1)
public:
    explicit ProviderGithub(QObject *parent = nullptr);
    virtual ~ProviderGithub();

    QJsonObject selfTestV1() const override;
    static QList<SearchResult> parseResponse(const QByteArray &, bool *ok);

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
    QString m_authToken;
};

#endif // PROVIDER_GITHUB_H
