#ifndef PROVIDERAUTHENTICATION_H
#define PROVIDERAUTHENTICATION_H

#include "searchprovider.h"
#include <QUrl>

enum class AuthenticationMode { None, ManualToken, OAuth2Pkce };
struct OAuthConfiguration {
    QUrl authorizationUrl;
    QUrl tokenUrl;
    QStringList scopes;
    quint16 callbackPort = 0; // zero only when the provider accepts dynamic loopback ports
    QString callbackPath = QStringLiteral("/callback");
};
struct AuthenticationDescriptor {
    AuthenticationMode mode = AuthenticationMode::None;
    OAuthConfiguration oauth;
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
