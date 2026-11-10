// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef KLOOKUP_LEGACY_PROVIDER_AUTHENTICATION_H
#define KLOOKUP_LEGACY_PROVIDER_AUTHENTICATION_H
// Frozen /1.0 header, compiled separately to detect by-value ABI regressions.
#include "legacy_searchprovider.h"
#include <QUrl>
enum class AuthenticationMode { None, ManualToken, OAuth2Pkce };
struct OAuthConfiguration {
    QUrl authorizationUrl;
    QUrl tokenUrl;
    QStringList scopes;
    quint16 callbackPort = 0;
    QString callbackPath = QStringLiteral("/callback");
};
struct AuthenticationDescriptor {
    AuthenticationMode mode = AuthenticationMode::None;
    OAuthConfiguration oauth;
};
class ProviderAuthentication {
public:
    virtual ~ProviderAuthentication() = default;
    virtual AuthenticationDescriptor authenticationDescriptor() const = 0;
};
#define ProviderAuthentication_iid "org.kde.klookup.ProviderAuthentication/1.0"
Q_DECLARE_INTERFACE(ProviderAuthentication, ProviderAuthentication_iid)
#endif
