#include "../src/providerauthmanager.h"
#include "../src/providerauthsession.h"
#include "../src/searchtab.h"
#include "../src/settingsdialog.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include <cstring>

class MemoryCredentials : public CredentialStore
{
public:
    using CredentialStore::CredentialStore;
    QMap<QString, QString> saved;
    QList<bool> interactions;
    int reads = 0;
    int writes = 0;
    bool failWrites = false;
    bool deferWrites = false;
    struct Pending {
        QString key;
        QString value;
        Callback callback;
    };
    QList<Pending> pending;
    void cancelPending(const QString &key) override
    {
        pending.removeIf([&](const Pending &item) { return item.key == key; });
    }
    void read(const QString &key, bool interactive, Callback callback) override
    {
        ++reads;
        interactions.append(interactive);
        callback(saved.contains(key)
                     ? CredentialStoreResult{true, saved.value(key), {}}
                     : CredentialStoreResult{false, {}, QStringLiteral("Not found"), CredentialStoreResult::Code::NotFound});
    }
    void write(const QString &key, const QString &value, bool interactive, Callback callback) override
    {
        ++writes;
        interactions.append(interactive);
        if (deferWrites) {
            pending.append({key, value, callback});
            return;
        }
        if (failWrites)
            callback({false, {}, QStringLiteral("Wallet is unavailable")});
        else {
            saved.insert(key, value);
            callback({true, {}, {}});
        }
    }
    void remove(const QString &key, bool interactive, Callback callback) override
    {
        interactions.append(interactive);
        saved.remove(key);
        callback({true, {}, {}});
    }
};

struct AuthRequest {
    QNetworkAccessManager::Operation operation;
    QUrl url;
    QByteArray body;
};
class AuthReply : public QNetworkReply
{
    QByteArray m_bytes;

public:
    AuthReply(const QNetworkRequest &request, QNetworkAccessManager::Operation operation, const QByteArray &bytes, QObject *parent)
        : QNetworkReply(parent), m_bytes(bytes)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setOpenMode(QIODevice::ReadOnly);
    }
    void finish()
    {
        setFinished(true);
        emit readyRead();
        emit finished();
    }
    void abort() override
    {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Cancelled"));
        setFinished(true);
        emit finished();
    }
    bool isSequential() const override
    {
        return true;
    }
    qint64 bytesAvailable() const override
    {
        return m_bytes.size() + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 count = qMin(maxSize, qint64(m_bytes.size()));
        memcpy(data, m_bytes.constData(), count);
        m_bytes.remove(0, count);
        return count;
    }
};
class ScriptedAuthNetwork : public QNetworkAccessManager
{
    QList<AuthRequest> *m_requests;
    QList<QByteArray> *m_responses;

public:
    ScriptedAuthNetwork(QList<AuthRequest> *requests, QList<QByteArray> *responses) : m_requests(requests), m_responses(responses) {}

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *data) override
    {
        m_requests->append({operation, request.url(), data ? data->readAll() : QByteArray()});
        auto *reply = new AuthReply(request, operation, m_responses->isEmpty() ? QByteArray("{}") : m_responses->takeFirst(), this);
        QTimer::singleShot(0, reply, &AuthReply::finish);
        return reply;
    }
};
class TestAuthSession : public ProviderAuthSession
{
public:
    using ProviderAuthSession::ProviderAuthSession;
    QList<AuthRequest> requests;
    QList<QByteArray> responses;
    QList<QUrl> browserUrls;
    int timeout = 1000;

protected:
    QNetworkAccessManager *createNetworkManager() override
    {
        return new ScriptedAuthNetwork(&requests, &responses);
    }
    bool openBrowser(const QUrl &url) override
    {
        browserUrls.append(url);
        return true;
    }
    int operationTimeoutMs() const override
    {
        return timeout;
    }
};

class AuthTestProvider : public SearchProvider, public ProviderAuthentication
{
    Q_OBJECT
    Q_INTERFACES(SearchProvider ProviderAuthentication)
public:
    QString token;
    int searches = 0;
    explicit AuthTestProvider(QObject *parent = nullptr) : SearchProvider(parent) {}
    QString name() const override
    {
        return QStringLiteral("OAuth test provider");
    }
    QString id() const override
    {
        return QStringLiteral("oauth-test");
    }
    AuthenticationDescriptor authenticationDescriptor() const override
    {
        return {AuthenticationMode::OAuth2Pkce,
                {QUrl(QStringLiteral("https://identity.example/authorize")),
                 QUrl(QStringLiteral("https://identity.example/token")),
                 {QStringLiteral("catalogue.read")},
                 0,
                 QStringLiteral("/callback")}};
    }
    void setCredentials(const QString &, const QString &access) override
    {
        token = access;
    }
    void search(const QString &query) override
    {
        ++searches;
        if (token.isEmpty()) {
            emit searchFailed(id(), QStringLiteral("Authentication required"));
            return;
        }
        SearchResult result;
        result.id = QStringLiteral("one");
        result.title = query;
        emit searchCompleted(id(), {result});
    }
    SearchProvider *clone(QObject *parent) const override
    {
        return new AuthTestProvider(parent);
    }
};

class ProviderAuthenticationTest : public QObject
{
    Q_OBJECT
    QTemporaryDir configRoot;
    static AuthenticationDescriptor descriptor()
    {
        AuthTestProvider provider;
        return provider.authenticationDescriptor();
    }
    static QByteArray token(const QString &access = QStringLiteral("dummy-access-one"),
                            const QString &refresh = QStringLiteral("dummy-refresh-one"))
    {
        return QJsonDocument(QJsonObject{{QStringLiteral("access_token"), access},
                                         {QStringLiteral("refresh_token"), refresh},
                                         {QStringLiteral("token_type"), QStringLiteral("Bearer")},
                                         {QStringLiteral("expires_in"), 3600}})
            .toJson();
    }
    static QUrl callback(const TestAuthSession &session, const QString &state, bool denied = false)
    {
        QUrl url = session.callbackUrl();
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("state"), state);
        query.addQueryItem(denied ? QStringLiteral("error") : QStringLiteral("code"),
                           denied ? QStringLiteral("access_denied") : QStringLiteral("dummy-code"));
        if (denied)
            query.addQueryItem(QStringLiteral("error_description"), QStringLiteral("dummy-leaked-secret"));
        url.setQuery(query);
        return url;
    }
    static void expire(MemoryCredentials &store)
    {
        const QString key = store.saved.firstKey();
        auto saved = QJsonDocument::fromJson(store.saved.value(key).toUtf8()).object();
        saved.insert(QStringLiteral("expiresAt"), QDateTime::currentDateTimeUtc().addSecs(-10).toString(Qt::ISODateWithMs));
        store.saved[key] = QString::fromUtf8(QJsonDocument(saved).toJson(QJsonDocument::Compact));
    }
private slots:
    void initTestCase()
    {
        QVERIFY(configRoot.isValid());
        qputenv("XDG_CONFIG_HOME", configRoot.path().toUtf8());
    }
    void settingsModesPersistOnlyPublicConfiguration()
    {
        AuthTestProvider provider;
        ProviderAuthManager manager(nullptr);
        SettingsDialog dialog({&provider}, nullptr, &manager);
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("nativeProviders"));
        list->setCurrentRow(0);
        auto *mode = dialog.findChild<QLabel *>(QStringLiteral("providerAuthMode"));
        QVERIFY(mode && mode->text().contains(QStringLiteral("PKCE")));
        auto *client = dialog.findChild<QLineEdit *>(QStringLiteral("oauthClientId"));
        QVERIFY(client && client->isEnabled());
        client->setText(QStringLiteral("public-client-id"));
        dialog.button(QDialogButtonBox::Apply)->click();
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Authentication-oauth-test"));
        QCOMPARE(group.readEntry(QStringLiteral("ClientId"), QString()), QStringLiteral("public-client-id"));
        QCOMPARE(manager.sessionFor(&provider)->state(), ProviderAuthSession::State::Disconnected);
    }

    void pkceGrantRefreshSearchAndDisconnect()
    {
        MemoryCredentials store;
        TestAuthSession session(QStringLiteral("oauth-test"), descriptor(), &store);
        SearchTab before(QStringLiteral("before"));
        auto *providerBefore = new AuthTestProvider;
        before.startProvider(providerBefore, &session);
        QCOMPARE(providerBefore->searches, 0);
        QVERIFY(session.browserUrls.isEmpty());
        QCOMPARE(store.reads, 0); // no background wallet access
        session.responses.append(token());
        session.connectOAuth(QStringLiteral("public-client"), true);
        QCOMPARE(session.browserUrls.size(), 1);
        const QUrl authorization = session.browserUrls.first();
        const QUrlQuery authQuery(authorization);
        QCOMPARE(authQuery.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
        QVERIFY(!authQuery.hasQueryItem(QStringLiteral("client_secret")));
        QVERIFY(authQuery.queryItemValue(QStringLiteral("scope")).contains(QStringLiteral("catalogue.read")));
        QCOMPARE(session.findChild<QNetworkAccessManager *>()->redirectPolicy(), QNetworkRequest::ManualRedirectPolicy);
        QVERIFY(authQuery.queryItemValue(QStringLiteral("state")).size() >= 43);
        const auto redirect = QUrl(authQuery.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
        QCOMPARE(redirect.host(), QStringLiteral("127.0.0.1"));
        QCOMPARE(redirect.path(), QStringLiteral("/callback"));
        QVERIFY(redirect.port() > 0);
        QNetworkAccessManager browser;
        QPointer<QNetworkReply> reply = browser.get(QNetworkRequest(callback(session, authQuery.queryItemValue(QStringLiteral("state")))));
        QTRY_COMPARE(session.state(), ProviderAuthSession::State::Connected);
        QCOMPARE(session.requests.size(), 1);
        QCOMPARE(session.requests.first().operation, QNetworkAccessManager::PostOperation);
        QCOMPARE(session.requests.first().url, descriptor().oauth.tokenUrl);
        const QUrlQuery posted(QString::fromUtf8(session.requests.first().body));
        QCOMPARE(posted.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("authorization_code"));
        QVERIFY(!posted.hasQueryItem(QStringLiteral("client_secret")));
        QCOMPARE(posted.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded), redirect.toString(QUrl::FullyEncoded));
        const QByteArray verifier = posted.queryItemValue(QStringLiteral("code_verifier")).toLatin1();
        QVERIFY(verifier.size() >= 43);
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
                                         .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)),
                 authQuery.queryItemValue(QStringLiteral("code_challenge")));
        QCOMPARE(store.writes, 1);
        QVERIFY(store.interactions.last());
        before.loadMore(providerBefore->id());
        QCOMPARE(providerBefore->searches, 1);
        QCOMPARE(providerBefore->token, QStringLiteral("dummy-access-one"));
        expire(store);
        session.restore(QStringLiteral("public-client"));
        QVERIFY(!session.hasUsableToken());
        session.responses.append(token(QStringLiteral("dummy-access-two"), QStringLiteral("dummy-refresh-two")));
        SearchTab after(QStringLiteral("after"));
        auto *providerAfter = new AuthTestProvider;
        after.startProvider(providerAfter, &session);
        SearchTab concurrent(QStringLiteral("concurrent"));
        auto *concurrentProvider = new AuthTestProvider;
        concurrent.startProvider(concurrentProvider, &session);
        QTRY_COMPARE(providerAfter->searches, 1);
        QTRY_COMPARE(concurrentProvider->searches, 1);
        QCOMPARE(providerAfter->token, QStringLiteral("dummy-access-two"));
        QCOMPARE(session.browserUrls.size(), 1); // refresh never opens a browser
        QCOMPARE(session.requests.size(), 2);
        const QUrlQuery refresh(QString::fromUtf8(session.requests.last().body));
        QCOMPARE(refresh.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("refresh_token"));
        QCOMPARE(refresh.queryItemValue(QStringLiteral("refresh_token")), QStringLiteral("dummy-refresh-one"));
        QVERIFY(!store.interactions.last()); // background persistence never asks to unlock
        QCOMPARE(QJsonDocument::fromJson(store.saved.first().toUtf8()).object().value(QStringLiteral("refreshToken")).toString(),
                 QStringLiteral("dummy-refresh-two"));
        emit providerAfter->authenticationRejected();
        QVERIFY(session.accessToken().isEmpty());
        session.responses.append(token(QStringLiteral("dummy-access-three"), QStringLiteral("dummy-refresh-three")));
        bool refreshed = false;
        session.ensureReady(this, [&](bool ok, const QString &) { refreshed = ok; });
        QTRY_VERIFY(refreshed);
        QCOMPARE(session.requests.size(), 3);
        QCOMPARE(session.browserUrls.size(), 1);
        session.disconnectProvider();
        QVERIFY(store.saved.isEmpty());
        QVERIFY(session.accessToken().isEmpty());
        QVERIFY(providerBefore->token.isEmpty());
        QVERIFY(providerAfter->token.isEmpty());
        bool ready = true;
        session.ensureReady(this, [&](bool ok, const QString &) { ready = ok; });
        QVERIFY(!ready);
        if (reply)
            reply->deleteLater();
    }
    void parametersAreEncodedAndMalformedTokensAreRejected()
    {
        MemoryCredentials store;
        TestAuthSession session(QStringLiteral("encoding"), descriptor(), &store);
        const QString client = QStringLiteral("public+client&application");
        session.responses.append(token());
        session.connectOAuth(client, false);
        const QUrlQuery authorize(session.browserUrls.first());
        QCOMPARE(authorize.queryItemValue(QStringLiteral("client_id"), QUrl::FullyDecoded), client);
        QCOMPARE(authorize.allQueryItemValues(QStringLiteral("client_id")).size(), 1);
        QUrl redirect = session.callbackUrl();
        redirect.setQuery(QStringLiteral("state=") + authorize.queryItemValue(QStringLiteral("state")) +
                          QStringLiteral("&code=dummy%2Bcode%26part"));
        QNetworkAccessManager browser;
        auto *reply = browser.get(QNetworkRequest(redirect));
        QTRY_COMPARE(session.state(), ProviderAuthSession::State::Connected);
        const QUrlQuery posted(QString::fromUtf8(session.requests.last().body));
        QCOMPARE(posted.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded), QStringLiteral("dummy+code&part"));
        QCOMPARE(posted.queryItemValue(QStringLiteral("client_id"), QUrl::FullyDecoded), client);
        reply->deleteLater();
        const QList<QByteArray> malformed = {QByteArray(R"({"access_token":"dummy-access","expires_in":3600})"),
                                             QByteArray(R"({"access_token":"dummy-access","token_type":null,"expires_in":3600})"),
                                             QByteArray(R"({"access_token":"bad token","token_type":"Bearer","expires_in":3600})"),
                                             QByteArray(R"({"access_token":"dummy-access","token_type":"Bearer","expires_in":true})"),
                                             QByteArray(R"({"access_token":"dummy-access","token_type":"Bearer","expires_in":3.5})"),
                                             QByteArray(R"({"access_token":"dummy-access","token_type":"WrongType","expires_in":3600})"),
                                             QByteArray(65537, 'x')};
        for (const auto &response : malformed) {
            session.responses.append(response);
            session.connectOAuth(client, false);
            auto *responseReply = browser.get(
                QNetworkRequest(callback(session, QUrlQuery(session.browserUrls.last()).queryItemValue(QStringLiteral("state")))));
            QTRY_COMPARE(session.state(), ProviderAuthSession::State::Error);
            QVERIFY(session.accessToken().isEmpty());
            responseReply->deleteLater();
        }
    }

    void stateMismatchAndDeniedConsent()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*dummy-leaked-secret.*")));
        for (bool denied : {false, true}) {
            MemoryCredentials store;
            TestAuthSession session(QStringLiteral("provider"), descriptor(), &store);
            session.responses.append(token());
            session.connectOAuth(QStringLiteral("client"), false);
            const auto state = QUrlQuery(session.browserUrls.first()).queryItemValue(QStringLiteral("state"));
            QNetworkAccessManager browser;
            auto *reply = browser.get(QNetworkRequest(callback(session, denied ? state : QStringLiteral("incorrect-state"), denied)));
            QTRY_COMPARE(session.state(), ProviderAuthSession::State::Error);
            QVERIFY(session.requests.isEmpty());
            QVERIFY(session.accessToken().isEmpty());
            QVERIFY(store.saved.isEmpty());
            QVERIFY(!session.statusText().contains(QStringLiteral("dummy-code")));
            reply->deleteLater();
        }
    }
    void callbackRejectsWrongHostPathAndDuplicateState()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*dummy-leaked-secret.*")));
        MemoryCredentials store;
        TestAuthSession session(QStringLiteral("provider"), descriptor(), &store);
        session.connectOAuth(QStringLiteral("client"), false);
        const QUrl redirect = session.callbackUrl();
        const QByteArray host = QByteArray("127.0.0.1:") + QByteArray::number(redirect.port());
        const QList<QByteArray> requests = {
            QByteArray("GET /wrong?code=dummy-leaked-secret HTTP/1.1\r\nHost: ") + host + "\r\n\r\n",
            QByteArray("GET /callback?code=dummy-leaked-secret HTTP/1.1\r\nHost: attacker.example\r\n\r\n"),
            QByteArray("GET /callback?state=a&state=b&code=dummy-leaked-secret HTTP/1.1\r\nHost: ") + host + "\r\n\r\n",
            QByteArray("POST /callback HTTP/1.1\r\nHost: ") + host + "\r\nContent-Length: 5\r\n\r\nhello",
            QByteArray("GET /callback HTTP/1.1\r\nHost: ") + host + "\r\nX-Large: " + QByteArray(8192, 'x') + "\r\n\r\n"};
        for (const auto &request : requests) {
            QTcpSocket socket;
            socket.connectToHost(redirect.host(), quint16(redirect.port()));
            QVERIFY(socket.waitForConnected(1000));
            socket.write(request);
            QTRY_VERIFY(socket.bytesAvailable() > 0 || socket.state() == QAbstractSocket::UnconnectedState);
            QVERIFY(socket.readAll().startsWith("HTTP/1.1 400"));
            QVERIFY(session.requests.isEmpty());
            QCOMPARE(session.state(), ProviderAuthSession::State::Authorizing);
        }
        session.cancel();
    }

    void persistenceFailureAndTimeoutCancellation()
    {
        MemoryCredentials store;
        store.failWrites = true;
        TestAuthSession session(QStringLiteral("provider"), descriptor(), &store);
        session.responses.append(token());
        session.connectOAuth(QStringLiteral("client"), true);
        QNetworkAccessManager browser;
        auto *reply =
            browser.get(QNetworkRequest(callback(session, QUrlQuery(session.browserUrls.first()).queryItemValue(QStringLiteral("state")))));
        QTRY_COMPARE(session.state(), ProviderAuthSession::State::Connected);
        QVERIFY(session.hasUsableToken());
        QVERIFY(session.statusText().contains(QStringLiteral("session only")));
        QVERIFY(store.saved.isEmpty());
        reply->deleteLater();
        session.timeout = 10;
        session.connectOAuth(QStringLiteral("client"), false);
        QTRY_COMPARE(session.state(), ProviderAuthSession::State::Error);
        QVERIFY(session.statusText().contains(QStringLiteral("timed out")));
        session.timeout = 1000;
        session.connectOAuth(QStringLiteral("client"), false);
        session.cancel();
        QCOMPARE(session.state(), ProviderAuthSession::State::Disconnected);
        QVERIFY(session.callbackUrl().isEmpty());
    }
    void tokenErrorsDoNotLeakProviderDescriptions()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*dummy-leaked-secret.*")));
        MemoryCredentials store;
        TestAuthSession session(QStringLiteral("provider"), descriptor(), &store);
        session.responses.append(QByteArray(R"({"error":"invalid_grant","error_description":"dummy-leaked-secret"})"));
        session.connectOAuth(QStringLiteral("client"), false);
        QNetworkAccessManager browser;
        auto *reply =
            browser.get(QNetworkRequest(callback(session, QUrlQuery(session.browserUrls.first()).queryItemValue(QStringLiteral("state")))));
        QTRY_COMPARE(session.state(), ProviderAuthSession::State::Error);
        QVERIFY(!session.statusText().contains(QStringLiteral("dummy-leaked-secret")));
        QVERIFY(session.accessToken().isEmpty());
        reply->deleteLater();
    }

    void cancelledContextAndDestroyedSession()
    {
        MemoryCredentials store;
        auto *session = new TestAuthSession(QStringLiteral("provider"), descriptor(), &store);
        session->responses.append(token());
        session->connectOAuth(QStringLiteral("client"), true);
        QNetworkAccessManager browser;
        auto *reply = browser.get(
            QNetworkRequest(callback(*session, QUrlQuery(session->browserUrls.first()).queryItemValue(QStringLiteral("state")))));
        QTRY_COMPARE(session->state(), ProviderAuthSession::State::Connected);
        reply->deleteLater();
        expire(store);
        session->restore(QStringLiteral("client"));
        session->responses.append(token(QStringLiteral("dummy-new-access"), QStringLiteral("dummy-new-refresh")));
        auto *context = new QObject;
        bool called = false;
        session->ensureReady(context, [&](bool, const QString &) { called = true; });
        delete context;
        delete session;
        QCoreApplication::processEvents();
        QVERIFY(!called);
    }

    void providerIsolationLegacyTokensAndPendingSaveCancellation()
    {
        MemoryCredentials store;
        const AuthenticationDescriptor manual{AuthenticationMode::ManualToken, {}};
        ProviderAuthSession first(QStringLiteral("one"), manual, &store);
        ProviderAuthSession second(QStringLiteral("credentials/v1/b25l/manual"), manual, &store);
        first.setManualToken(QStringLiteral("dummy-manual-one"), true);
        second.setManualToken(QStringLiteral("dummy-manual-two"), true);
        QCOMPARE(store.saved.size(), 2);
        first.disconnectProvider();
        QCOMPARE(store.saved.size(), 1);
        QCOMPARE(second.accessToken(), QStringLiteral("dummy-manual-two"));
        store.saved[QStringLiteral("legacy_provider")] = QStringLiteral("dummy-legacy-key");
        ProviderAuthSession legacy(QStringLiteral("legacy_provider"), manual, &store);
        legacy.restore({});
        QCOMPARE(legacy.accessToken(), QStringLiteral("dummy-legacy-key"));
        legacy.disconnectProvider();
        QVERIFY(!store.saved.contains(QStringLiteral("legacy_provider")));
        store.deferWrites = true;
        first.setManualToken(QStringLiteral("dummy-pending-secret"), true);
        QCOMPARE(store.pending.size(), 1);
        first.disconnectProvider();
        QVERIFY(store.pending.isEmpty()); // queued wallet unlock cannot resurrect credentials
    }
    void synchronousConsumerDestructionIsSafe()
    {
        MemoryCredentials store;
        const AuthenticationDescriptor manual{AuthenticationMode::ManualToken, {}};
        QPointer<ProviderAuthSession> session = new ProviderAuthSession(QStringLiteral("destroyed"), manual, &store);
        connect(session, &ProviderAuthSession::credentialsChanged, this, [session](const QString &token) {
            if (!token.isEmpty())
                delete session;
        });
        session->setManualToken(QStringLiteral("dummy-never-persisted"), true);
        QVERIFY(session.isNull());
        QVERIFY(store.saved.isEmpty());
    }

    void clientBindingAndMalformedResponses()
    {
        MemoryCredentials store;
        TestAuthSession session(QStringLiteral("provider"), descriptor(), &store);
        session.responses.append(token());
        session.connectOAuth(QStringLiteral("client-one"), true);
        QNetworkAccessManager browser;
        auto *reply =
            browser.get(QNetworkRequest(callback(session, QUrlQuery(session.browserUrls.first()).queryItemValue(QStringLiteral("state")))));
        QTRY_COMPARE(session.state(), ProviderAuthSession::State::Connected);
        reply->deleteLater();
        session.restore(QStringLiteral("client-two"));
        QCOMPARE(session.state(), ProviderAuthSession::State::Error);
        QVERIFY(session.statusText().contains(QStringLiteral("do not match")));
        QVERIFY(session.accessToken().isEmpty());
        session.responses.append(QByteArray(R"({"access_token":42,"expires_in":3600})"));
        session.connectOAuth(QStringLiteral("client-one"), false);
        reply =
            browser.get(QNetworkRequest(callback(session, QUrlQuery(session.browserUrls.last()).queryItemValue(QStringLiteral("state")))));
        QTRY_COMPARE(session.state(), ProviderAuthSession::State::Error);
        QVERIFY(session.accessToken().isEmpty());
        reply->deleteLater();
        auto invalid = descriptor();
        invalid.oauth.tokenUrl = QUrl(QStringLiteral("http://identity.example/token"));
        TestAuthSession insecure(QStringLiteral("insecure"), invalid, &store);
        insecure.connectOAuth(QStringLiteral("client"), false);
        QCOMPARE(insecure.state(), ProviderAuthSession::State::Error);
        QVERIFY(insecure.browserUrls.isEmpty());
    }

    void walletRestorationRejectsMalformedSecrets()
    {
        MemoryCredentials store;
        TestAuthSession session(QStringLiteral("wallet-validation"), descriptor(), &store);
        session.responses.append(token());
        session.connectOAuth(QStringLiteral("client"), true);
        QNetworkAccessManager browser;
        auto *reply =
            browser.get(QNetworkRequest(callback(session, QUrlQuery(session.browserUrls.last()).queryItemValue(QStringLiteral("state")))));
        QTRY_COMPARE(session.state(), ProviderAuthSession::State::Connected);
        reply->deleteLater();
        const auto original = QJsonDocument::fromJson(store.saved.first().toUtf8()).object();
        for (const auto &field : {QStringLiteral("accessToken"), QStringLiteral("refreshToken"), QStringLiteral("expiresAt")}) {
            auto saved = original;
            saved.insert(field,
                         field == QStringLiteral("expiresAt") ? QJsonValue(42) : QJsonValue(QStringLiteral("dummy-invalid\r\nheader")));
            store.saved[store.saved.firstKey()] = QString::fromUtf8(QJsonDocument(saved).toJson());
            session.restore(QStringLiteral("client"));
            QCOMPARE(session.state(), ProviderAuthSession::State::Error);
            QVERIFY(session.accessToken().isEmpty());
        }
        const AuthenticationDescriptor manual{AuthenticationMode::ManualToken, {}};
        ProviderAuthSession legacy(QStringLiteral("legacy-malformed"), manual, &store);
        store.saved[QStringLiteral("legacy-malformed")] = QStringLiteral("dummy-invalid\nkey");
        legacy.restore({});
        QCOMPARE(legacy.state(), ProviderAuthSession::State::Error);
        QVERIFY(legacy.accessToken().isEmpty());
    }
    void reentrantWaitersCannotUseDisconnectedCredentials()
    {
        MemoryCredentials store;
        TestAuthSession session(QStringLiteral("waiters"), descriptor(), &store);
        session.responses.append(token());
        session.connectOAuth(QStringLiteral("client"), true);
        QNetworkAccessManager browser;
        auto *reply =
            browser.get(QNetworkRequest(callback(session, QUrlQuery(session.browserUrls.last()).queryItemValue(QStringLiteral("state")))));
        QTRY_COMPARE(session.state(), ProviderAuthSession::State::Connected);
        reply->deleteLater();
        expire(store);
        session.restore(QStringLiteral("client"));
        session.responses.append(token());
        int calls = 0;
        session.ensureReady(this, [&](bool ok, const QString &) {
            QVERIFY(ok);
            ++calls;
            session.disconnectProvider();
        });
        session.ensureReady(this, [&](bool ok, const QString &) {
            QVERIFY(!ok);
            ++calls;
        });
        QTRY_COMPARE(calls, 2);
        QVERIFY(session.accessToken().isEmpty());
        QVERIFY(store.saved.isEmpty());
    }
    void shortLivedTokenWithoutRefreshIsUsableUntilExpiry()
    {
        MemoryCredentials store;
        TestAuthSession session(QStringLiteral("short-lived"), descriptor(), &store);
        auto response = QJsonDocument::fromJson(token()).object();
        response.remove(QStringLiteral("refresh_token"));
        response.insert(QStringLiteral("expires_in"), 20);
        session.responses.append(QJsonDocument(response).toJson());
        session.connectOAuth(QStringLiteral("client"), false);
        QNetworkAccessManager browser;
        auto *reply =
            browser.get(QNetworkRequest(callback(session, QUrlQuery(session.browserUrls.last()).queryItemValue(QStringLiteral("state")))));
        QTRY_COMPARE(session.state(), ProviderAuthSession::State::Connected);
        bool ready = false;
        session.ensureReady(this, [&](bool ok, const QString &) { ready = ok; });
        QVERIFY(ready);
        QCOMPARE(session.requests.size(), 1);
        reply->deleteLater();
    }
    void configurationContainsNoCredentials()
    {
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->sync();
        QDirIterator files(configRoot.path(), QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            QFile file(files.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto bytes = file.readAll();
            for (const auto &secret :
                 {QByteArray("dummy-access"), QByteArray("dummy-refresh"), QByteArray("dummy-manual"), QByteArray("dummy-code")})
                QVERIFY(!bytes.contains(secret));
        }
    }
};
QTEST_MAIN(ProviderAuthenticationTest)
#include "test_providerauthentication.moc"
