#include "providerauthmanager.h"

ProviderAuthManager::ProviderAuthManager(QWidget *window, QObject *parent)
    : QObject(parent), m_store(new WalletCredentialStore(window, this))
{
}

ProviderAuthSession *ProviderAuthManager::sessionFor(SearchProvider *provider)
{
    if (!provider)
        return nullptr;
    const auto descriptor = authenticationDescriptor(provider);
    if (descriptor.mode == AuthenticationMode::None)
        return nullptr;
    if (m_sessions.value(provider->id()))
        return m_sessions.value(provider->id());
    auto *session = new ProviderAuthSession(provider->id(), descriptor, m_store, this);
    m_sessions.insert(provider->id(), session);
    connect(provider, &SearchProvider::authenticationRejected, session, &ProviderAuthSession::invalidateAccessToken);
    connect(session, &ProviderAuthSession::credentialsChanged, provider,
            [provider](const QString &token) { provider->setCredentials({}, token); });
    return session;
}
