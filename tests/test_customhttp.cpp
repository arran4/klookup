// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QSignalSpy>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <KSharedConfig>

#include "customhttpsource.h"
#include "customhttpprovider.h"
#include "customhttpsourcemanager.h"

class MockNetworkReply : public QNetworkReply
{
public:
    MockNetworkReply(const QByteArray &content,
                     int statusCode = 200,
                     QNetworkReply::NetworkError error = QNetworkReply::NoError,
                     const QString &errorString = QString(),
                     qint64 overrideContentLength = -1,
                     const QUrl &redirectUrl = QUrl(),
                     bool chunkedStream = false,
                     QObject *parent = nullptr)
        : QNetworkReply(parent), m_data(content), m_offset(0)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        if (redirectUrl.isValid()) {
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, redirectUrl);
        }
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (overrideContentLength >= 0) {
            setHeader(QNetworkRequest::ContentLengthHeader, overrideContentLength);
        } else if (!chunkedStream) {
            setHeader(QNetworkRequest::ContentLengthHeader, content.size());
        }
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (error != QNetworkReply::NoError) {
            setError(error, errorString);
            setErrorString(errorString);
        }

        if (chunkedStream && content.size() > 1024) {
            QTimer::singleShot(0, this, [this] {
                emit metaDataChanged();
                emit readyRead();
                QTimer::singleShot(1, this, [this] {
                    setFinished(true);
                    emit readyRead();
                    emit finished();
                });
            });
        } else {
            QTimer::singleShot(0, this, [this] {
                emit metaDataChanged();
                emit readyRead();
                setFinished(true);
                emit finished();
            });
        }
    }

    void abort() override {
        m_aborted = true;
        close();
    }

    bool wasAborted() const { return m_aborted; }

    qint64 bytesAvailable() const override {
        return m_data.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override {
        if (m_offset >= m_data.size()) return 0;
        qint64 bytesToRead = qMin(maxSize, static_cast<qint64>(m_data.size() - m_offset));
        memcpy(data, m_data.constData() + m_offset, bytesToRead);
        m_offset += bytesToRead;
        return bytesToRead;
    }

    qint64 writeData(const char *, qint64) override {
        return -1;
    }

private:
    QByteArray m_data;
    qint64 m_offset;
    bool m_aborted = false;
};

class MockNetworkAccessManager : public QNetworkAccessManager
{
public:
    explicit MockNetworkAccessManager(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    QByteArray nextResponse;
    int nextStatusCode = 200;
    QNetworkReply::NetworkError nextError = QNetworkReply::NoError;
    QString nextErrorString;
    qint64 nextOverrideContentLength = -1;
    QUrl nextRedirectUrl;
    bool nextChunkedStream = false;
    int requestCount = 0;

    QUrl lastRequestUrl;
    QByteArray lastRequestBody;
    QMap<QByteArray, QByteArray> lastRequestHeaders;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override {
        Q_UNUSED(op);
        requestCount++;
        lastRequestUrl = request.url();
        lastRequestHeaders.clear();
        for (const auto &headerName : request.rawHeaderList()) {
            lastRequestHeaders.insert(headerName.toLower(), request.rawHeader(headerName));
        }
        if (outgoingData) {
            lastRequestBody = outgoingData->readAll();
        } else {
            lastRequestBody.clear();
        }

        return new MockNetworkReply(nextResponse, nextStatusCode, nextError, nextErrorString,
                                    nextOverrideContentLength, nextRedirectUrl, nextChunkedStream, this);
    }
};

class TestCustomHttp : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("KDE_FORK_SLAVES", "1");
    }

    void testQueryEscaping()
    {
        const QString query = QStringLiteral("foo & bar \"quotes\" \n next");
        const QString urlEncoded = CustomHttpUtils::urlEncodeQuery(query);
        QVERIFY(!urlEncoded.contains(QLatin1Char(' ')));
        QVERIFY(!urlEncoded.contains(QLatin1Char('&')));

        const QString jsonEscaped = CustomHttpUtils::jsonEscapeQuery(query);
        QVERIFY(jsonEscaped.contains(QStringLiteral("\\\"quotes\\\"")));
        QVERIFY(jsonEscaped.contains(QStringLiteral("\\n")));

        const QString formattedUrl = CustomHttpUtils::formatUrl(QStringLiteral("https://example.org/search?q={query}"), QStringLiteral("hello world"));
        QCOMPARE(formattedUrl, QStringLiteral("https://example.org/search?q=hello%20world"));

        const QByteArray formattedBody = CustomHttpUtils::formatBody(QStringLiteral("{\"query\": \"{query}\"}"), QStringLiteral("application/json"), QStringLiteral("test \"quote\""));
        QCOMPARE(formattedBody, QByteArray("{\"query\": \"test \\\"quote\\\"\"}"));
    }

    void testJsonPathExtraction()
    {
        const QByteArray json =
            "{\n"
            "    \"status\": \"ok\",\n"
            "    \"data\": {\n"
            "        \"items\": [\n"
            "            {\"id\": \"item1\", \"title\": \"First\"},\n"
            "            {\"id\": \"item2\", \"title\": \"Second\"}\n"
            "        ]\n"
            "    }\n"
            "}";
        const QJsonDocument doc = QJsonDocument::fromJson(json);

        QCOMPARE(CustomHttpUtils::extractPath(doc.object(), QStringLiteral("status")).toString(), QStringLiteral("ok"));
        QCOMPARE(CustomHttpUtils::extractPath(doc.object(), QStringLiteral("data.items[0].id")).toString(), QStringLiteral("item1"));
        QCOMPARE(CustomHttpUtils::extractPath(doc.object(), QStringLiteral("data.items[1].title")).toString(), QStringLiteral("Second"));
        QVERIFY(CustomHttpUtils::extractPath(doc.object(), QStringLiteral("data.nonexistent")).isUndefined());
    }

    void testResultsExtraction()
    {
        const QByteArray json =
            "{\n"
            "    \"results\": [\n"
            "        {\n"
            "            \"uid\": \"101\",\n"
            "            \"headline\": \"KDE Plasma 6.3 Released\",\n"
            "            \"link\": \"https://kde.org/announcements/plasma/6/6.3.0/\",\n"
            "            \"summary\": \"Great desktop environment update\",\n"
            "            \"category\": \"Desktop\"\n"
            "        },\n"
            "        {\n"
            "            \"uid\": \"102\",\n"
            "            \"headline\": \"Dangerous Link\",\n"
            "            \"link\": \"javascript:alert(1)\",\n"
            "            \"summary\": \"Should be filtered\",\n"
            "            \"category\": \"Security\"\n"
            "        }\n"
            "    ]\n"
            "}";
        const QJsonDocument doc = QJsonDocument::fromJson(json);

        CustomHttpFieldMapping mapping;
        mapping.resultsRoot = QStringLiteral("results");
        mapping.idField = QStringLiteral("uid");
        mapping.titleField = QStringLiteral("headline");
        mapping.urlField = QStringLiteral("link");
        mapping.descriptionField = QStringLiteral("summary");
        mapping.typeField = QStringLiteral("category");

        const QList<SearchResult> results = CustomHttpUtils::extractResults(doc, mapping, QStringLiteral("my-provider"));
        QCOMPARE(results.size(), 2);

        QCOMPARE(results.at(0).id, QStringLiteral("101"));
        QCOMPARE(results.at(0).title, QStringLiteral("KDE Plasma 6.3 Released"));
        QCOMPARE(results.at(0).url, QStringLiteral("https://kde.org/announcements/plasma/6/6.3.0/"));
        QCOMPARE(results.at(0).description, QStringLiteral("Great desktop environment update"));
        QCOMPARE(results.at(0).type, QStringLiteral("Desktop"));

        // Unsafe javascript: link should be filtered out
        QCOMPARE(results.at(1).id, QStringLiteral("102"));
        QVERIFY(results.at(1).url.isEmpty());
    }

    void testDefinitionValidation()
    {
        CustomHttpSourceDefinition def;
        QString error;
        QVERIFY(!def.isValid(&error));

        def.id = QStringLiteral("test-source");
        def.name = QStringLiteral("Test Source");
        QVERIFY(!def.isValid(&error));

        // Built-in ID collision
        def.id = QStringLiteral("wikipedia");
        def.urlTemplate = QStringLiteral("https://example.org/search?q={query}");
        QVERIFY(!def.isValid(&error));
        QVERIFY(error.contains(QStringLiteral("conflicts with a built-in provider")));

        def.id = QStringLiteral("duckduckgo");
        QVERIFY(!def.isValid(&error));

        // Invalid characters in ID
        def.id = QStringLiteral("bad id!");
        QVERIFY(!def.isValid(&error));

        def.id = QStringLiteral("test-source");

        // Invalid scheme
        def.urlTemplate = QStringLiteral("ftp://example.org/search?q={query}");
        QVERIFY(!def.isValid(&error));

        // Missing placeholder
        def.urlTemplate = QStringLiteral("https://example.org/search?q=constant");
        QVERIFY(!def.isValid(&error));

        // Valid GET
        def.urlTemplate = QStringLiteral("https://example.org/search?q={query}");
        QVERIFY(def.isValid(&error));

        // POST validation
        def.method = CustomHttpSourceDefinition::Method::Post;
        def.postContentType = QStringLiteral("application/json");
        def.bodyTemplate = QStringLiteral("{\"query\": \"{query}\"}");
        def.urlTemplate = QStringLiteral("https://example.org/api/search");
        QVERIFY(def.isValid(&error));
    }

    void testDefinitionSerialization()
    {
        CustomHttpSourceDefinition def;
        def.id = QStringLiteral("custom-hn");
        def.name = QStringLiteral("Hacker News");
        def.description = QStringLiteral("Algolia HN Search");
        def.method = CustomHttpSourceDefinition::Method::Get;
        def.urlTemplate = QStringLiteral("https://hn.algolia.com/api/v1/search?query={query}");
        def.headers.insert(QStringLiteral("Accept"), QStringLiteral("application/json"));
        def.authHeader = QStringLiteral("Authorization");
        def.authSecretToken = QStringLiteral("Bearer secret123");
        def.mapping.resultsRoot = QStringLiteral("hits");
        def.mapping.titleField = QStringLiteral("title");
        def.mapping.urlField = QStringLiteral("url");

        // Without secret
        const QJsonObject jsonNoSecret = def.toJson(false);
        QVERIFY(!jsonNoSecret.contains(QStringLiteral("authSecretToken")));

        // With secret
        const QJsonObject jsonWithSecret = def.toJson(true);
        QCOMPARE(jsonWithSecret.value(QStringLiteral("authSecretToken")).toString(), QStringLiteral("Bearer secret123"));

        // Roundtrip
        const auto restored = CustomHttpSourceDefinition::fromJson(jsonWithSecret);
        QCOMPARE(restored.id, def.id);
        QCOMPARE(restored.name, def.name);
        QCOMPARE(restored.urlTemplate, def.urlTemplate);
        QCOMPARE(restored.mapping.resultsRoot, def.mapping.resultsRoot);
    }

    void testSourceManager()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        CustomHttpSourceManager manager;
        QCOMPARE(manager.sources().size(), 0);

        CustomHttpSourceDefinition def;
        def.id = QStringLiteral("src-1");
        def.name = QStringLiteral("First Source");
        def.urlTemplate = QStringLiteral("https://example.org/search?q={query}");

        manager.addOrUpdateSource(def);
        QCOMPARE(manager.sources().size(), 1);
        QCOMPARE(manager.source(QStringLiteral("src-1")).name, QStringLiteral("First Source"));

        manager.setSourceEnabled(QStringLiteral("src-1"), false);
        QVERIFY(!manager.source(QStringLiteral("src-1")).enabled);

        // Verify persistence
        CustomHttpSourceManager freshManager;
        QCOMPARE(freshManager.sources().size(), 1);
        QCOMPARE(freshManager.source(QStringLiteral("src-1")).name, QStringLiteral("First Source"));
        QVERIFY(!freshManager.source(QStringLiteral("src-1")).enabled);

        // Export & Import
        const QString exportedJson = manager.exportJson();
        QVERIFY(!exportedJson.isEmpty());

        manager.removeSource(QStringLiteral("src-1"));
        QCOMPARE(manager.sources().size(), 0);

        QString importErr;
        QVERIFY(manager.importJson(exportedJson, &importErr));
        QCOMPARE(manager.sources().size(), 1);
        QCOMPARE(manager.source(QStringLiteral("src-1")).name, QStringLiteral("First Source"));
    }

    void testCustomHttpProviderExecution()
    {
        MockNetworkAccessManager mockNam;
        mockNam.nextResponse =
            "{\n"
            "    \"items\": [\n"
            "        {\n"
            "            \"id\": \"item-abc\",\n"
            "            \"title\": \"Mock Title\",\n"
            "            \"url\": \"https://example.org/item-abc\",\n"
            "            \"description\": \"Mock Description\"\n"
            "        }\n"
            "    ]\n"
            "}";

        CustomHttpSourceDefinition def;
        def.id = QStringLiteral("mock-custom");
        def.name = QStringLiteral("Mock Custom Provider");
        def.urlTemplate = QStringLiteral("https://api.example.org/search?q={query}");
        def.headers.insert(QStringLiteral("X-API-Client"), QStringLiteral("KLookup"));
        def.authHeader = QStringLiteral("Authorization");
        def.authSecretToken = QStringLiteral("Bearer {secret}");
        def.mapping.resultsRoot = QStringLiteral("items");
        def.mapping.idField = QStringLiteral("id");
        def.mapping.titleField = QStringLiteral("title");
        def.mapping.urlField = QStringLiteral("url");
        def.mapping.descriptionField = QStringLiteral("description");

        CustomHttpProvider provider(def, nullptr, &mockNam);
        provider.setSecretToken(QStringLiteral("my-secret-key-123"));

        QSignalSpy resultsSpy(&provider, &SearchProvider::searchCompleted);
        QSignalSpy errorSpy(&provider, &SearchProvider::searchFailed);

        provider.search(QStringLiteral("plasma desktop"));

        QCOMPARE(mockNam.lastRequestUrl.toEncoded(), QByteArray("https://api.example.org/search?q=plasma%20desktop"));
        QCOMPARE(mockNam.lastRequestHeaders.value("x-api-client"), QByteArray("KLookup"));
        QCOMPARE(mockNam.lastRequestHeaders.value("authorization"), QByteArray("Bearer my-secret-key-123"));

        QTRY_COMPARE(resultsSpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        const auto results = resultsSpy.first().at(1).value<QList<SearchResult>>();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().id, QStringLiteral("item-abc"));
        QCOMPARE(results.first().title, QStringLiteral("Mock Title"));
        QCOMPARE(results.first().url, QStringLiteral("https://example.org/item-abc"));
    }

    void testCustomHttpProviderPostRequest()
    {
        MockNetworkAccessManager mockNam;
        mockNam.nextResponse =
            "[\n"
            "    {\"id\": \"post-1\", \"title\": \"Post Result\", \"url\": \"https://example.org/post\"}\n"
            "]";

        CustomHttpSourceDefinition def;
        def.id = QStringLiteral("mock-post");
        def.name = QStringLiteral("Mock POST Provider");
        def.method = CustomHttpSourceDefinition::Method::Post;
        def.urlTemplate = QStringLiteral("https://api.example.org/search");
        def.postContentType = QStringLiteral("application/json");
        def.bodyTemplate = QStringLiteral("{\"query\": \"{query}\", \"limit\": 10}");
        def.mapping.resultsRoot = QStringLiteral(".");
        def.mapping.idField = QStringLiteral("id");
        def.mapping.titleField = QStringLiteral("title");
        def.mapping.urlField = QStringLiteral("url");

        CustomHttpProvider provider(def, nullptr, &mockNam);

        QSignalSpy resultsSpy(&provider, &SearchProvider::searchCompleted);
        provider.search(QStringLiteral("test \"quoted\" query"));

        QCOMPARE(mockNam.lastRequestUrl.toString(), QStringLiteral("https://api.example.org/search"));
        QCOMPARE(mockNam.lastRequestBody, QByteArray("{\"query\": \"test \\\"quoted\\\" query\", \"limit\": 10}"));

        QTRY_COMPARE(resultsSpy.count(), 1);
        const auto results = resultsSpy.first().at(1).value<QList<SearchResult>>();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().title, QStringLiteral("Post Result"));
    }

    void testCustomHttpProviderErrorHandling()
    {
        MockNetworkAccessManager mockNam;
        mockNam.nextStatusCode = 500;
        mockNam.nextError = QNetworkReply::InternalServerError;
        mockNam.nextErrorString = QStringLiteral("Server exploded");

        CustomHttpSourceDefinition def;
        def.id = QStringLiteral("mock-err");
        def.name = QStringLiteral("Mock Error Provider");
        def.urlTemplate = QStringLiteral("https://api.example.org/error?q={query}");

        CustomHttpProvider provider(def, nullptr, &mockNam);

        QSignalSpy resultsSpy(&provider, &SearchProvider::searchCompleted);
        QSignalSpy errorSpy(&provider, &SearchProvider::searchFailed);

        provider.search(QStringLiteral("error query"));

        QTRY_COMPARE(errorSpy.count(), 1);
        QCOMPARE(resultsSpy.count(), 0);
        QVERIFY(errorSpy.first().at(1).toString().contains(QStringLiteral("Server exploded")));
    }

    void testImportTrustAndValidation()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());

        CustomHttpSourceManager manager;

        const QString importJson = QStringLiteral(
            "[\n"
            "  {\n"
            "    \"id\": \"imported-source\",\n"
            "    \"name\": \"Imported Test Source\",\n"
            "    \"enabled\": true,\n"
            "    \"trusted\": true,\n"
            "    \"urlTemplate\": \"https://api.example.com/search?q={query}\",\n"
            "    \"mapping\": {\"titleField\": \"name\"}\n"
            "  }\n"
            "]"
        );

        QString importErr;
        QVERIFY(manager.importJson(importJson, &importErr));

        const auto imported = manager.source(QStringLiteral("imported-source"));
        QCOMPARE(imported.id, QStringLiteral("imported-source"));
        QCOMPARE(imported.enabled, false);
        QCOMPARE(imported.trusted, false);

        MockNetworkAccessManager mockNam;
        CustomHttpProvider untrustedProvider(imported, nullptr, &mockNam);

        QSignalSpy resultsSpy(&untrustedProvider, &SearchProvider::searchCompleted);
        QSignalSpy errorSpy(&untrustedProvider, &SearchProvider::searchFailed);

        untrustedProvider.search(QStringLiteral("test query"));

        QCOMPARE(errorSpy.count(), 1);
        QCOMPARE(resultsSpy.count(), 0);
        QVERIFY(errorSpy.first().at(1).toString().contains(QStringLiteral("untrusted")));
        QCOMPARE(mockNam.requestCount, 0);

        manager.setSourceTrusted(QStringLiteral("imported-source"), true);
        manager.setSourceEnabled(QStringLiteral("imported-source"), true);
        const auto trustedDef = manager.source(QStringLiteral("imported-source"));
        QCOMPARE(trustedDef.trusted, true);
        QCOMPARE(trustedDef.enabled, true);

        mockNam.nextResponse = "{\"results\": [{\"name\": \"Trusted Result\"}]}";
        CustomHttpProvider trustedProvider(trustedDef, nullptr, &mockNam);
        QSignalSpy trustedResultsSpy(&trustedProvider, &SearchProvider::searchCompleted);
        trustedProvider.search(QStringLiteral("test query"));
        QTRY_COMPARE(trustedResultsSpy.count(), 1);
        QCOMPARE(mockNam.requestCount, 1);

        auto modified = trustedDef;
        modified.urlTemplate = QStringLiteral("https://evil.example.org/search?q={query}");
        modified.trusted = false;
        manager.addOrUpdateSource(modified);
        QCOMPARE(manager.source(QStringLiteral("imported-source")).trusted, false);
        QCOMPARE(manager.source(QStringLiteral("imported-source")).enabled, false);
    }

    void testStreamingResponseBounds()
    {
        MockNetworkAccessManager mockNam;

        CustomHttpSourceDefinition def;
        def.id = QStringLiteral("stream-bound-test");
        def.name = QStringLiteral("Stream Bound Test");
        def.urlTemplate = QStringLiteral("https://api.example.org/stream?q={query}");
        def.trusted = true;

        CustomHttpProvider provider(def, nullptr, &mockNam);

        // 1. Content-Length header fast-fail exceeding 5 MiB
        mockNam.nextOverrideContentLength = 10 * 1024 * 1024;
        mockNam.nextResponse = "{\"items\": []}";

        QSignalSpy resultsSpy1(&provider, &SearchProvider::searchCompleted);
        QSignalSpy errorSpy1(&provider, &SearchProvider::searchFailed);

        provider.search(QStringLiteral("test"));

        QTRY_COMPARE(errorSpy1.count(), 1);
        QCOMPARE(resultsSpy1.count(), 0);
        QVERIFY(errorSpy1.first().at(1).toString().contains(QStringLiteral("exceeds maximum limit of 5 MiB")));

        // 2. Streaming accumulation abort exceeding 5 MiB
        mockNam.nextOverrideContentLength = -1;
        mockNam.nextResponse = QByteArray(6 * 1024 * 1024, 'a');
        mockNam.nextChunkedStream = true;

        QSignalSpy resultsSpy2(&provider, &SearchProvider::searchCompleted);
        QSignalSpy errorSpy2(&provider, &SearchProvider::searchFailed);

        provider.search(QStringLiteral("test2"));

        QTRY_COMPARE(errorSpy2.count(), 1);
        QCOMPARE(resultsSpy2.count(), 0);
        QVERIFY(errorSpy2.first().at(1).toString().contains(QStringLiteral("exceeded maximum limit of 5 MiB")));

        // 3. Response at exact 5 MiB limit passes
        const QByteArray prefix = "{\"title\": \"Exact Limit\", \"padding\": \"";
        const QByteArray suffix = "\"}";
        const int paddingSize = (5 * 1024 * 1024) - prefix.size() - suffix.size();
        mockNam.nextOverrideContentLength = 5 * 1024 * 1024;
        mockNam.nextResponse = prefix + QByteArray(paddingSize, 'x') + suffix;
        QCOMPARE(mockNam.nextResponse.size(), 5 * 1024 * 1024);
        mockNam.nextChunkedStream = false;

        def.mapping.titleField = QStringLiteral("title");
        provider.setDefinition(def);

        QSignalSpy resultsSpy3(&provider, &SearchProvider::searchCompleted);
        QSignalSpy errorSpy3(&provider, &SearchProvider::searchFailed);

        provider.search(QStringLiteral("test3"));

        QTRY_COMPARE(resultsSpy3.count(), 1);
        QCOMPARE(errorSpy3.count(), 0);
    }

    void testKWalletAuthenticationAndSecretRedaction()
    {
        CustomHttpSourceDefinition def;
        def.id = QStringLiteral("auth-custom");
        def.name = QStringLiteral("Auth Custom");
        def.urlTemplate = QStringLiteral("https://api.example.org/search?q={query}&key={secret}");
        def.authHeader = QStringLiteral("Authorization");
        def.authSecretToken = QStringLiteral("Bearer {secret}");
        def.headers.insert(QStringLiteral("X-API-Key"), QStringLiteral("{secret}"));
        def.trusted = true;

        QVERIFY(def.requiresSecret());

        def.authSecretToken = QStringLiteral("Bearer secret1234567890abcdef");
        def.headers.insert(QStringLiteral("Authorization"), QStringLiteral("Bearer token1234567890abcdef"));
        const QJsonObject jsonNoSecret = def.toJson(false);
        QVERIFY(!jsonNoSecret.contains(QStringLiteral("authSecretToken")));
        QCOMPARE(jsonNoSecret.value(QStringLiteral("headers")).toObject().value(QStringLiteral("Authorization")).toString(), QStringLiteral("Bearer {secret}"));
        QVERIFY(!QJsonDocument(jsonNoSecret).toJson().contains("secret1234567890abcdef"));
        QVERIFY(!QJsonDocument(jsonNoSecret).toJson().contains("token1234567890abcdef"));

        // Reset to template for runtime credential substitution
        def.headers.remove(QStringLiteral("Authorization"));
        def.authSecretToken = QStringLiteral("Bearer {secret}");

        MockNetworkAccessManager mockNam;
        mockNam.nextResponse = "{\"results\": []}";

        CustomHttpProvider provider(def, nullptr, &mockNam);
        QVERIFY(provider.supportsAuthentication());

        provider.setCredentials(QString(), QStringLiteral("my-secret-vault-token"));

        QSignalSpy resultsSpy(&provider, &SearchProvider::searchCompleted);
        provider.search(QStringLiteral("plasma"));

        QTRY_COMPARE(resultsSpy.count(), 1);
        QCOMPARE(mockNam.lastRequestUrl.toEncoded(), QByteArray("https://api.example.org/search?q=plasma&key=my-secret-vault-token"));
        QCOMPARE(mockNam.lastRequestHeaders.value("authorization"), QByteArray("Bearer my-secret-vault-token"));
        QCOMPARE(mockNam.lastRequestHeaders.value("x-api-key"), QByteArray("my-secret-vault-token"));

        auto *cloned = static_cast<CustomHttpProvider *>(provider.clone());
        QVERIFY(cloned->supportsAuthentication());
        cloned->search(QStringLiteral("desktop"));
        QCOMPARE(mockNam.lastRequestHeaders.value("authorization"), QByteArray("Bearer my-secret-vault-token"));
        delete cloned;
    }

    void testCrossOriginRedirectProtection()
    {
        MockNetworkAccessManager mockNam;
        mockNam.nextStatusCode = 302;
        mockNam.nextRedirectUrl = QUrl(QStringLiteral("https://malicious.evil.com/steal"));
        mockNam.nextResponse = "";

        CustomHttpSourceDefinition def;
        def.id = QStringLiteral("redirect-test");
        def.name = QStringLiteral("Redirect Test");
        def.urlTemplate = QStringLiteral("https://api.example.org/search?q={query}");
        def.authHeader = QStringLiteral("Authorization");
        def.authSecretToken = QStringLiteral("Bearer {secret}");
        def.trusted = true;

        CustomHttpProvider provider(def, nullptr, &mockNam);
        provider.setCredentials(QString(), QStringLiteral("sensitive-token-12345"));

        QSignalSpy resultsSpy(&provider, &SearchProvider::searchCompleted);
        QSignalSpy errorSpy(&provider, &SearchProvider::searchFailed);

        provider.search(QStringLiteral("query"));

        QTRY_COMPARE(errorSpy.count(), 1);
        QCOMPARE(resultsSpy.count(), 0);
        QVERIFY(errorSpy.first().at(1).toString().contains(QStringLiteral("Cross-origin redirect to 'malicious.evil.com' blocked for credential protection")));
    }
};

QTEST_MAIN(TestCustomHttp)
#include "test_customhttp.moc"
