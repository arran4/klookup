#ifndef PROVIDER_SPOTIFY_H
#define PROVIDER_SPOTIFY_H

#include "providerauthentication.h"
#include "searchcontinuation.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QStringList>

class ProviderSpotify : public SearchProvider, public SearchProviderContinuation, public ProviderAuthentication
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
    Q_INTERFACES(SearchProvider SearchProviderContinuation ProviderAuthentication)
public:
    explicit ProviderSpotify(QObject *parent = nullptr);
    ~ProviderSpotify() override;

    QString name() const override;
    QString id() const override;

    bool supportsAuthentication() const override { return true; }
    void setCredentials(const QString &username, const QString &password) override;
    AuthenticationDescriptor authenticationDescriptor() const override;

    void search(const QString &query) override;
    void requestPage(const QString &query, const QString &token, quint64 requestId) override;
    bool supportsDetails() const override { return true; }
    void requestDetails(const QString &resultId, const QString &token, quint64 requestId) override;
    void cancelRequests() override;
    SearchProvider *clone(QObject *parent = nullptr) const override;

    static QStringList supportedTypes();
    static QStringList defaultTypes();
    QStringList selectedTypes() const { return m_types; }
    void setSelectedTypes(const QStringList &types);

private:
    void sendJsonRequest(QPointer<QNetworkReply> &slot, const QUrl &url, quint64 requestId,
                         std::function<void(const QJsonObject &)> onSuccess);
    SearchResult parseItem(const QJsonObject &obj, const QString &fallbackType = QString()) const;
    ResultMetadata parseMetadata(const QJsonObject &obj, const QString &itemType) const;

protected:
    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_detailReply;
    QString m_accessToken;
    QStringList m_types;
};

#endif // PROVIDER_SPOTIFY_H
