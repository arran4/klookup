#ifndef OAUTHREPLYHANDLER_H
#define OAUTHREPLYHANDLER_H

#include <QObject>
#include <QVariantMap>
#include <functional>
class QTcpServer;
class QNetworkReply;

// Fixed-origin, bounded loopback callback and redacted JSON token handling.
// No untrusted request URL, token body or provider error description is logged.
class OAuthReplyHandler : public QObject
{
    Q_OBJECT
public:
    OAuthReplyHandler(quint16 port, const QString &path, QObject *parent);
    QString callback() const;
    void networkReplyFinished(QNetworkReply *reply);
    bool isListening() const;
    void close();
    std::function<void(bool invalidGrant)> failure;

signals:
    void callbackReceived(const QVariantMap &values);
    void tokensReceived(const QVariantMap &tokens);

private:
    QTcpServer *m_server;
    QString m_path;
    QString m_callbackUrl;
    int m_clients = 0;
};
#endif
