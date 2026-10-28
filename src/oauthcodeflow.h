#ifndef OAUTHCODEFLOW_H
#define OAUTHCODEFLOW_H

#include "providerauthentication.h"
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QPointer>
class OAuthReplyHandler;
class QNetworkReply;

// Public native client, RFC 6749/7636/8252. Only Authorization Code + S256 and
// refresh-token exchange are supported; there is no client-secret capability.
class OAuthCodeFlow : public QObject
{
    Q_OBJECT
public:
    OAuthCodeFlow(const OAuthConfiguration &configuration, const QString &clientId, QNetworkAccessManager *network, QObject *parent);
    ~OAuthCodeFlow() override;
    void authorize(OAuthReplyHandler *handler, const QString &state);
    void refresh(OAuthReplyHandler *handler, const QString &refreshToken);
    QString token() const
    {
        return m_accessToken;
    }
    QString refreshToken() const
    {
        return m_refreshToken;
    }
    QDateTime expirationAt() const
    {
        return m_expiresAt;
    }
signals:
    void browserRequested(const QUrl &url);
    void granted();

private:
    void attach(OAuthReplyHandler *handler);
    void requestToken(const QList<QPair<QString, QString>> &parameters);
    OAuthConfiguration m_configuration;
    QString m_clientId;
    QString m_verifier;
    QString m_accessToken;
    QString m_refreshToken;
    QDateTime m_expiresAt;
    QNetworkAccessManager *m_network;
    QPointer<OAuthReplyHandler> m_handler;
    QPointer<QNetworkReply> m_reply;
};
#endif
