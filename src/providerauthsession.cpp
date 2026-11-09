#include "providerauthsession.h"
#include "oauthcodeflow.h"
#include "oauthreplyhandler.h"
#include "sourceurl.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrlQuery>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
bool containsControls(const QString &text, bool rejectSpace = false)
{
    for (const auto character : text)
        if (character.unicode() < 0x20 || character.unicode() == 0x7f || (rejectSpace && character == QLatin1Char(' ')))
            return true;
    return false;
}

QString randomState()
{
    QByteArray bytes(32, Qt::Uninitialized);
    for (int i = 0; i < bytes.size(); i += 4) {
        const quint32 value = QRandomGenerator::system()->generate();
        memcpy(bytes.data() + i, &value, 4);
    }
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

class AuthNetworkManager : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *data) override
    {
        auto guarded = request;
        guarded.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        guarded.setTransferTimeout(30000);
        auto *reply = QNetworkAccessManager::createRequest(operation, guarded, data);
        reply->setReadBufferSize(65537);
        connect(reply, &QNetworkReply::readyRead, reply, [reply] {
            if (reply->bytesAvailable() > 65536)
                reply->abort();
        });
        return reply;
    }
};
} // namespace

ProviderAuthSession::ProviderAuthSession(const QString &providerId, const AuthenticationDescriptor &descriptor, CredentialStore *store,
                                         QObject *parent)
    : QObject(parent), m_providerId(providerId), m_descriptor(descriptor), m_store(store)
{
    m_expiryNotice.setSingleShot(true);
    connect(&m_expiryNotice, &QTimer::timeout, this, [this] {
        if (hasUsableToken())
            scheduleExpiryNotification();
        QPointer<ProviderAuthSession> guard(this);
        if (!hasUsableToken())
            emit credentialsChanged(QString());
        if (guard)
            emit changed();
    });
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(QStringLiteral("Authorization timed out; connect again in Settings")); });
}
ProviderAuthSession::~ProviderAuthSession()
{
    ++m_generation;
    if (m_store)
        m_store->cancelPending(credentialKey());
    retireFlow();
}

bool ProviderAuthSession::hasUsableToken() const
{
    return !m_accessToken.isEmpty() && (!m_expiresAt.isValid() || m_expiresAt > QDateTime::currentDateTimeUtc());
}
QString ProviderAuthSession::statusText() const
{
    if (!m_message.isEmpty())
        return m_message;
    if (m_state == State::Connected && !hasUsableToken() && m_descriptor.mode == AuthenticationMode::OAuth2Pkce)
        return m_refreshToken.isEmpty() ? QStringLiteral("Access expired; reconnect in Settings")
                                        : QStringLiteral("Access expired; it will refresh on the next lookup");
    switch (m_state) {
    case State::Disconnected:
        return QStringLiteral("Not connected. Connect or restore saved credentials in Settings.");
    case State::Authorizing:
        return QStringLiteral("Waiting for browser authorization…");
    case State::Refreshing:
        return QStringLiteral("Refreshing access…");
    case State::Connected:
        return m_persisted ? QStringLiteral("Connected; credentials saved in KWallet") : QStringLiteral("Connected for this session only");
    case State::Error:
        return QStringLiteral("Authentication unavailable; connect again in Settings");
    }
    return {};
}
QUrl ProviderAuthSession::callbackUrl() const
{
    return m_handler ? QUrl(m_handler->callback()) : QUrl();
}
QString ProviderAuthSession::credentialKey() const
{
    return QStringLiteral("credentials/v1/") +
           QString::fromLatin1(m_providerId.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) +
           (m_descriptor.mode == AuthenticationMode::OAuth2Pkce ? QStringLiteral("/oauth") : QStringLiteral("/manual"));
}
bool ProviderAuthSession::canUseLegacyKey() const
{
    return mode() == AuthenticationMode::ManualToken &&
           QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]{1,128}$")).match(m_providerId).hasMatch();
}
QByteArray ProviderAuthSession::fingerprint() const
{
    auto scopes = m_descriptor.oauth.scopes;
    scopes.sort();
    const QString data = m_providerId + QLatin1Char('\n') + m_clientId + QLatin1Char('\n') +
                         m_descriptor.oauth.authorizationUrl.toString(QUrl::FullyEncoded) + QLatin1Char('\n') +
                         m_descriptor.oauth.tokenUrl.toString(QUrl::FullyEncoded) + QLatin1Char('\n') + scopes.join(QLatin1Char(' '));
    return QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha256).toHex();
}
bool ProviderAuthSession::validConfiguration() const
{
    const auto authorization = validatedSourceUrl(m_descriptor.oauth.authorizationUrl.toString(QUrl::FullyEncoded));
    const auto token = validatedSourceUrl(m_descriptor.oauth.tokenUrl.toString(QUrl::FullyEncoded));
    return !m_clientId.trimmed().isEmpty() && authorization.scheme() == QStringLiteral("https") &&
           token.scheme() == QStringLiteral("https") && !authorization.hasFragment() && !token.hasFragment() &&
           QRegularExpression(QStringLiteral("^/[A-Za-z0-9/_-]*$")).match(m_descriptor.oauth.callbackPath).hasMatch();
}
QNetworkAccessManager *ProviderAuthSession::createNetworkManager()
{
    return new AuthNetworkManager;
}
bool ProviderAuthSession::openBrowser(const QUrl &url)
{
    return QDesktopServices::openUrl(url);
}

void ProviderAuthSession::scheduleExpiryNotification()
{
    m_expiryNotice.stop();
    if (!m_expiresAt.isValid())
        return;
    const qint64 delay = QDateTime::currentDateTimeUtc().msecsTo(m_expiresAt);
    m_expiryNotice.start(int(qBound(qint64(0), delay, qint64(std::numeric_limits<int>::max()))));
}

void ProviderAuthSession::invalidateAccessToken()
{
    m_accessToken.clear();
    m_expiresAt = QDateTime();
    m_expiryNotice.stop();
    if (m_store)
        m_store->cancelPending(credentialKey());
    m_persisted = false;
    m_message = QStringLiteral("Provider rejected access; retry to refresh or reconnect in Settings");
    if (m_state != State::Refreshing)
        m_state = State::Error;
    QPointer<ProviderAuthSession> guard(this);
    emit credentialsChanged(QString());
    if (guard)
        emit changed();
}

void ProviderAuthSession::retireFlow()
{
    m_timeout.stop();
    if (m_handler) {
        m_handler->close();
        m_handler->disconnect();
    }
    if (m_flow) {
        m_flow->disconnect(this);
        m_flow->deleteLater();
    }
    m_handler = nullptr;
    m_flow = nullptr;
}
void ProviderAuthSession::finishWaiters(bool ok)
{
    const auto waiting = std::exchange(m_waiters, {});
    const QString message = ok ? QString() : statusText();
    const quint64 generation = m_generation;
    QPointer<ProviderAuthSession> guard(this);
    for (const auto &entry : waiting) {
        if (!entry.context)
            continue;
        // A consumer may disconnect, reconnect or destroy this session while
        // handling its callback. Remaining lookups must not use stale success.
        const bool unchanged = guard && guard->m_generation == generation;
        entry.callback(ok && unchanged, unchanged ? message : QStringLiteral("Authentication changed; retry this lookup"));
    }
}
void ProviderAuthSession::fail(const QString &message)
{
    ++m_generation;
    retireFlow();
    m_state = State::Error;
    m_message = message;
    QPointer<ProviderAuthSession> guard(this);
    if (!hasUsableToken())
        emit credentialsChanged(QString());
    if (!guard)
        return;
    emit changed();
    if (guard)
        finishWaiters(false);
}

void ProviderAuthSession::cancel()
{
    ++m_generation;
    if (m_store) {
        m_store->cancelPending(credentialKey());
        if (canUseLegacyKey())
            m_store->cancelPending(m_providerId);
    }
    retireFlow();
    m_state = hasUsableToken() ? State::Connected : State::Disconnected;
    m_message = QStringLiteral("Authorization cancelled");
    QPointer<ProviderAuthSession> guard(this);
    emit changed();
    if (guard)
        finishWaiters(false);
}

void ProviderAuthSession::startFlow(bool refreshing)
{
    if (!validConfiguration()) {
        fail(QStringLiteral("A client ID and valid HTTPS provider endpoints are required"));
        return;
    }
    retireFlow();
    if (m_store)
        m_store->cancelPending(credentialKey());
    const quint64 generation = ++m_generation;
    auto *network = createNetworkManager();
    network->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
    network->setTransferTimeout(30000);
    m_flow = new OAuthCodeFlow(m_descriptor.oauth, m_clientId, network, this);
    m_expectedState = randomState();
    auto *handler = new OAuthReplyHandler(refreshing ? 0 : m_descriptor.oauth.callbackPort, m_descriptor.oauth.callbackPath, m_flow);
    m_handler = handler;
    handler->failure = [this, generation](bool invalidGrant) {
        if (generation != m_generation)
            return;
        if (invalidGrant)
            m_refreshToken.clear();
        fail(QStringLiteral("Token request failed or was rejected; reconnect in Settings"));
    };
    if (!handler->isListening()) {
        fail(QStringLiteral("Cannot listen on the registered loopback callback port"));
        return;
    }
    connect(m_handler, &OAuthReplyHandler::callbackReceived, this, [this, generation](const QVariantMap &values) {
        if (generation != m_generation)
            return;
        if (values.value(QStringLiteral("state")).toString() != m_expectedState) {
            fail(QStringLiteral("Authorization callback state did not match; connect again"));
        } else if (values.contains(QStringLiteral("error"))) {
            fail(QStringLiteral("Authorization was denied or cancelled by the provider"));
        } else if (values.value(QStringLiteral("code")).metaType().id() != QMetaType::QString ||
                   values.value(QStringLiteral("code")).toString().isEmpty() ||
                   values.value(QStringLiteral("code")).toString().size() > 8192) {
            fail(QStringLiteral("Authorization callback did not contain a valid code"));
        } else
            m_handler->close();
    });
    // Validate before Qt consumes tokens or a callback. Errors never include the
    // response body, authorization code, state, verifier or provider description.
    connect(m_handler, &OAuthReplyHandler::tokensReceived, this, [this, generation](const QVariantMap &tokens) {
        if (generation != m_generation)
            return;
        const auto access = tokens.value(QStringLiteral("access_token"));
        const auto refresh = tokens.value(QStringLiteral("refresh_token"));
        const auto type = tokens.value(QStringLiteral("token_type")).toString();
        bool lifetimeOk = true;
        if (tokens.contains(QStringLiteral("expires_in"))) {
            bool ok = false;
            const qint64 lifetime = tokens.value(QStringLiteral("expires_in")).toLongLong(&ok);
            lifetimeOk =
                ok &&
                QRegularExpression(QStringLiteral("^[0-9]+$")).match(tokens.value(QStringLiteral("expires_in")).toString()).hasMatch() &&
                lifetime > 0 && lifetime <= 315360000;
        }
        const bool invalidAccess = access.metaType().id() != QMetaType::QString || access.toString().isEmpty() ||
                                   access.toString().size() > 65536 || containsControls(access.toString(), true);
        const bool invalidRefresh = refresh.isValid() && (refresh.metaType().id() != QMetaType::QString ||
                                                          refresh.toString().size() > 65536 || containsControls(refresh.toString()));
        if (invalidAccess || invalidRefresh ||
            (tokens.value(QStringLiteral("token_type")).metaType().id() != QMetaType::QString ||
             type.compare(QStringLiteral("Bearer"), Qt::CaseInsensitive) != 0) ||
            !lifetimeOk) {
            fail(QStringLiteral("Provider returned an invalid token response"));
        }
    });
    connect(m_flow, &OAuthCodeFlow::browserRequested, this, [this, generation](const QUrl &url) {
        if (generation == m_generation && !openBrowser(url))
            fail(QStringLiteral("Cannot open the system browser for authorization"));
    });
    connect(m_flow, &OAuthCodeFlow::granted, this, [this, generation, refreshing] {
        if (generation != m_generation || !m_flow)
            return;
        const QString token = m_flow->token();
        if (token.isEmpty()) {
            fail(QStringLiteral("Provider returned no access token"));
            return;
        }
        m_accessToken = token;
        if (!m_flow->refreshToken().isEmpty())
            m_refreshToken = m_flow->refreshToken();
        m_expiresAt = m_flow->expirationAt();
        scheduleExpiryNotification();
        m_state = State::Connected;
        m_persisted = false;
        m_message = m_remember ? QStringLiteral("Connected; saving credentials to KWallet…") : QString();
        retireFlow();
        QPointer<ProviderAuthSession> guard(this);
        emit credentialsChanged(m_accessToken);
        if (!guard)
            return;
        emit changed();
        if (!guard)
            return;
        finishWaiters(true);
        if (!guard)
            return;
        if (m_remember)
            persist(!refreshing);
    });
    m_state = refreshing ? State::Refreshing : State::Authorizing;
    m_message.clear();
    QPointer<ProviderAuthSession> guard(this);
    emit changed();
    if (!guard)
        return;
    m_timeout.start(refreshing ? qMin(operationTimeoutMs(), 30000) : operationTimeoutMs());
    if (refreshing)
        m_flow->refresh(m_handler, m_refreshToken);
    else
        m_flow->authorize(m_handler, m_expectedState);
}

void ProviderAuthSession::connectOAuth(const QString &clientId, bool remember)
{
    if (m_descriptor.mode != AuthenticationMode::OAuth2Pkce)
        return;
    QPointer<ProviderAuthSession> guard(this);
    cancel();
    if (!guard)
        return;
    m_clientId = clientId.trimmed();
    m_remember = remember;
    m_persisted = false;
    m_accessToken.clear();
    m_refreshToken.clear();
    m_expiresAt = QDateTime();
    m_expiryNotice.stop();
    emit credentialsChanged(QString());
    if (guard)
        startFlow(false);
}
void ProviderAuthSession::setManualToken(const QString &token, bool remember)
{
    if (m_descriptor.mode != AuthenticationMode::ManualToken)
        return;
    if (containsControls(token)) {
        fail(QStringLiteral("API keys/tokens cannot contain control characters"));
        return;
    }
    QPointer<ProviderAuthSession> guard(this);
    cancel();
    if (!guard)
        return;
    m_accessToken = token;
    m_expiresAt = QDateTime();
    m_remember = remember;
    m_persisted = false;
    m_state = token.isEmpty() ? State::Disconnected : State::Connected;
    m_message.clear();
    emit credentialsChanged(token);
    if (!guard)
        return;
    emit changed();
    if (!guard)
        return;
    if (remember && !token.isEmpty())
        persist(true);
}
void ProviderAuthSession::persist(bool interactive)
{
    if (!m_store) {
        m_remember = false;
        m_message = QStringLiteral("Connected for this session only; KWallet is unavailable");
        emit changed();
        return;
    }
    QString value = m_accessToken;
    if (m_descriptor.mode == AuthenticationMode::OAuth2Pkce) {
        const QJsonObject saved{{QStringLiteral("accessToken"), m_accessToken},
                                {QStringLiteral("refreshToken"), m_refreshToken},
                                {QStringLiteral("expiresAt"), m_expiresAt.toString(Qt::ISODateWithMs)},
                                {QStringLiteral("binding"), QString::fromLatin1(fingerprint())}};
        value = QString::fromUtf8(QJsonDocument(saved).toJson(QJsonDocument::Compact));
    }
    const quint64 generation = m_generation;
    m_message = QStringLiteral("Connected; saving credentials to KWallet…");
    QPointer<ProviderAuthSession> guard(this);
    emit changed();
    if (!guard)
        return;
    m_store->write(credentialKey(), value, interactive, [guard, generation](const CredentialStoreResult &result) {
        if (!guard || generation != guard->m_generation)
            return;
        if (result.ok) {
            guard->m_message.clear();
            guard->m_persisted = true;
        } else {
            guard->m_remember = false;
            guard->m_message = QStringLiteral("Connected for this session only; ") + result.error;
        }
        emit guard->changed();
    });
}
void ProviderAuthSession::restore(const QString &clientId)
{
    QPointer<ProviderAuthSession> guard(this);
    cancel();
    if (!guard)
        return;
    m_accessToken.clear();
    m_refreshToken.clear();
    m_expiresAt = QDateTime();
    m_expiryNotice.stop();
    m_persisted = false;
    emit credentialsChanged(QString());
    if (!guard)
        return;
    m_clientId = clientId.trimmed();
    if (!m_store) {
        fail(QStringLiteral("KWallet is unavailable"));
        return;
    }
    if (m_descriptor.mode == AuthenticationMode::OAuth2Pkce && !validConfiguration()) {
        fail(QStringLiteral("Configure the provider client ID before restoring credentials"));
        return;
    }
    const quint64 generation = ++m_generation;
    m_message = QStringLiteral("Restoring saved credentials from KWallet…");
    emit changed();
    if (!guard)
        return;
    m_store->read(credentialKey(), true, [guard, generation](const CredentialStoreResult &result) {
        if (!guard || generation != guard->m_generation)
            return;
        if (!result.ok && result.code == CredentialStoreResult::Code::NotFound && guard->canUseLegacyKey()) {
            guard->m_store->read(guard->m_providerId, true, [guard, generation](const CredentialStoreResult &legacy) {
                if (guard)
                    guard->acceptRestored(legacy, generation);
            });
        } else
            guard->acceptRestored(result, generation);
    });
}
void ProviderAuthSession::acceptRestored(const CredentialStoreResult &result, quint64 generation)
{
    if (generation != m_generation)
        return;
    if (!result.ok) {
        fail(result.error);
        return;
    }
    QString access = result.value;
    QString refresh;
    QDateTime expiry;
    if (mode() == AuthenticationMode::OAuth2Pkce) {
        const auto doc = QJsonDocument::fromJson(result.value.toUtf8());
        const auto saved = doc.object();
        if (!doc.isObject() || saved.value(QStringLiteral("binding")).toString().toLatin1() != fingerprint() ||
            !saved.value(QStringLiteral("accessToken")).isString() || !saved.value(QStringLiteral("refreshToken")).isString() ||
            !saved.value(QStringLiteral("expiresAt")).isString()) {
            fail(QStringLiteral("Saved credentials do not match this provider/client configuration; reconnect"));
            return;
        }
        access = saved.value(QStringLiteral("accessToken")).toString();
        refresh = saved.value(QStringLiteral("refreshToken")).toString();
        const QString storedExpiry = saved.value(QStringLiteral("expiresAt")).toString();
        expiry = QDateTime::fromString(storedExpiry, Qt::ISODateWithMs);
        if ((!storedExpiry.isEmpty() && !expiry.isValid()) || (access.isEmpty() && refresh.isEmpty()) || access.size() > 65536 ||
            refresh.size() > 65536) {
            fail(QStringLiteral("Saved credentials are malformed; reconnect"));
            return;
        }
    }
    if (access.size() > 65536 || refresh.size() > 65536 || containsControls(access, mode() == AuthenticationMode::OAuth2Pkce) ||
        containsControls(refresh)) {
        fail(QStringLiteral("Saved credentials are malformed; reconnect"));
        return;
    }
    m_accessToken = access;
    m_refreshToken = refresh;
    m_expiresAt = expiry;
    scheduleExpiryNotification();
    m_remember = true;
    m_persisted = true;
    m_state = State::Connected;
    m_message.clear();
    QPointer<ProviderAuthSession> guard(this);
    emit credentialsChanged(accessToken());
    if (guard)
        emit changed();
}
void ProviderAuthSession::disconnectProvider()
{
    QPointer<ProviderAuthSession> guard(this);
    cancel();
    if (!guard)
        return;
    m_accessToken.clear();
    m_refreshToken.clear();
    m_expiresAt = QDateTime();
    m_expiryNotice.stop();
    m_remember = false;
    m_persisted = false;
    m_state = State::Disconnected;
    m_message = QStringLiteral("Disconnected for this session; removing saved credentials…");
    emit credentialsChanged(QString());
    if (!guard)
        return;
    emit changed();
    if (!guard)
        return;
    const quint64 generation = m_generation;
    if (!m_store) {
        m_message = QStringLiteral("Disconnected for this session; stored credentials could not be removed");
        emit changed();
        return;
    }
    auto report = [guard, generation](const CredentialStoreResult &result) {
        if (!guard || generation != guard->m_generation)
            return;
        guard->m_message = result.ok ? QStringLiteral("Disconnected; saved credentials removed")
                                     : QStringLiteral("Disconnected for this session; saved credentials may remain: ") + result.error;
        emit guard->changed();
    };
    m_store->remove(credentialKey(), true, [guard, generation, report](const CredentialStoreResult &result) {
        if (!guard || generation != guard->m_generation)
            return;
        if (result.ok && guard->canUseLegacyKey())
            guard->m_store->remove(guard->m_providerId, true, report);
        else
            report(result);
    });
}
void ProviderAuthSession::ensureReady(QObject *context, std::function<void(bool, const QString &)> callback)
{
    const bool refreshDue = m_expiresAt.isValid() && m_expiresAt <= QDateTime::currentDateTimeUtc().addSecs(30);
    if (m_descriptor.mode != AuthenticationMode::OAuth2Pkce || (hasUsableToken() && (!refreshDue || m_refreshToken.isEmpty()))) {
        callback(true, {});
        return;
    }
    if (m_refreshToken.isEmpty() || m_state == State::Authorizing) {
        callback(false, QStringLiteral("Connect or restore this provider in Settings before searching"));
        return;
    }
    m_waiters.append({context, std::move(callback)});
    if (m_state != State::Refreshing)
        startFlow(true);
}
