// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providerhostservice.h"
#include "providerpackage.h"

#include <QSignalSpy>
#include <QTest>

using namespace ProviderHost;

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

        // Allowed: api.beta.example.com
        QVERIFY(pkg.isOriginAllowed(QUrl(QStringLiteral("https://api.beta.example.com/v1/search"))));
        QVERIFY(pkg.isOriginAllowed(QUrl(QStringLiteral("http://api.beta.example.com:8080/"))));

        // Allowed: *.beta.net
        QVERIFY(pkg.isOriginAllowed(QUrl(QStringLiteral("https://cdn.beta.net/image.png"))));
        QVERIFY(pkg.isOriginAllowed(QUrl(QStringLiteral("https://beta.net/"))));

        // Disallowed
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
};

QTEST_MAIN(TestProviderDiscovery)
#include "test_providerdiscovery.moc"
