#ifndef PROVIDERAUTHENTICATION_H
#define PROVIDERAUTHENTICATION_H

#include "searchprovider.h"
#include <QUrl>
#include <QJsonValue>
#include <QMetaType>
#include <QVariant>

enum class AuthenticationMode { None, ManualToken, OAuth2Pkce, Unsupported };
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
// The original descriptor and the /1.0 interface above/below retain their exact
// binary layout. Never extend either in place: native plugins return v1 by value.
struct AuthenticationDescriptorV2 : AuthenticationDescriptor {
    QString publicClientId;
    QString tokenLabel = QStringLiteral("API key / token");
    QString guidance;
    bool executionSupported = true;
    bool walletSupported = true;
    QStringList declaredMethods;
    bool usable() const { return executionSupported && (mode == AuthenticationMode::ManualToken || mode == AuthenticationMode::OAuth2Pkce); }
    QStringList methods() const;
    static bool fromManifest(const QJsonValue &value, AuthenticationDescriptorV2 *result, QString *error);
};
Q_DECLARE_METATYPE(AuthenticationDescriptorV2)

class ProviderAuthentication
{
public:
    virtual ~ProviderAuthentication() = default;
    virtual AuthenticationDescriptor authenticationDescriptor() const = 0;
};
#define ProviderAuthentication_iid "org.kde.klookup.ProviderAuthentication/1.0"
Q_DECLARE_INTERFACE(ProviderAuthentication, ProviderAuthentication_iid)

class ProviderAuthenticationV2 {
public:
    virtual ~ProviderAuthenticationV2() = default;
    virtual AuthenticationDescriptorV2 authenticationDescriptorV2() const = 0;
};
#define ProviderAuthenticationV2_iid "org.kde.klookup.ProviderAuthentication/2.0"
Q_DECLARE_INTERFACE(ProviderAuthenticationV2, ProviderAuthenticationV2_iid)

inline AuthenticationDescriptor authenticationDescriptor(SearchProvider *provider)
{
    if (auto *capability = qobject_cast<ProviderAuthentication *>(provider))
        return capability->authenticationDescriptor();
    return {provider && provider->supportsAuthentication() ? AuthenticationMode::ManualToken : AuthenticationMode::None, {}};
}
// A new IID and a QObject property allow v2 metadata without touching the v1
// provider interface or ProviderV1's existing virtual function order.
inline AuthenticationDescriptorV2 authenticationDescriptorV2(QObject *provider)
{
    if (auto *capability = qobject_cast<ProviderAuthenticationV2 *>(provider))
        return capability->authenticationDescriptorV2();
    if (provider) {
        const auto property = provider->property("klookupAuthenticationV2");
        if (property.canConvert<AuthenticationDescriptorV2>())
            return property.value<AuthenticationDescriptorV2>();
    }
    AuthenticationDescriptorV2 result;
    if (auto *legacy = qobject_cast<SearchProvider *>(provider)) {
        const auto original = authenticationDescriptor(legacy);
        result.mode = original.mode;
        result.oauth = original.oauth;
    }
    return result;
}
#endif
