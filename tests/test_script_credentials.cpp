// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jsprovider.h"
#include "providerpackage.h"
#include "providerhostservice.h"
#include "credentialstore.h"
#include "providerapiv1.h"

#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QTimer>

namespace {

class MockCredentialStore : public CredentialStore {
    Q_OBJECT
public:
    using CredentialStore::CredentialStore;
    QHash<QString, QString> values;
    QStringList cancelledKeys;

    void read(const QString &key, bool, Callback callback) override {
        if (values.contains(key)) {
            callback(CredentialStoreResult{true, values.value(key), {}, CredentialStoreResult::Code::None});
        } else {
            callback(CredentialStoreResult{false, {}, QStringLiteral("Not found"), CredentialStoreResult::Code::NotFound});
        }
    }
    void write(const QString &key, const QString &value, bool, Callback callback) override {
        values[key] = value;
        callback(CredentialStoreResult{true, {}, {}, CredentialStoreResult::Code::None});
    }
    void remove(const QString &key, bool, Callback callback) override {
        values.remove(key);
        callback(CredentialStoreResult{true, {}, {}, CredentialStoreResult::Code::None});
    }
    void cancelPending(const QString &key) override {
        cancelledKeys.append(key);
    }
};

class DeferredCredentialStore : public CredentialStore {
    Q_OBJECT
public:
    using CredentialStore::CredentialStore;
    Callback pendingCallback;
    QString pendingKey;
    QStringList cancelledKeys;

    void read(const QString &key, bool, Callback callback) override {
        pendingKey = key;
        pendingCallback = callback;
    }
    void write(const QString &, const QString &, bool, Callback callback) override {
        callback(CredentialStoreResult{true, {}, {}, CredentialStoreResult::Code::None});
    }
    void remove(const QString &, bool, Callback callback) override {
        callback(CredentialStoreResult{true, {}, {}, CredentialStoreResult::Code::None});
    }
    void cancelPending(const QString &key) override {
        cancelledKeys.append(key);
    }
    void resolve(const QString &value) {
        if (pendingCallback) {
            auto cb = pendingCallback;
            pendingCallback = nullptr;
            cb(CredentialStoreResult{true, value, {}, CredentialStoreResult::Code::None});
        }
    }
};

class ScriptCredentialReply : public QNetworkReply {
    Q_OBJECT
public:
    explicit ScriptCredentialReply(const QNetworkRequest &request, QObject *parent = nullptr)
        : QNetworkReply(parent), m_request(request)
    {
        setRequest(request);
        setUrl(request.url());
        setOpenMode(QIODevice::ReadOnly);
    }

    void respondSuccess(int statusCode, const QByteArray &data, const QMap<QByteArray, QByteArray> &headers = {})
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
            setRawHeader(it.key(), it.value());
        }
        m_data = data;
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit metaDataChanged();
            emit readyRead();
            emit finished();
        });
    }

    void respondRedirect(const QUrl &target, int statusCode = 302)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        setAttribute(QNetworkRequest::RedirectionTargetAttribute, target);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit metaDataChanged();
            emit finished();
        });
    }

    void abort() override {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"));
        setFinished(true);
        emit finished();
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override {
        const qint64 len = qMin(maxlen, static_cast<qint64>(m_data.size() - m_offset));
        if (len <= 0) return 0;
        memcpy(data, m_data.constData() + m_offset, len);
        m_offset += len;
        return len;
    }

private:
    QNetworkRequest m_request;
    QByteArray m_data;
    qint64 m_offset = 0;
};

class ScriptCredentialNam : public QNetworkAccessManager {
    Q_OBJECT
public:
    using QNetworkAccessManager::QNetworkAccessManager;
    std::function<ScriptCredentialReply *(const QNetworkRequest &, Operation, const QByteArray &)> handler;
    struct Captured {
        QNetworkRequest request;
        Operation op;
        QByteArray body;
    };
    QList<Captured> captured;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override {
        QByteArray body;
        if (outgoingData) body = outgoingData->readAll();
        captured.append({request, op, body});
        if (handler) return handler(request, op, body);
        auto *reply = new ScriptCredentialReply(request, this);
        reply->respondSuccess(200, "{\"results\":[]}");
        return reply;
    }
};

ProviderPackage createPackageFixture(const QString &dirPath,
                                     const QString &packageId,
                                     const QStringList &origins,
                                     const QStringList &secrets,
                                     const QString &scriptCode)
{
    QJsonObject manifestObj;
    manifestObj[QStringLiteral("id")] = packageId;
    manifestObj[QStringLiteral("name")] = QStringLiteral("Test Secure Script");
    manifestObj[QStringLiteral("version")] = QStringLiteral("1.0.0");
    manifestObj[QStringLiteral("runtime")] = QStringLiteral("javascript");
    manifestObj[QStringLiteral("entry")] = QStringLiteral("main.mjs");

    QJsonObject permsObj;
    permsObj[QStringLiteral("origins")] = QJsonArray::fromStringList(origins);
    permsObj[QStringLiteral("secrets")] = QJsonArray::fromStringList(secrets);
    manifestObj[QStringLiteral("permissions")] = permsObj;

    QFile manifestFile(dirPath + QStringLiteral("/provider.json"));
    if (manifestFile.open(QIODevice::WriteOnly)) {
        manifestFile.write(QJsonDocument(manifestObj).toJson());
        manifestFile.close();
    }

    QFile scriptFile(dirPath + QStringLiteral("/main.mjs"));
    if (scriptFile.open(QIODevice::WriteOnly)) {
        scriptFile.write(scriptCode.toUtf8());
        scriptFile.close();
    }

    auto pkg = ProviderPackage::fromDirectory(dirPath);
    pkg.setTrusted(true);
    return pkg;
}

} // namespace

class TestScriptCredentials : public QObject {
    Q_OBJECT
private slots:
    void testKWalletCredentialStoreInjection()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString script = QStringLiteral(
            "function search(query) {\n"
            "    return host.fetch('https://api.secure.example.com/search?q=' + encodeURIComponent(query), {\n"
            "        secretKey: 'api_token',\n"
            "        secretHeader: 'Authorization'\n"
            "    }).then(function(resp) {\n"
            "        return [{ id: '1', title: 'Result 1' }];\n"
            "    });\n"
            "}\n"
            "export { search };\n"
        );

        auto pkg = createPackageFixture(tempDir.path(), QStringLiteral("secure_pkg"),
                                        {QStringLiteral("https://api.secure.example.com")},
                                        {QStringLiteral("api_token")},
                                        script);
        QVERIFY(pkg.isValid());

        auto *store = new MockCredentialStore(this);
        store->values[QStringLiteral("provider_pkg:secure_pkg:api_token")] = QStringLiteral("Bearer KWALLET_TOKEN_123");

        auto *mockNam = new ScriptCredentialNam(this);
        mockNam->handler = [](const QNetworkRequest &req, QNetworkAccessManager::Operation, const QByteArray &) -> ScriptCredentialReply * {
            auto *reply = new ScriptCredentialReply(req);
            reply->respondSuccess(200, "{\"results\":[{\"title\":\"Result 1\"}]}");
            return reply;
        };

        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNam);
        hostService->setCredentialStore(store);

        JsProvider provider(pkg, this, hostService);
        hostService->setParent(&provider);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));

        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        const quint64 reqId = provider.search(QStringLiteral("test"));
        QVERIFY(reqId != 0);
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.count(), 1);

        QCOMPARE(mockNam->captured.size(), 1);
        const auto &captured = mockNam->captured.first();
        QCOMPARE(captured.request.rawHeader("Authorization"), QByteArray("Bearer KWALLET_TOKEN_123"));
    }

    void testClonedJsProviderRetainsCredentialStore()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString script = QStringLiteral(
            "function search(query) {\n"
            "    return host.fetch('https://api.secure.example.com/search?q=' + encodeURIComponent(query), {\n"
            "        secretKey: 'api_token',\n"
            "        secretHeader: 'X-API-Key'\n"
            "    }).then(function(resp) {\n"
            "        return [{ id: '1', title: 'OK' }];\n"
            "    });\n"
            "}\n"
            "export { search };\n"
        );

        auto pkg = createPackageFixture(tempDir.path(), QStringLiteral("clone_pkg"),
                                        {QStringLiteral("https://api.secure.example.com")},
                                        {QStringLiteral("api_token")},
                                        script);
        QVERIFY(pkg.isValid());

        auto *store = new MockCredentialStore(this);
        store->values[QStringLiteral("provider_pkg:clone_pkg:api_token")] = QStringLiteral("STORED_SECRET_KEY");

        auto *mockNam = new ScriptCredentialNam(this);
        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNam);
        hostService->setCredentialStore(store);

        JsProvider master(pkg, this, hostService);
        hostService->setParent(&master);
        QVERIFY(master.isLoaded());

        // Clone for per-tab isolation
        auto *cloned = master.clone(this);
        QVERIFY(cloned != nullptr);
        auto *clonedJs = qobject_cast<JsProvider *>(cloned);
        QVERIFY(clonedJs != nullptr);
        QVERIFY(clonedJs->isLoaded());

        // Cloned host carries credential store
        QVERIFY(clonedJs->hostService() != nullptr);
        QCOMPARE(clonedJs->hostService()->credentialStore(), store);
        // And maintains independent NAM
        QVERIFY(clonedJs->hostService()->networkAccessManager() != master.hostService()->networkAccessManager());

        delete cloned;
    }

    void testSessionOnlyCredentialsLifecycle()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString script = QStringLiteral(
            "function search(query) {\n"
            "    return host.fetch('https://api.secure.example.com/search', {\n"
            "        secretKey: 'session_key',\n"
            "        secretHeader: 'Authorization'\n"
            "    }).then(function(resp) {\n"
            "        return [];\n"
            "    });\n"
            "}\n"
            "export { search };\n"
        );

        auto pkg = createPackageFixture(tempDir.path(), QStringLiteral("session_pkg"),
                                        {QStringLiteral("https://api.secure.example.com")},
                                        {QStringLiteral("session_key")},
                                        script);

        auto *mockNam = new ScriptCredentialNam(this);
        mockNam->handler = [](const QNetworkRequest &req, QNetworkAccessManager::Operation, const QByteArray &) -> ScriptCredentialReply * {
            auto *reply = new ScriptCredentialReply(req);
            reply->respondSuccess(200, "{\"results\":[]}");
            return reply;
        };

        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNam);
        JsProvider provider(pkg, this, hostService);
        hostService->setParent(&provider);

        // Set session-only credential (in memory)
        provider.setSessionCredential(QStringLiteral("TEMP_SESSION_TOKEN"));
        QCOMPARE(provider.sessionCredential(), QStringLiteral("TEMP_SESSION_TOKEN"));

        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        provider.search(QStringLiteral("query1"));
        QVERIFY(spy.wait(1000));
        QCOMPARE(mockNam->captured.size(), 1);
        QCOMPARE(mockNam->captured.last().request.rawHeader("Authorization"), QByteArray("TEMP_SESSION_TOKEN"));

        // Revoke session credential
        provider.setSessionCredential(QString());
        QVERIFY(provider.sessionCredential().isEmpty());

        provider.search(QStringLiteral("query2"));
        QVERIFY(spy.wait(1000));
        QCOMPARE(mockNam->captured.size(), 2);
        QVERIFY(!mockNam->captured.last().request.hasRawHeader("Authorization"));
    }

    void testQueryParamAndBodyPlaceholderSubstitution()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString script = QStringLiteral(
            "function search(query) {\n"
            "    return host.fetch('https://api.secure.example.com/api', {\n"
            "        method: 'POST',\n"
            "        secretKey: 'api_token',\n"
            "        secretQueryParam: 'access_token',\n"
            "        secretBodyPlaceholder: '__TOKEN__',\n"
            "        body: '{\"secret\":\"__TOKEN__\"}'\n"
            "    }).then(function(resp) {\n"
            "        return [];\n"
            "    });\n"
            "}\n"
            "export { search };\n"
        );

        auto pkg = createPackageFixture(tempDir.path(), QStringLiteral("subst_pkg"),
                                        {QStringLiteral("https://api.secure.example.com")},
                                        {QStringLiteral("api_token")},
                                        script);

        auto *store = new MockCredentialStore(this);
        store->values[QStringLiteral("provider_pkg:subst_pkg:api_token")] = QStringLiteral("MY_SECRET_KEY");

        auto *mockNam = new ScriptCredentialNam(this);
        auto *hostService = new ProviderHost::ProviderHostService(pkg, this, mockNam);
        hostService->setCredentialStore(store);

        JsProvider provider(pkg, this, hostService);
        hostService->setParent(&provider);

        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        provider.search(QStringLiteral("query"));
        QVERIFY(spy.wait(1000));

        QCOMPARE(mockNam->captured.size(), 1);
        const auto &captured = mockNam->captured.first();

        // Query param injected on wire
        QUrlQuery query(captured.request.url());
        QCOMPARE(query.queryItemValue(QStringLiteral("access_token")), QStringLiteral("MY_SECRET_KEY"));

        // Body placeholder substituted in memory on wire
        QCOMPARE(captured.body, QByteArray("{\"secret\":\"MY_SECRET_KEY\"}"));
    }

    void testCrossOriginRedirectRedaction()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Target redirect origin is allowed by manifest, but cross-origin (different port)
        auto pkg = createPackageFixture(tempDir.path(), QStringLiteral("redact_pkg"),
                                        {QStringLiteral("https://api.secure.example.com"),
                                         QStringLiteral("https://api.secure.example.com:8443")},
                                        {QStringLiteral("api_token")},
                                        QStringLiteral("export function search() {}"));

        auto *store = new MockCredentialStore(this);
        store->values[QStringLiteral("provider_pkg:redact_pkg:api_token")] = QStringLiteral("SECRET_TO_REDACT");

        auto *mockNam = new ScriptCredentialNam(this);
        int requestCount = 0;
        mockNam->handler = [&requestCount](const QNetworkRequest &req, QNetworkAccessManager::Operation, const QByteArray &) -> ScriptCredentialReply * {
            auto *reply = new ScriptCredentialReply(req);
            if (++requestCount == 1) {
                // Redirect to cross-origin (port 8443)
                reply->respondRedirect(QUrl(QStringLiteral("https://api.secure.example.com:8443/redirected")));
            } else {
                reply->respondSuccess(200, "{\"results\":[]}");
            }
            return reply;
        };

        ProviderHost::ProviderHostService host(pkg, this, mockNam);
        host.setCredentialStore(store);

        QSignalSpy spy(&host, &ProviderHost::ProviderHostService::httpResponseReady);
        ProviderHost::HttpRequestOptions opts;
        opts.url = QUrl(QStringLiteral("https://api.secure.example.com/init"));
        opts.secretKey = QStringLiteral("api_token");
        opts.secretHeader = QStringLiteral("Authorization");
        opts.secretQueryParam = QStringLiteral("auth_token");
        opts.secretBodyPlaceholder = QStringLiteral("__KEY__");
        opts.requestId = 42;
        host.executeHttpRequest(opts);

        QVERIFY(spy.wait(1000));
        QCOMPARE(mockNam->captured.size(), 2);

        // First request to port 443 had secrets injected
        const auto &first = mockNam->captured[0];
        QCOMPARE(first.request.rawHeader("Authorization"), QByteArray("SECRET_TO_REDACT"));
        QCOMPARE(QUrlQuery(first.request.url()).queryItemValue(QStringLiteral("auth_token")), QStringLiteral("SECRET_TO_REDACT"));

        // Second request across origin (port 8443) had secrets REDACTED
        const auto &second = mockNam->captured[1];
        QVERIFY(!second.request.hasRawHeader("Authorization"));
        QVERIFY(QUrlQuery(second.request.url()).queryItemValue(QStringLiteral("auth_token")).isEmpty());
    }

    void testSafeAsyncCancellation()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        auto pkg = createPackageFixture(tempDir.path(), QStringLiteral("async_cancel_pkg"),
                                        {QStringLiteral("https://api.secure.example.com")},
                                        {QStringLiteral("api_token")},
                                        QStringLiteral("export function search() {}"));

        auto *store = new DeferredCredentialStore(this);
        auto *mockNam = new ScriptCredentialNam(this);

        ProviderHost::ProviderHostService host(pkg, this, mockNam);
        host.setCredentialStore(store);

        ProviderHost::HttpRequestOptions opts;
        opts.url = QUrl(QStringLiteral("https://api.secure.example.com/search"));
        opts.secretKey = QStringLiteral("api_token");
        opts.secretHeader = QStringLiteral("Authorization");
        opts.requestId = 101;

        // Initiate request - wallet read is now pending
        host.executeHttpRequest(opts);
        QCOMPARE(store->pendingKey, QStringLiteral("provider_pkg:async_cancel_pkg:api_token"));

        // Cancel the request before wallet read resolves (#118 async cancellation safety)
        host.cancelRequest(101);
        QVERIFY(store->cancelledKeys.contains(QStringLiteral("provider_pkg:async_cancel_pkg:api_token")));

        // Now resolve wallet read after cancellation
        store->resolve(QStringLiteral("LATE_SECRET"));
        QTest::qWait(50);

        // Verifies no network request was ever dispatched
        QCOMPARE(mockNam->captured.size(), 0);
    }

    void testPackagePermissionChangeInvalidatesCredentials()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        auto pkg = createPackageFixture(tempDir.path(), QStringLiteral("perm_pkg"),
                                        {QStringLiteral("https://api.secure.example.com")},
                                        {QStringLiteral("api_token")},
                                        QStringLiteral("export function search() {}"));

        auto *mockNam = new ScriptCredentialNam(this);
        ProviderHost::ProviderHostService host(pkg, this, mockNam);
        host.setSessionCredential(QStringLiteral("SESSION_TOKEN"));
        QCOMPARE(host.sessionCredential(), QStringLiteral("SESSION_TOKEN"));

        // Tamper package: untrust package
        auto tamperedPkg = pkg;
        tamperedPkg.setTrusted(false);

        host.setPackage(tamperedPkg);
        // Session credentials immediately cleared
        QVERIFY(host.sessionCredential().isEmpty());
    }

    void testUndeclaredSecretBlocked()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Package only declares "declared_token", NOT "undeclared_secret"
        auto pkg = createPackageFixture(tempDir.path(), QStringLiteral("decl_pkg"),
                                        {QStringLiteral("https://api.secure.example.com")},
                                        {QStringLiteral("declared_token")},
                                        QStringLiteral("export function search() {}"));

        auto *mockNam = new ScriptCredentialNam(this);
        ProviderHost::ProviderHostService host(pkg, this, mockNam);

        QSignalSpy spy(&host, &ProviderHost::ProviderHostService::httpResponseReady);
        ProviderHost::HttpRequestOptions opts;
        opts.url = QUrl(QStringLiteral("https://api.secure.example.com/search"));
        opts.secretKey = QStringLiteral("undeclared_secret");
        opts.secretHeader = QStringLiteral("Authorization");
        opts.requestId = 77;
        host.executeHttpRequest(opts);

        QVERIFY(spy.wait(500));
        auto resp = spy.first().at(1).value<ProviderHost::HttpResponse>();
        QVERIFY(!resp.success);
        QVERIFY(resp.errorMessage.contains(QStringLiteral("not declared")));
        QCOMPARE(mockNam->captured.size(), 0);
    }

    void testSecretsNeverExposedToScriptOrDiagnostics()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString script = QStringLiteral(
            "function search(query) {\n"
            "    var exposed = host.getSecret('api_token');\n"
            "    return Promise.resolve([{ id: '1', title: exposed }]);\n"
            "}\n"
            "export { search };\n"
        );

        auto pkg = createPackageFixture(tempDir.path(), QStringLiteral("diag_pkg"),
                                        {QStringLiteral("https://api.secure.example.com")},
                                        {QStringLiteral("api_token")},
                                        script);

        auto *hostService = new ProviderHost::ProviderHostService(pkg, this);
        hostService->setSessionCredential(QStringLiteral("SECRET_123"));

        JsProvider provider(pkg, this, hostService);
        hostService->setParent(&provider);

        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        provider.search(QStringLiteral("test"));
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.count(), 1);

        auto page = spy.first().at(1).value<SearchPage>();
        QCOMPARE(page.results.size(), 1);
        // host.getSecret returns empty string; raw secret never exposed to JS engine
        QVERIFY(page.results.first().title.isEmpty());

        // In diagnostic mode, requests requesting secrets are completely denied
        hostService->setDiagnosticPolicy(false);
        QSignalSpy hostSpy(hostService, &ProviderHost::ProviderHostService::httpResponseReady);
        ProviderHost::HttpRequestOptions opts;
        opts.url = QUrl(QStringLiteral("https://api.secure.example.com/search"));
        opts.secretKey = QStringLiteral("api_token");
        opts.requestId = 99;
        hostService->executeHttpRequest(opts);
        QVERIFY(hostSpy.wait(500));
        auto resp = hostSpy.first().at(1).value<ProviderHost::HttpResponse>();
        QVERIFY(!resp.success);
        QVERIFY(resp.errorMessage.contains(QStringLiteral("diagnostic")));
    }
};

QTEST_MAIN(TestScriptCredentials)
#include "test_script_credentials.moc"
