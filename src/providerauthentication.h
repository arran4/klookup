#ifndef PROVIDERAUTHENTICATION_H
#define PROVIDERAUTHENTICATION_H

#include "searchprovider.h"
#include <QUrl>
#include <QJsonValue>

enum class AuthenticationMode { None, ManualToken, OAuth2Pkce, Unsupported };
struct OAuthConfiguration {
    QUrl authorizationUrl;
    QUrl tokenUrl;
    QStringList scopes;
    quint16 callbackPort = 0; // zero only when the provider accepts dynamic loopback ports
    QString callbackPath = QStringLiteral("/callback");
    QString publicClientId; // If supplied by the provider, no editable field is needed.
};
struct AuthenticationDescriptor {
    AuthenticationMode mode = AuthenticationMode::None;
    OAuthConfiguration oauth;
    QString tokenLabel = QStringLiteral("API key / token");
    QString guidance;
    bool executionSupported = true;
    bool walletSupported = true;
    QStringList declaredMethods;
    bool usable() const { return executionSupported && (mode == AuthenticationMode::ManualToken || mode == AuthenticationMode::OAuth2Pkce); }
    QStringList methods() const;
    static bool fromManifest(const QJsonValue &value, AuthenticationDescriptor *result, QString *error);
};
class ProviderAuthentication
{
public:
    virtual ~ProviderAuthentication() = default;
    virtual AuthenticationDescriptor authenticationDescriptor() const = 0;
};
#define ProviderAuthentication_iid "org.kde.klookup.ProviderAuthentication/1.0"
Q_DECLARE_INTERFACE(ProviderAuthentication, ProviderAuthentication_iid)

inline AuthenticationDescriptor authenticationDescriptor(SearchProvider *provider)
{
    if (auto *capability = qobject_cast<ProviderAuthentication *>(provider))
        return capability->authenticationDescriptor();
    return {provider && provider->supportsAuthentication() ? AuthenticationMode::ManualToken : AuthenticationMode::None, {}};
}
#endif
