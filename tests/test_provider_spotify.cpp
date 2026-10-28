#include "../providers/spotify/provider_spotify.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

class DummySpotifyReply : public QNetworkReply
{
    Q_OBJECT
public:
    DummySpotifyReply(QObject *parent, const QByteArray &data, QNetworkReply::NetworkError err = QNetworkReply::NoError,
                      int httpStatus = 200, const QByteArray &retryAfter = {})
        : QNetworkReply(parent), m_data(data)
    {
        setOpenMode(QIODevice::ReadOnly);
        setError(err, QString());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, httpStatus);
        if (!retryAfter.isEmpty())
            setRawHeader("Retry-After", retryAfter);
    }

    void finish()
    {
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    void abort() override
    {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"));
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return m_data.size() + QNetworkReply::bytesAvailable();
    }

    bool isSequential() const override
    {
        return true;
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const qint64 len = qMin(maxlen, qint64(m_data.size()));
        memcpy(data, m_data.constData(), len);
        m_data.remove(0, len);
        return len;
    }

private:
    QByteArray m_data;
};

class MockSpotifyNam : public QNetworkAccessManager
{
    Q_OBJECT
public:
    explicit MockSpotifyNam(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

    QList<DummySpotifyReply *> replies;
    QByteArray nextResponse;
    QNetworkReply::NetworkError nextError = QNetworkReply::NoError;
    int nextHttpStatus = 200;
    QByteArray nextRetryAfter;
    QUrl lastUrl;
    QByteArray lastAuthHeader;
    bool autoFinish = true;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override
    {
        Q_UNUSED(op);
        Q_UNUSED(outgoingData);
        lastUrl = request.url();
        lastAuthHeader = request.rawHeader("Authorization");
        auto *reply = new DummySpotifyReply(this, nextResponse, nextError, nextHttpStatus, nextRetryAfter);
        replies.append(reply);
        if (autoFinish)
            QTimer::singleShot(0, reply, &DummySpotifyReply::finish);
        return reply;
    }
};

class TestableProviderSpotify : public ProviderSpotify
{
public:
    explicit TestableProviderSpotify(QObject *parent = nullptr) : ProviderSpotify(parent)
    {
        QNetworkAccessManager *existing = findChild<QNetworkAccessManager *>();
        if (existing) {
            existing->setParent(nullptr);
            existing->deleteLater();
        }
        mockNam = new MockSpotifyNam(this);
        m_nam = mockNam;
    }

    MockSpotifyNam *mockNam = nullptr;
};

class TestProviderSpotify : public QObject
{
    Q_OBJECT
private slots:
    void testIdentityAndOAuthDescriptor();
    void testUnauthenticatedSearchFailsClearly();
    void testMultiTypeSearchAndPagination();
    void testTypeFilteringAndInvalidTokens();
    void testDetailsAndStructuredMetadata();
    void testAuthRejectionPolicyAndRateLimitHandling();
    void testClonePreservesCredentialsAndTypesAndCancelAborts();
};

void TestProviderSpotify::testIdentityAndOAuthDescriptor()
    {
        ProviderSpotify provider;
        QCOMPARE(provider.id(), QStringLiteral("spotify"));
        QCOMPARE(provider.name(), QStringLiteral("Spotify"));
        QVERIFY(provider.supportsAuthentication());
        QVERIFY(provider.supportsDetails());

        const AuthenticationDescriptor desc = provider.authenticationDescriptor();
        QCOMPARE(desc.mode, AuthenticationMode::OAuth2Pkce);
        QCOMPARE(desc.oauth.authorizationUrl, QUrl(QStringLiteral("https://accounts.spotify.com/authorize")));
        QCOMPARE(desc.oauth.tokenUrl, QUrl(QStringLiteral("https://accounts.spotify.com/api/token")));
        QCOMPARE(desc.oauth.callbackPort, quint16(43821));
        QCOMPARE(desc.oauth.callbackPath, QStringLiteral("/callback"));
        QVERIFY(desc.oauth.scopes.isEmpty());
    }

void TestProviderSpotify::testUnauthenticatedSearchFailsClearly()
    {
        TestableProviderSpotify provider;
        QSignalSpy failSpy(&provider, &SearchProvider::requestFailed);
        provider.requestPage(QStringLiteral("daft punk"), QString(), 11);
        QCOMPARE(failSpy.count(), 1);
        QCOMPARE(failSpy.at(0).at(0).toULongLong(), 11ULL);
        QVERIFY(failSpy.at(0).at(1).toString().contains(QStringLiteral("Spotify authentication is required")));
        QVERIFY(provider.mockNam->replies.isEmpty());
    }

void TestProviderSpotify::testMultiTypeSearchAndPagination()
    {
        TestableProviderSpotify provider;
        provider.setCredentials({}, QStringLiteral("test-access-token"));
        provider.mockNam->nextResponse = R"({
            "artists": {
                "items": [
                    {
                        "id": "4tZwfgrHOc3mvqYlEYSvVi",
                        "name": "Daft Punk",
                        "type": "artist",
                        "genres": ["electro", "filter house"],
                        "followers": {"total": 10500000},
                        "popularity": 84,
                        "external_urls": {"spotify": "https://open.spotify.com/artist/4tZwfgrHOc3mvqYlEYSvVi"},
                        "images": [{"url": "https://i.scdn.co/image/ab6761610000e5eb123456", "width": 640, "height": 640}]
                    }
                ],
                "offset": 0,
                "limit": 10,
                "total": 1
            },
            "albums": {
                "items": [
                    {
                        "id": "2noRn2Aes5aoNVsU6iWThc",
                        "name": "Discovery",
                        "type": "album",
                        "album_type": "album",
                        "release_date": "2001-03-12",
                        "total_tracks": 14,
                        "artists": [{"id": "4tZwfgrHOc3mvqYlEYSvVi", "name": "Daft Punk"}],
                        "external_urls": {"spotify": "javascript:evil"}
                    }
                ],
                "offset": 0,
                "limit": 10,
                "total": 25
            },
            "tracks": {
                "items": [
                    {
                        "id": "0DiWol3AO6WpXZgp0goxAV",
                        "name": "One More Time",
                        "type": "track",
                        "duration_ms": 320066,
                        "explicit": false,
                        "popularity": 82,
                        "artists": [{"id": "4tZwfgrHOc3mvqYlEYSvVi", "name": "Daft Punk"}],
                        "album": {
                            "id": "2noRn2Aes5aoNVsU6iWThc",
                            "name": "Discovery",
                            "release_date": "2001-03-12"
                        },
                        "external_urls": {"spotify": "https://open.spotify.com/track/0DiWol3AO6WpXZgp0goxAV"}
                    }
                ],
                "offset": 0,
                "limit": 10,
                "total": 40
            }
        })";

        QSignalSpy pageSpy(&provider, &SearchProvider::pageCompleted);
        provider.requestPage(QStringLiteral("daft punk"), QString(), 7);
        QVERIFY(pageSpy.wait(100));

        QCOMPARE(provider.mockNam->lastAuthHeader, QByteArrayLiteral("Bearer test-access-token"));
        const QUrlQuery q(provider.mockNam->lastUrl);
        QCOMPARE(q.queryItemValue(QStringLiteral("q")), QStringLiteral("daft punk"));
        QCOMPARE(q.queryItemValue(QStringLiteral("type")), QStringLiteral("artist,album,track"));
        QCOMPARE(q.queryItemValue(QStringLiteral("limit")), QStringLiteral("10"));

        const SearchPage page = pageSpy.at(0).at(1).value<SearchPage>();
        QCOMPARE(page.nextToken, QStringLiteral("10"));
        QCOMPARE(page.results.size(), 3);

        QCOMPARE(page.results[0].id, QStringLiteral("artist:4tZwfgrHOc3mvqYlEYSvVi"));
        QCOMPARE(page.results[0].title, QStringLiteral("Daft Punk"));
        QCOMPARE(page.results[0].type, QStringLiteral("artist"));
        QCOMPARE(page.results[0].url, QStringLiteral("https://open.spotify.com/artist/4tZwfgrHOc3mvqYlEYSvVi"));
        QVERIFY(page.results[0].description.contains(QStringLiteral("electro")));

        // Unsafe external_urls fallback to canonical open.spotify.com URL
        QCOMPARE(page.results[1].id, QStringLiteral("album:2noRn2Aes5aoNVsU6iWThc"));
        QCOMPARE(page.results[1].url, QStringLiteral("https://open.spotify.com/album/2noRn2Aes5aoNVsU6iWThc"));
        QCOMPARE(page.results[1].additionalData.value(QStringLiteral("release_date")).toString(), QStringLiteral("2001-03-12"));
        QCOMPARE(page.results[1].additionalData.value(QStringLiteral("total_tracks")).toInt(), 14);

        QCOMPARE(page.results[2].id, QStringLiteral("track:0DiWol3AO6WpXZgp0goxAV"));
        QCOMPARE(page.results[2].additionalData.value(QStringLiteral("duration")).toString(), QStringLiteral("5:20"));
        QCOMPARE(page.results[2].additionalData.value(QStringLiteral("album")).toString(), QStringLiteral("Discovery"));
    }

void TestProviderSpotify::testTypeFilteringAndInvalidTokens()
    {
        TestableProviderSpotify provider;
        provider.setCredentials({}, QStringLiteral("token"));
        provider.setSelectedTypes({QStringLiteral("show"), QStringLiteral("episode"), QStringLiteral("unknown_type")});
        QCOMPARE(provider.selectedTypes(), (QStringList{QStringLiteral("show"), QStringLiteral("episode")}));

        QSignalSpy failSpy(&provider, &SearchProvider::requestFailed);
        provider.requestPage(QStringLiteral("science"), QStringLiteral("1000"), 3);
        QCOMPARE(failSpy.count(), 1);
        QVERIFY(failSpy.at(0).at(1).toString().contains(QStringLiteral("Invalid continuation token")));
    }

void TestProviderSpotify::testDetailsAndStructuredMetadata()
    {
        TestableProviderSpotify provider;
        provider.setCredentials({}, QStringLiteral("token-xyz"));
        provider.mockNam->nextResponse = R"({
            "id": "0DiWol3AO6WpXZgp0goxAV",
            "name": "One More Time",
            "type": "track",
            "uri": "spotify:track:0DiWol3AO6WpXZgp0goxAV",
            "duration_ms": 320066,
            "explicit": false,
            "popularity": 82,
            "artists": [
                {
                    "id": "4tZwfgrHOc3mvqYlEYSvVi",
                    "name": "Daft Punk",
                    "external_urls": {"spotify": "https://open.spotify.com/artist/4tZwfgrHOc3mvqYlEYSvVi"}
                }
            ],
            "album": {
                "id": "2noRn2Aes5aoNVsU6iWThc",
                "name": "Discovery",
                "release_date": "2001-03-12",
                "images": [
                    {"url": "https://i.scdn.co/image/ab67616d0000b273123456", "width": 640, "height": 640},
                    {"url": "javascript:evil", "width": 300, "height": 300}
                ]
            },
            "external_urls": {"spotify": "https://open.spotify.com/track/0DiWol3AO6WpXZgp0goxAV"}
        })";

        QSignalSpy detailSpy(&provider, &SearchProvider::detailsCompleted);
        provider.requestDetails(QStringLiteral("track:0DiWol3AO6WpXZgp0goxAV"), QString(), 21);
        QVERIFY(detailSpy.wait(100));

        QCOMPARE(provider.mockNam->lastUrl, QUrl(QStringLiteral("https://api.spotify.com/v1/tracks/0DiWol3AO6WpXZgp0goxAV")));
        const SearchDetails details = detailSpy.at(0).at(1).value<SearchDetails>();
        QCOMPARE(details.result.id, QStringLiteral("track:0DiWol3AO6WpXZgp0goxAV"));
        QCOMPARE(details.metadata.sections.size(), 3);
        QCOMPARE(details.metadata.sections[0].label, QStringLiteral("Overview"));
        QCOMPARE(details.metadata.sections[1].label, QStringLiteral("Associations"));
        QCOMPARE(details.metadata.sections[2].label, QStringLiteral("Artwork and links"));
        // Unsafe javascript: image URL is filtered out; only canonical Spotify link + 1 valid HTTPS artwork remain
        QCOMPARE(details.metadata.sections[2].values.size(), 2);
        QCOMPARE(details.metadata.sections[2].values[1].kind, MetadataValue::Kind::Image);
        QCOMPARE(details.metadata.attribution, QStringList{QStringLiteral("Data provided by Spotify Web API.")});
    }

void TestProviderSpotify::testAuthRejectionPolicyAndRateLimitHandling()
    {
        TestableProviderSpotify provider;
        provider.setCredentials({}, QStringLiteral("expired-token"));
        provider.mockNam->nextHttpStatus = 401;
        provider.mockNam->nextResponse = R"({"error": {"status": 401, "message": "The access token expired"}})";

        QSignalSpy authRejectedSpy(&provider, &SearchProvider::authenticationRejected);
        QSignalSpy failSpy(&provider, &SearchProvider::requestFailed);

        provider.requestPage(QStringLiteral("query"), QString(), 31);
        QVERIFY(failSpy.wait(100));
        QCOMPARE(authRejectedSpy.count(), 1);
        QVERIFY(failSpy.at(0).at(1).toString().contains(QStringLiteral("The access token expired")));

        failSpy.clear();
        provider.mockNam->nextHttpStatus = 403;
        provider.mockNam->nextResponse = R"({"error": {"status": 403, "message": "Check settings on developer.spotify.com/dashboard"}})";
        provider.requestPage(QStringLiteral("query"), QString(), 32);
        QVERIFY(failSpy.wait(100));
        QVERIFY(failSpy.at(0).at(1).toString().contains(QStringLiteral("HTTP 403")));

        failSpy.clear();
        provider.mockNam->nextHttpStatus = 429;
        provider.mockNam->nextRetryAfter = QByteArrayLiteral("7");
        provider.mockNam->nextResponse = R"({"error": {"status": 429, "message": "API rate limit exceeded"}})";
        provider.requestPage(QStringLiteral("query"), QString(), 33);
        QVERIFY(failSpy.wait(100));
        QVERIFY(failSpy.at(0).at(1).toString().contains(QStringLiteral("429")));
        QCOMPARE(failSpy.at(0).at(2).toInt(), 7000);
    }

void TestProviderSpotify::testClonePreservesCredentialsAndTypesAndCancelAborts()
    {
        TestableProviderSpotify provider;
        provider.setCredentials({}, QStringLiteral("cloned-token"));
        provider.setSelectedTypes({QStringLiteral("artist"), QStringLiteral("album")});

        QScopedPointer<SearchProvider> clonedBase(provider.clone());
        auto *cloned = qobject_cast<ProviderSpotify *>(clonedBase.data());
        QVERIFY(cloned);
        QCOMPARE(cloned->selectedTypes(), (QStringList{QStringLiteral("artist"), QStringLiteral("album")}));

        provider.mockNam->autoFinish = false;
        provider.requestPage(QStringLiteral("test"), QString(), 50);
        QCOMPARE(provider.mockNam->replies.size(), 1);
        QVERIFY(provider.mockNam->replies.first()->isRunning());
        provider.cancelRequests();
        QVERIFY(provider.mockNam->replies.first()->isFinished());
    }

QTEST_MAIN(TestProviderSpotify)
#include "test_provider_spotify.moc"
