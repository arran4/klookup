// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "credentialstore.h"
#include "providerhostservice.h"
#include "providerpackage.h"

#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace ProviderHost;

class MockCredentialStore : public CredentialStore
{
    Q_OBJECT
public:
    void cancelPending(const QString &) override {}
    void read(const QString &key, bool, Callback callback) override
    {
        if (values.contains(key)) {
            callback(CredentialStoreResult{true, values.value(key), {}, CredentialStoreResult::Code::None});
        } else {
            callback(CredentialStoreResult{false, {}, QStringLiteral("Not found"), CredentialStoreResult::Code::NotFound});
        }
    }
    void write(const QString &key, const QString &val, bool, Callback callback) override
    {
        values.insert(key, val);
        callback(CredentialStoreResult{true, {}, {}, CredentialStoreResult::Code::None});
    }
    void remove(const QString &key, bool, Callback callback) override
    {
        values.remove(key);
        callback(CredentialStoreResult{true, {}, {}, CredentialStoreResult::Code::None});
    }
    QMap<QString, QString> values;
};

class FakeNetworkReply : public QNetworkReply
{
    Q_OBJECT
public:
    FakeNetworkReply(const QNetworkRequest &request, QObject *parent = nullptr)
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

    void respondRedirect(const QUrl &targetUrl)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 302);
        setAttribute(QNetworkRequest::RedirectionTargetAttribute, targetUrl);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit metaDataChanged();
            emit finished();
        });
    }

    void setKnownHeader(QNetworkRequest::KnownHeaders header, const QVariant &value)
    {
        setHeader(header, value);
    }

    void abort() override
    {
        m_aborted = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation aborted"));
        setFinished(true);
        emit finished();
    }

    bool isAborted() const { return m_aborted; }
    const QNetworkRequest &sentRequest() const { return m_request; }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const qint64 len = qMin(maxlen, static_cast<qint64>(m_data.size() - m_offset));
        if (len <= 0) return 0;
        memcpy(data, m_data.constData() + m_offset, len);
        m_offset += len;
        return len;
    }

    qint64 bytesAvailable() const override
    {
        return (m_data.size() - m_offset) + QIODevice::bytesAvailable();
    }

private:
    QNetworkRequest m_request;
    QByteArray m_data;
    qint64 m_offset = 0;
    bool m_aborted = false;
};

class FakeNetworkAccessManager : public QNetworkAccessManager
{
    Q_OBJECT
public:
    explicit FakeNetworkAccessManager(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    std::function<FakeNetworkReply *(const QNetworkRequest &req, QNetworkAccessManager::Operation op)> handler;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoingData) override
    {
        Q_UNUSED(outgoingData);
        if (handler) {
            return handler(req, op);
        }
        auto *rep = new FakeNetworkReply(req, this);
        rep->respondSuccess(200, "OK");
        return rep;
    }
};

class TestProviderDiscovery : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testDiscoveryPrecedence()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d");
        const QString userPath = fixtureDir + QStringLiteral("/user");
        const QString systemPath = fixtureDir + QStringLiteral("/system");

        // Scan custom search paths: userPath before systemPath
        const auto packages = ProviderPackageDiscovery::discoverPackages({userPath, systemPath});
        QCOMPARE(packages.size(), 2);

        // Package 1: alpha should be the user version (overriding system)
        QCOMPARE(packages[0].id(), QStringLiteral("alpha"));
        QCOMPARE(packages[0].name(), QStringLiteral("Alpha User Provider"));
        QCOMPARE(packages[0].manifest().version, QStringLiteral("1.0.0"));
        QVERIFY(!packages[0].isTrusted());
        QVERIFY(!packages[0].isEnabled());
        QVERIFY(!packages[0].contentHash().isEmpty());

        // Package 2: beta should be discovered from system
        QCOMPARE(packages[1].id(), QStringLiteral("beta"));
        QCOMPARE(packages[1].name(), QStringLiteral("Beta System Provider"));
        QCOMPARE(packages[1].manifest().version, QStringLiteral("1.2.0"));
        QVERIFY(!packages[1].isTrusted());
        QVERIFY(!packages[1].isEnabled());
    }

    void testPathTraversalAndValidation()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d");

        // 1. Path traversal escape attempt
        const auto escapePkg = ProviderPackage::fromDirectory(fixtureDir + QStringLiteral("/invalid_escape"));
        QVERIFY(!escapePkg.isValid());
        QVERIFY(escapePkg.validationError().contains(QStringLiteral("symlink traversal blocked")) ||
                escapePkg.validationError().contains(QStringLiteral("does not exist")));

        // 2. Malformed JSON manifest
        const auto brokenPkg = ProviderPackage::fromDirectory(fixtureDir + QStringLiteral("/invalid_manifest"));
        QVERIFY(!brokenPkg.isValid());
        QVERIFY(brokenPkg.validationError().contains(QStringLiteral("JSON parse error")));

        // 3. Non-existent directory
        const auto missingPkg = ProviderPackage::fromDirectory(fixtureDir + QStringLiteral("/non_existent_dir"));
        QVERIFY(!missingPkg.isValid());
        QVERIFY(missingPkg.validationError().contains(QStringLiteral("does not exist")));
    }

    void testOriginMatching()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d");
        const auto pkg = ProviderPackage::fromDirectory(fixtureDir + QStringLiteral("/system/beta"));
        QVERIFY(pkg.isValid());

        // Allowed: api.beta.example.com (https:443)
        QVERIFY(pkg.isOriginAllowed(QUrl(QStringLiteral("https://api.beta.example.com/v1/search"))));

        // Wrong scheme / port -> rejected
        QVERIFY(!pkg.isOriginAllowed(QUrl(QStringLiteral("http://api.beta.example.com:8080/"))));
        QVERIFY(!pkg.isOriginAllowed(QUrl(QStringLiteral("https://api.beta.example.com:8443/"))));

        // Allowed wildcard: *.beta.net (https:443)
        QVERIFY(pkg.isOriginAllowed(QUrl(QStringLiteral("https://cdn.beta.net/image.png"))));
        QVERIFY(pkg.isOriginAllowed(QUrl(QStringLiteral("https://beta.net/"))));

        // Scheme downgrade blocked
        QVERIFY(!pkg.isOriginAllowed(QUrl(QStringLiteral("http://beta.net/"))));

        // Disallowed origins
        QVERIFY(!pkg.isOriginAllowed(QUrl(QStringLiteral("https://malicious.org/api"))));
        QVERIFY(!pkg.isOriginAllowed(QUrl(QStringLiteral("https://example.com/"))));
        QVERIFY(!pkg.isOriginAllowed(QUrl(QStringLiteral("file:///etc/passwd"))));
    }

    void testHostServiceUntrustedRejection()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir + QStringLiteral("/user/alpha"));
        QVERIFY(pkg.isValid());
        QVERIFY(!pkg.isTrusted()); // untrusted by default

        ProviderHostService host(pkg, this);
        QSignalSpy spy(&host, &ProviderHostService::httpResponseReady);

        HttpRequestOptions opts;
        opts.url = QUrl(QStringLiteral("https://api.alpha.example.com/search"));
        opts.requestId = 10;
        host.executeHttpRequest(opts);

        QVERIFY(spy.wait(500));
        QCOMPARE(spy.count(), 1);
        const auto resp = spy.first().at(1).value<HttpResponse>();
        QCOMPARE(resp.requestId, 10ULL);
        QVERIFY(!resp.success);
        QVERIFY(resp.errorMessage.contains(QStringLiteral("Untrusted provider package")));
    }

    void testHostServiceOriginEnforcement()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir + QStringLiteral("/user/alpha"));
        pkg.setTrusted(true); // explicitly trusted

        ProviderHostService host(pkg, this);
        QSignalSpy spy(&host, &ProviderHostService::httpResponseReady);

        HttpRequestOptions opts;
        opts.url = QUrl(QStringLiteral("https://unauthorized.domain.org/data"));
        opts.requestId = 20;
        host.executeHttpRequest(opts);

        QVERIFY(spy.wait(500));
        QCOMPARE(spy.count(), 1);
        const auto resp = spy.first().at(1).value<HttpResponse>();
        QCOMPARE(resp.requestId, 20ULL);
        QVERIFY(!resp.success);
        QVERIFY(resp.errorMessage.contains(QStringLiteral("Target origin 'unauthorized.domain.org' is not declared")));
    }

    void testHostServiceSecretInterpolation()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir + QStringLiteral("/user/alpha"));
        pkg.setTrusted(true);

        ProviderHostService host(pkg, this);

        // 1. Undeclared secret key rejection
        QSignalSpy spy(&host, &ProviderHostService::httpResponseReady);
        HttpRequestOptions opts;
        opts.url = QUrl(QStringLiteral("https://api.alpha.example.com/search"));
        opts.secretKey = QStringLiteral("undeclared_secret");
        opts.requestId = 30;
        host.executeHttpRequest(opts);

        QVERIFY(spy.wait(500));
        const auto resp = spy.first().at(1).value<HttpResponse>();
        QVERIFY(!resp.success);
        QVERIFY(resp.errorMessage.contains(QStringLiteral("Secret 'undeclared_secret' is not declared")));

        // 2. Declared secret key resolution
        bool resolverCalled = false;
        host.setSecretResolver([&](const QString &key) -> QString {
            if (key == QLatin1String("alpha_key")) {
                resolverCalled = true;
                return QStringLiteral("secret-token-123");
            }
            return QString();
        });

        spy.clear();
        opts.secretKey = QStringLiteral("alpha_key");
        opts.secretHeader = QStringLiteral("Authorization");
        opts.requestId = 31;
        host.executeHttpRequest(opts);

        // Resolver was called securely
        QVERIFY(resolverCalled);
    }

    void testBoundedRedirects()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir + QStringLiteral("/user/alpha"));
        pkg.setTrusted(true);

        auto *mockNam = new FakeNetworkAccessManager(this);
        int requestCount = 0;
        mockNam->handler = [&](const QNetworkRequest &req, QNetworkAccessManager::Operation) -> FakeNetworkReply * {
            ++requestCount;
            auto *reply = new FakeNetworkReply(req, mockNam);
            if (req.url().path() == QStringLiteral("/redirect1")) {
                reply->respondRedirect(QUrl(QStringLiteral("https://api.alpha.example.com/target")));
            } else if (req.url().path() == QStringLiteral("/target")) {
                reply->respondSuccess(200, "{\"status\":\"ok\"}");
            } else if (req.url().path() == QStringLiteral("/downgrade")) {
                reply->respondRedirect(QUrl(QStringLiteral("http://api.alpha.example.com/insecure")));
            } else if (req.url().path() == QStringLiteral("/undeclared")) {
                reply->respondRedirect(QUrl(QStringLiteral("https://evil.attacker.org/pwn")));
            } else if (req.url().path().startsWith(QStringLiteral("/loop"))) {
                reply->respondRedirect(QUrl(QStringLiteral("https://api.alpha.example.com/loop%1").arg(requestCount)));
            }
            return reply;
        };

        ProviderHostService host(pkg, this, mockNam);
        QSignalSpy spy(&host, &ProviderHostService::httpResponseReady);

        // 1. Safe redirect followed within same origin
        requestCount = 0;
        HttpRequestOptions opts;
        opts.url = QUrl(QStringLiteral("https://api.alpha.example.com/redirect1"));
        opts.requestId = 100;
        host.executeHttpRequest(opts);
        QVERIFY(spy.wait(500));
        auto resp = spy.takeFirst().at(1).value<HttpResponse>();
        QVERIFY(resp.success);
        QCOMPARE(resp.statusCode, 200);
        QCOMPARE(resp.body, QByteArray("{\"status\":\"ok\"}"));
        QCOMPARE(requestCount, 2);

        // 2. HTTPS to HTTP downgrade blocked
        requestCount = 0;
        opts.url = QUrl(QStringLiteral("https://api.alpha.example.com/downgrade"));
        opts.requestId = 101;
        host.executeHttpRequest(opts);
        QVERIFY(spy.wait(500));
        resp = spy.takeFirst().at(1).value<HttpResponse>();
        QVERIFY(!resp.success);
        QVERIFY(resp.errorMessage.contains(QStringLiteral("Insecure redirect from HTTPS to HTTP blocked")));

        // 3. Redirect to undeclared origin blocked
        requestCount = 0;
        opts.url = QUrl(QStringLiteral("https://api.alpha.example.com/undeclared"));
        opts.requestId = 102;
        host.executeHttpRequest(opts);
        QVERIFY(spy.wait(500));
        resp = spy.takeFirst().at(1).value<HttpResponse>();
        QVERIFY(!resp.success);
        QVERIFY(resp.errorMessage.contains(QStringLiteral("blocked by package policy")));

        // 4. Exceeding 5 hops blocked
        requestCount = 0;
        opts.url = QUrl(QStringLiteral("https://api.alpha.example.com/loop0"));
        opts.requestId = 103;
        host.executeHttpRequest(opts);
        QVERIFY(spy.wait(1000));
        resp = spy.takeFirst().at(1).value<HttpResponse>();
        QVERIFY(!resp.success);
        QVERIFY(resp.errorMessage.contains(QStringLiteral("exceeded maximum of 5 hops")));
    }

    void testStreamingChunkResponseBounds()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir + QStringLiteral("/user/alpha"));
        pkg.setTrusted(true);

        auto *mockNam = new FakeNetworkAccessManager(this);
        mockNam->handler = [&](const QNetworkRequest &req, QNetworkAccessManager::Operation) -> FakeNetworkReply * {
            auto *reply = new FakeNetworkReply(req, mockNam);
            if (req.url().path() == QStringLiteral("/huge-header")) {
                reply->setKnownHeader(QNetworkRequest::ContentLengthHeader, 6 * 1024 * 1024);
                reply->respondSuccess(200, "dummy");
            } else if (req.url().path() == QStringLiteral("/huge-chunk")) {
                // Large chunk in body
                QByteArray hugeData(6 * 1024 * 1024, 'A');
                reply->respondSuccess(200, hugeData);
            }
            return reply;
        };

        ProviderHostService host(pkg, this, mockNam);
        QSignalSpy spy(&host, &ProviderHostService::httpResponseReady);

        // 1. Fast-path header rejection
        HttpRequestOptions opts;
        opts.url = QUrl(QStringLiteral("https://api.alpha.example.com/huge-header"));
        opts.requestId = 200;
        host.executeHttpRequest(opts);
        QVERIFY(spy.wait(500));
        auto resp = spy.takeFirst().at(1).value<HttpResponse>();
        QVERIFY(!resp.success);
        QVERIFY(resp.truncated);
        QVERIFY(resp.errorMessage.contains(QStringLiteral("exceeded maximum streaming limit")));

        // 2. Huge chunk abort
        opts.url = QUrl(QStringLiteral("https://api.alpha.example.com/huge-chunk"));
        opts.requestId = 201;
        host.executeHttpRequest(opts);
        QVERIFY(spy.wait(500));
        resp = spy.takeFirst().at(1).value<HttpResponse>();
        QVERIFY(!resp.success);
        QVERIFY(resp.truncated);
        QVERIFY(resp.body.isEmpty()); // Buffer was cleared immediately upon limit exceeded
    }

    void testKWalletCredentialStoreIsolation()
    {
        const QString fixtureDir = QStringLiteral(FIXTURES_DIR "/providers.d");
        auto pkg = ProviderPackage::fromDirectory(fixtureDir + QStringLiteral("/user/alpha"));
        pkg.setTrusted(true);

        auto *store = new MockCredentialStore;
        // Native provider credential with same name "alpha"
        store->values[QStringLiteral("alpha")] = QStringLiteral("native_secret_value");
        // Script package credential keyed strictly by provider_pkg:id:key
        store->values[QStringLiteral("provider_pkg:alpha:alpha_key")] = QStringLiteral("script_isolated_secret");
        store->values[QStringLiteral("provider_pkg:beta:alpha_key")] = QStringLiteral("other_script_secret");

        auto *mockNam = new FakeNetworkAccessManager(this);
        QNetworkRequest capturedReq;
        mockNam->handler = [&](const QNetworkRequest &req, QNetworkAccessManager::Operation) -> FakeNetworkReply * {
            capturedReq = req;
            auto *reply = new FakeNetworkReply(req, mockNam);
            reply->respondSuccess(200, "OK");
            return reply;
        };

        ProviderHostService host(pkg, this, mockNam);
        host.setCredentialStore(store);

        QSignalSpy spy(&host, &ProviderHostService::httpResponseReady);
        HttpRequestOptions opts;
        opts.url = QUrl(QStringLiteral("https://api.alpha.example.com/search"));
        opts.secretKey = QStringLiteral("alpha_key");
        opts.secretHeader = QStringLiteral("X-API-Key");
        opts.requestId = 300;
        host.executeHttpRequest(opts);

        QVERIFY(spy.wait(500));
        auto resp = spy.takeFirst().at(1).value<HttpResponse>();
        QVERIFY(resp.success);

        // Verify the injected header received the package-isolated key, NOT native or other package credentials
        QCOMPARE(capturedReq.rawHeader("X-API-Key"), QByteArray("script_isolated_secret"));
        delete store;
    }

    void testPackageContentHashIncludesAllAssets()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString dirPath = tempDir.path();

        // Write provider.json
        QFile manifest(dirPath + QStringLiteral("/provider.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write("{\"id\":\"test_pkg\",\"name\":\"Test\",\"entry\":\"main.mjs\"}");
        manifest.close();

        // Write main.mjs
        QFile mainFile(dirPath + QStringLiteral("/main.mjs"));
        QVERIFY(mainFile.open(QIODevice::WriteOnly));
        mainFile.write("import { helper } from './helper.mjs';\nexport function search() {}\n");
        mainFile.close();

        // Write helper.mjs
        QFile helperFile(dirPath + QStringLiteral("/helper.mjs"));
        QVERIFY(helperFile.open(QIODevice::WriteOnly));
        helperFile.write("export const helper = 42;\n");
        helperFile.close();

        auto pkg1 = ProviderPackage::fromDirectory(dirPath);
        QVERIFY(pkg1.isValid());
        const QByteArray hash1 = pkg1.contentHash();
        QVERIFY(!hash1.isEmpty());

        // Modify helper.mjs (external module asset)
        QVERIFY(helperFile.open(QIODevice::WriteOnly));
        helperFile.write("export const helper = 999; // tampered\n");
        helperFile.close();

        auto pkg2 = ProviderPackage::fromDirectory(dirPath);
        QVERIFY(pkg2.isValid());
        const QByteArray hash2 = pkg2.contentHash();

        // Hash MUST differ because helper.mjs was modified!
        QVERIFY(hash1 != hash2);
    }
};

QTEST_MAIN(TestProviderDiscovery)
#include "test_providerdiscovery.moc"
