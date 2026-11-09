// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Do not include the current provider or authentication headers in this module.
#include "fixtures/legacy_providerauthentication.h"
class LegacyAuthAbiProvider : public SearchProvider, public ProviderAuthentication {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SearchProvider_iid)
    Q_INTERFACES(SearchProvider ProviderAuthentication)
public:
    using SearchProvider::SearchProvider;
    QString id() const override { return QStringLiteral("legacy_auth_abi_fixture"); }
    QString name() const override { return QStringLiteral("Legacy auth ABI fixture"); }
    void search(const QString &query) override {
        SearchResult result; result.id = QStringLiteral("legacy"); result.title = query;
        emit searchCompleted(id(), {result});
    }
    SearchProvider *clone(QObject *parent = nullptr) const override { return new LegacyAuthAbiProvider(parent); }
    AuthenticationDescriptor authenticationDescriptor() const override {
        AuthenticationDescriptor result;
        result.mode = AuthenticationMode::OAuth2Pkce;
        result.oauth.authorizationUrl = QUrl(QStringLiteral("https://example.invalid/authorize"));
        result.oauth.tokenUrl = QUrl(QStringLiteral("https://example.invalid/token"));
        result.oauth.scopes = {QStringLiteral("read")};
        result.oauth.callbackPath = QStringLiteral("/callback");
        return result;
    }
};
#include "provider_auth_abi_fixture.moc"
