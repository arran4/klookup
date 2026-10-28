#ifndef PROVIDERAUTHSESSION_H
#define PROVIDERAUTHSESSION_H

#include "credentialstore.h"
#include "providerauthentication.h"
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>

class OAuthCodeFlow;
class OAuthReplyHandler;

class ProviderAuthSession : public QObject
{
    Q_OBJECT
public:
    enum class State { Disconnected, Authorizing, Connected, Refreshing, Error };
    explicit ProviderAuthSession(const QString &providerId, const AuthenticationDescriptor &descriptor, CredentialStore *store,
                                 QObject *parent = nullptr);
    ~ProviderAuthSession() override;
    AuthenticationMode mode() const
    {
        return m_descriptor.mode;
    }
    State state() const
    {
        return m_state;
    }
    QString statusText() const;
    bool hasUsableToken() const;
    QString accessToken() const
    {
        return hasUsableToken() ? m_accessToken : QString();
    }
    QUrl callbackUrl() const;
    void connectOAuth(const QString &clientId, bool remember);
    void setManualToken(const QString &token, bool remember);
    void restore(const QString &clientId);
    void disconnectProvider();
    void invalidateAccessToken();
    void cancel();
    void ensureReady(QObject *context, std::function<void(bool, const QString &)> callback);
signals:
    void changed();
    void credentialsChanged(const QString &token);

protected:
    virtual QNetworkAccessManager *createNetworkManager();
    virtual bool openBrowser(const QUrl &url);
    virtual int operationTimeoutMs() const
    {
        return 120000;
    }

private:
    struct Waiting {
        QPointer<QObject> context;
        std::function<void(bool, const QString &)> callback;
    };
    void startFlow(bool refresh);
    void retireFlow();
    void fail(const QString &message);
    void finishWaiters(bool ok);
    void persist(bool interactive);
    void scheduleExpiryNotification();
    QString credentialKey() const;
    bool canUseLegacyKey() const;
    void acceptRestored(const CredentialStoreResult &result, quint64 generation);
    QByteArray fingerprint() const;
    bool validConfiguration() const;
    QString m_providerId;
    AuthenticationDescriptor m_descriptor;
    QPointer<CredentialStore> m_store;
    State m_state = State::Disconnected;
    QString m_accessToken;
    QString m_refreshToken;
    QDateTime m_expiresAt;
    QString m_clientId;
    QString m_message;
    QString m_expectedState;
    bool m_remember = false;
    bool m_persisted = false;
    quint64 m_generation = 0;
    QPointer<OAuthCodeFlow> m_flow;
    QPointer<OAuthReplyHandler> m_handler;
    QTimer m_timeout;
    QTimer m_expiryNotice;
    QList<Waiting> m_waiters;
};
#endif
