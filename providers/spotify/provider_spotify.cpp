#include "provider_spotify.h"
#include "providerrequest.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace
{
constexpr quint16 SpotifyLoopbackPort = 43821;
constexpr int MaxResponseBytes = 8 * 1024 * 1024;

QString formatDurationMs(qint64 durationMs)
{
    if (durationMs <= 0)
        return {};
    const qint64 totalSeconds = durationMs / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0)
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QStringList extractNames(const QJsonArray &items, int limit = 16)
{
    QStringList names;
    for (const auto &value : items) {
        if (names.size() >= limit)
            break;
        const QString name = value.toObject().value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty())
            names.append(name);
    }
    return names;
}

QString canonicalSpotifyUrl(const QJsonObject &obj, const QString &type, const QString &rawId)
{
    const QString external =
        obj.value(QStringLiteral("external_urls")).toObject().value(QStringLiteral("spotify")).toString().trimmed();
    const QUrl parsed = Metadata::safeWebUrl(external);
    if (!parsed.isEmpty() && (parsed.host() == QStringLiteral("open.spotify.com") || parsed.host().endsWith(QStringLiteral(".spotify.com"))))
        return parsed.toString(QUrl::FullyEncoded);
    return QStringLiteral("https://open.spotify.com/%1/%2").arg(type, rawId);
}

bool isValidSpotifyId(const QString &value)
{
    static const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9]{1,64}$"));
    return idPattern.match(value).hasMatch();
}

QString sectionKeyForType(const QString &type)
{
    return type + QLatin1Char('s');
}
} // namespace

ProviderSpotify::ProviderSpotify(QObject *parent)
    : SearchProvider(parent), m_nam(new QNetworkAccessManager(this)), m_types(defaultTypes())
{
    const KConfigGroup config(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Provider-spotify"));
    const QStringList configured = config.readEntry(QStringLiteral("Types"), QStringList());
    if (!configured.isEmpty())
        setSelectedTypes(configured);
}

ProviderSpotify::~ProviderSpotify()
{
    cancelRequests();
}

QString ProviderSpotify::name() const
{
    return QStringLiteral("Spotify");
}

QString ProviderSpotify::id() const
{
    return QStringLiteral("spotify");
}

QStringList ProviderSpotify::supportedTypes()
{
    return {
        QStringLiteral("artist"),   QStringLiteral("album"),   QStringLiteral("track"),     QStringLiteral("playlist"),
        QStringLiteral("show"),     QStringLiteral("episode"), QStringLiteral("audiobook"),
    };
}

QStringList ProviderSpotify::defaultTypes()
{
    return {
        QStringLiteral("artist"),
        QStringLiteral("album"),
        QStringLiteral("track"),
    };
}

void ProviderSpotify::setSelectedTypes(const QStringList &types)
{
    QStringList filtered;
    const QStringList supported = supportedTypes();
    for (const QString &entry : types) {
        const QString normalized = entry.trimmed().toLower();
        if (supported.contains(normalized) && !filtered.contains(normalized))
            filtered.append(normalized);
    }
    m_types = filtered.isEmpty() ? defaultTypes() : filtered;
}

void ProviderSpotify::setCredentials(const QString &username, const QString &password)
{
    Q_UNUSED(username);
    m_accessToken = password.trimmed();
}

AuthenticationDescriptor ProviderSpotify::authenticationDescriptor() const
{
    AuthenticationDescriptor descriptor;
    descriptor.mode = AuthenticationMode::OAuth2Pkce;
    descriptor.oauth.authorizationUrl = QUrl(QStringLiteral("https://accounts.spotify.com/authorize"));
    descriptor.oauth.tokenUrl = QUrl(QStringLiteral("https://accounts.spotify.com/api/token"));
    descriptor.oauth.scopes = {};
    descriptor.oauth.callbackPort = SpotifyLoopbackPort;
    descriptor.oauth.callbackPath = QStringLiteral("/callback");
    return descriptor;
}

void ProviderSpotify::cancelRequests()
{
    ProviderRequest::cancel(m_reply, this);
    ProviderRequest::cancel(m_detailReply, this);
}

void ProviderSpotify::search(const QString &query)
{
    requestPage(query, QString(), 0);
}

void ProviderSpotify::sendJsonRequest(QPointer<QNetworkReply> &slot, const QUrl &url, quint64 requestId,
                                      std::function<void(const QJsonObject &)> onSuccess)
{
    ProviderRequest::cancel(slot, this);
    if (m_accessToken.isEmpty()) {
        ProviderRequest::fail(
            this, requestId,
            QStringLiteral("Spotify authentication is required. Configure a Spotify Client ID and connect in Settings."));
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup/0.1 (https://github.com/arran4/klookup)"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_accessToken.toUtf8());
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);

    slot = m_nam->get(request);
    auto *reply = slot.data();
    reply->setReadBufferSize(MaxResponseBytes + 1);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > MaxResponseBytes) {
            reply->setProperty("responseTooLarge", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, &slot, reply = QPointer<QNetworkReply>(reply), requestId, onSuccess] {
        if (!reply || slot != reply)
            return;
        slot = nullptr;
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (reply->property("responseTooLarge").toBool() || payload.size() > MaxResponseBytes) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Response exceeds 8 MiB limit"));
            return;
        }

        auto extractApiMessage = [&payload]() -> QString {
            QJsonParseError err;
            const auto doc = QJsonDocument::fromJson(payload, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const auto errorValue = doc.object().value(QStringLiteral("error"));
                if (errorValue.isObject())
                    return errorValue.toObject().value(QStringLiteral("message")).toString().trimmed();
                if (errorValue.isString())
                    return errorValue.toString().trimmed();
            }
            return {};
        };

        if (status == 401) {
            emit authenticationRejected();
            const QString detail = extractApiMessage();
            ProviderRequest::fail(this, requestId,
                                  detail.isEmpty()
                                      ? QStringLiteral("Spotify authentication expired or was rejected (HTTP 401). Reconnect in Settings.")
                                      : QStringLiteral("Spotify authentication expired or was rejected: %1").arg(detail));
            return;
        }
        if (status == 403) {
            const QString detail = extractApiMessage();
            ProviderRequest::fail(
                this, requestId,
                detail.isEmpty()
                    ? QStringLiteral("Spotify API access is restricted for this application or account (HTTP 403).")
                    : QStringLiteral("Spotify API access is restricted (HTTP 403): %1").arg(detail));
            return;
        }
        if (status == 429 || status == 503) {
            const auto retry = reply->rawHeader("Retry-After");
            bool ok = false;
            const qint64 seconds = retry.toLongLong(&ok);
            const qint64 delay =
                ok ? qBound(qint64(0), seconds, qint64(86400)) * 1000
                   : QDateTime::currentDateTimeUtc().msecsTo(QDateTime::fromString(QString::fromLatin1(retry), Qt::RFC2822Date));
            const int retryMs = int(qBound(qint64(1000), delay, qint64(86400000)));
            const QString error = status == 429 ? QStringLiteral("Spotify rate limit exceeded (HTTP 429)")
                                                : QStringLiteral("Spotify service temporarily unavailable (HTTP 503)");
            if (requestId)
                emit requestFailed(requestId, error, retryMs);
            else
                emit searchFailed(id(), error);
            return;
        }
        if (status && (status < 200 || status >= 300)) {
            const QString detail = extractApiMessage();
            ProviderRequest::fail(this, requestId,
                                  detail.isEmpty() ? QStringLiteral("HTTP Error: %1").arg(status)
                                                   : QStringLiteral("HTTP Error %1: %2").arg(status).arg(detail));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            ProviderRequest::fail(this, requestId,
                                  reply->error() == QNetworkReply::TimeoutError ? QStringLiteral("Request timed out")
                                                                                : QStringLiteral("Network request failed"));
            return;
        }

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Failed to parse JSON response"));
            return;
        }
        onSuccess(doc.object());
    });
}

SearchResult ProviderSpotify::parseItem(const QJsonObject &obj, const QString &fallbackType) const
{
    if (obj.isEmpty())
        return {};
    const QString rawId = obj.value(QStringLiteral("id")).toString().trimmed();
    const QString type = obj.value(QStringLiteral("type")).toString(fallbackType).trimmed().toLower();
    const QString title = obj.value(QStringLiteral("name")).toString().trimmed();
    if (!isValidSpotifyId(rawId) || !supportedTypes().contains(type) || title.isEmpty())
        return {};

    SearchResult result;
    result.id = type + QLatin1Char(':') + rawId;
    result.title = title;
    result.type = type;
    result.url = canonicalSpotifyUrl(obj, type, rawId);

    QVariantMap additional;
    QStringList summaryParts;

    if (type == QStringLiteral("track")) {
        const QStringList artists = extractNames(obj.value(QStringLiteral("artists")).toArray());
        if (!artists.isEmpty()) {
            additional.insert(QStringLiteral("artists"), artists.join(QStringLiteral(", ")));
            summaryParts.append(artists.join(QStringLiteral(", ")));
        }
        const QJsonObject album = obj.value(QStringLiteral("album")).toObject();
        const QString albumName = album.value(QStringLiteral("name")).toString().trimmed();
        if (!albumName.isEmpty()) {
            additional.insert(QStringLiteral("album"), albumName);
            summaryParts.append(albumName);
        }
        const QString releaseDate = album.value(QStringLiteral("release_date")).toString().trimmed();
        if (!releaseDate.isEmpty())
            additional.insert(QStringLiteral("release_date"), releaseDate);
        const QString duration = formatDurationMs(obj.value(QStringLiteral("duration_ms")).toInteger());
        if (!duration.isEmpty()) {
            additional.insert(QStringLiteral("duration"), duration);
            summaryParts.append(duration);
        }
        if (obj.contains(QStringLiteral("explicit")) && obj.value(QStringLiteral("explicit")).isBool())
            additional.insert(QStringLiteral("explicit"), obj.value(QStringLiteral("explicit")).toBool());
    } else if (type == QStringLiteral("album")) {
        const QStringList artists = extractNames(obj.value(QStringLiteral("artists")).toArray());
        if (!artists.isEmpty()) {
            additional.insert(QStringLiteral("artists"), artists.join(QStringLiteral(", ")));
            summaryParts.append(artists.join(QStringLiteral(", ")));
        }
        const QString albumType = obj.value(QStringLiteral("album_type")).toString().trimmed();
        if (!albumType.isEmpty())
            additional.insert(QStringLiteral("album_type"), albumType);
        const QString releaseDate = obj.value(QStringLiteral("release_date")).toString().trimmed();
        if (!releaseDate.isEmpty()) {
            additional.insert(QStringLiteral("release_date"), releaseDate);
            summaryParts.append(releaseDate);
        }
        const int totalTracks = obj.value(QStringLiteral("total_tracks")).toInt();
        if (totalTracks > 0) {
            additional.insert(QStringLiteral("total_tracks"), totalTracks);
            summaryParts.append(QStringLiteral("%1 tracks").arg(totalTracks));
        }
    } else if (type == QStringLiteral("artist")) {
        QStringList genres;
        for (const auto &g : obj.value(QStringLiteral("genres")).toArray()) {
            const QString genre = g.toString().trimmed();
            if (!genre.isEmpty() && genres.size() < 12)
                genres.append(genre);
        }
        if (!genres.isEmpty()) {
            additional.insert(QStringLiteral("genres"), genres.join(QStringLiteral(", ")));
            summaryParts.append(genres.join(QStringLiteral(", ")));
        }
        const QJsonObject followers = obj.value(QStringLiteral("followers")).toObject();
        if (followers.value(QStringLiteral("total")).isDouble()) {
            const int totalFollowers = followers.value(QStringLiteral("total")).toInt();
            additional.insert(QStringLiteral("followers"), totalFollowers);
            summaryParts.append(QStringLiteral("%1 followers").arg(totalFollowers));
        }
    } else {
        const QString owner = obj.value(QStringLiteral("owner")).toObject().value(QStringLiteral("display_name")).toString().trimmed();
        if (!owner.isEmpty()) {
            additional.insert(QStringLiteral("owner"), owner);
            summaryParts.append(owner);
        }
        const QString publisher = obj.value(QStringLiteral("publisher")).toString().trimmed();
        if (!publisher.isEmpty()) {
            additional.insert(QStringLiteral("publisher"), publisher);
            summaryParts.append(publisher);
        }
        const QString desc = obj.value(QStringLiteral("description")).toString().trimmed();
        if (!desc.isEmpty())
            summaryParts.append(desc);
        const QString releaseDate = obj.value(QStringLiteral("release_date")).toString().trimmed();
        if (!releaseDate.isEmpty())
            additional.insert(QStringLiteral("release_date"), releaseDate);
        const QString duration = formatDurationMs(obj.value(QStringLiteral("duration_ms")).toInteger());
        if (!duration.isEmpty())
            additional.insert(QStringLiteral("duration"), duration);
    }

    if (obj.value(QStringLiteral("popularity")).isDouble())
        additional.insert(QStringLiteral("popularity"), obj.value(QStringLiteral("popularity")).toInt());

    const QJsonArray images = obj.value(QStringLiteral("images")).toArray().isEmpty()
                                  ? obj.value(QStringLiteral("album")).toObject().value(QStringLiteral("images")).toArray()
                                  : obj.value(QStringLiteral("images")).toArray();
    for (const auto &imgVal : images) {
        const QUrl imageUrl = Metadata::safeWebUrl(imgVal.toObject().value(QStringLiteral("url")).toString());
        if (!imageUrl.isEmpty()) {
            additional.insert(QStringLiteral("artwork_url"), imageUrl.toString(QUrl::FullyEncoded));
            break;
        }
    }

    result.description = summaryParts.join(QStringLiteral(" • "));
    result.additionalData = additional;
    return result;
}

void ProviderSpotify::requestPage(const QString &query, const QString &token, quint64 requestId)
{
    bool ok = false;
    const int requestedOffset = token.isEmpty() ? 0 : token.toInt(&ok);
    if (!token.isEmpty() && (!ok || requestedOffset < 0 || requestedOffset > 990)) {
        ProviderRequest::fail(this, requestId, QStringLiteral("Invalid continuation token"));
        return;
    }

    const QString trimmedQuery = query.trimmed();
    if (trimmedQuery.isEmpty()) {
        if (requestId)
            emit pageCompleted(requestId, {});
        else
            emit searchCompleted(id(), {});
        return;
    }

    QUrl url(QStringLiteral("https://api.spotify.com/v1/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), trimmedQuery);
    q.addQueryItem(QStringLiteral("type"), m_types.join(QLatin1Char(',')));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    if (!token.isEmpty())
        q.addQueryItem(QStringLiteral("offset"), QString::number(requestedOffset));
    url.setQuery(q);

    sendJsonRequest(m_reply, url, requestId, [this, requestId, requestedOffset](const QJsonObject &root) {
        SearchPage page;
        bool foundSection = false;
        qint64 candidateNextOffset = -1;

        for (const QString &type : m_types) {
            const QString key = sectionKeyForType(type);
            if (!root.contains(key))
                continue;
            if (!root.value(key).isObject())
                continue;
            foundSection = true;
            const QJsonObject section = root.value(key).toObject();
            const QJsonArray items = section.value(QStringLiteral("items")).toArray();
            for (const auto &itemVal : items) {
                if (!itemVal.isObject())
                    continue;
                const SearchResult parsed = parseItem(itemVal.toObject(), type);
                if (!parsed.id.isEmpty())
                    page.results.append(parsed);
            }
            const int offset = section.value(QStringLiteral("offset")).toInt(-1);
            const int limit = section.value(QStringLiteral("limit")).toInt(0);
            const int total = section.value(QStringLiteral("total")).toInt(0);
            const qint64 nextOffset = qint64(offset) + qint64(limit);
            if (offset == requestedOffset && limit > 0 && limit <= 50 && nextOffset < total && nextOffset <= 990 && !items.isEmpty()) {
                if (candidateNextOffset < 0 || nextOffset < candidateNextOffset)
                    candidateNextOffset = nextOffset;
            }
        }

        if (!foundSection) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Malformed Spotify search response: no catalogue sections"));
            return;
        }

        if (candidateNextOffset > requestedOffset && !page.results.isEmpty())
            page.nextToken = QString::number(candidateNextOffset);

        if (requestId)
            emit pageCompleted(requestId, page);
        else
            emit searchCompleted(id(), page.results);
    });
}

void ProviderSpotify::requestDetails(const QString &resultId, const QString &token, quint64 requestId)
{
    static const QRegularExpression detailIdPattern(
        QStringLiteral("^(artist|album|track|playlist|show|episode|audiobook):([A-Za-z0-9]{1,64})$"));
    const auto match = detailIdPattern.match(resultId);
    if (!token.isEmpty() || !match.hasMatch()) {
        ProviderRequest::fail(this, requestId, QStringLiteral("Invalid Spotify result identity or detail token"));
        return;
    }

    const QString itemType = match.captured(1);
    const QString spotifyId = match.captured(2);
    QUrl url(QStringLiteral("https://api.spotify.com/v1/%1/%2").arg(sectionKeyForType(itemType), spotifyId));

    sendJsonRequest(m_detailReply, url, requestId, [this, resultId, itemType, requestId](const QJsonObject &root) {
        const SearchResult result = parseItem(root, itemType);
        if (result.id != resultId) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Malformed Spotify detail response: result identity mismatch"));
            return;
        }
        emit detailsCompleted(requestId, SearchDetails{result, QString(), parseMetadata(root, itemType)});
    });
}

ResultMetadata ProviderSpotify::parseMetadata(const QJsonObject &obj, const QString &itemType) const
{
    ResultMetadata metadata;

    MetadataNode overview{QStringLiteral("Overview"), {}, {}};
    overview.children.append(Metadata::text(QStringLiteral("Catalogue type"), {itemType}));
    overview.children.append(Metadata::text(QStringLiteral("Spotify ID"), {obj.value(QStringLiteral("id")).toString()}));
    const QString uri = obj.value(QStringLiteral("uri")).toString().trimmed();
    if (!uri.isEmpty())
        overview.children.append(Metadata::text(QStringLiteral("Spotify URI"), {uri}));
    const QString releaseDate = obj.value(QStringLiteral("release_date")).toString().trimmed().isEmpty()
                                    ? obj.value(QStringLiteral("album")).toObject().value(QStringLiteral("release_date")).toString().trimmed()
                                    : obj.value(QStringLiteral("release_date")).toString().trimmed();
    if (!releaseDate.isEmpty())
        overview.children.append(Metadata::text(QStringLiteral("Release date"), {releaseDate}));
    const QString duration = formatDurationMs(obj.value(QStringLiteral("duration_ms")).toInteger());
    if (!duration.isEmpty())
        overview.children.append(Metadata::text(QStringLiteral("Duration"), {duration}));
    if (obj.value(QStringLiteral("popularity")).isDouble())
        overview.children.append(Metadata::text(QStringLiteral("Popularity"), {QString::number(obj.value(QStringLiteral("popularity")).toInt())}));
    if (obj.value(QStringLiteral("explicit")).isBool())
        overview.children.append(
            Metadata::text(QStringLiteral("Explicit"), {obj.value(QStringLiteral("explicit")).toBool() ? QStringLiteral("Yes") : QStringLiteral("No")}));
    const int totalTracks = obj.value(QStringLiteral("total_tracks")).toInt();
    if (totalTracks > 0)
        overview.children.append(Metadata::text(QStringLiteral("Total tracks"), {QString::number(totalTracks)}));
    const QString label = obj.value(QStringLiteral("label")).toString().trimmed();
    if (!label.isEmpty())
        overview.children.append(Metadata::text(QStringLiteral("Label"), {label}));
    const QJsonObject followers = obj.value(QStringLiteral("followers")).toObject();
    if (followers.value(QStringLiteral("total")).isDouble())
        overview.children.append(
            Metadata::text(QStringLiteral("Followers"), {QString::number(followers.value(QStringLiteral("total")).toInt())}));
    metadata.sections.append(overview);

    MetadataNode associations{QStringLiteral("Associations"), {}, {}};
    const QJsonArray artists = obj.value(QStringLiteral("artists")).toArray();
    for (const auto &artistVal : artists) {
        const QJsonObject artist = artistVal.toObject();
        const QString artistName = artist.value(QStringLiteral("name")).toString().trimmed();
        const QString artistId = artist.value(QStringLiteral("id")).toString().trimmed();
        if (artistName.isEmpty())
            continue;
        MetadataNode artistNode{artistName, {}, {}};
        if (isValidSpotifyId(artistId)) {
            const QUrl artistUrl = Metadata::safeWebUrl(canonicalSpotifyUrl(artist, QStringLiteral("artist"), artistId));
            if (!artistUrl.isEmpty())
                artistNode.values.append({artistName, artistUrl, MetadataValue::Kind::Link});
        }
        if (artistNode.values.isEmpty())
            artistNode.values.append({artistName, {}, MetadataValue::Kind::Text});
        associations.children.append(artistNode);
    }
    const QJsonObject album = obj.value(QStringLiteral("album")).toObject();
    const QString albumName = album.value(QStringLiteral("name")).toString().trimmed();
    if (!albumName.isEmpty()) {
        const QString albumId = album.value(QStringLiteral("id")).toString().trimmed();
        const QUrl albumUrl = isValidSpotifyId(albumId)
                                  ? Metadata::safeWebUrl(canonicalSpotifyUrl(album, QStringLiteral("album"), albumId))
                                  : QUrl();
        MetadataNode albumNode{QStringLiteral("Album"), {}, {}};
        albumNode.values.append({albumName, albumUrl, albumUrl.isEmpty() ? MetadataValue::Kind::Text : MetadataValue::Kind::Link});
        associations.children.append(albumNode);
    }
    QStringList genres;
    for (const auto &g : obj.value(QStringLiteral("genres")).toArray()) {
        const QString genre = g.toString().trimmed();
        if (!genre.isEmpty() && genres.size() < 32)
            genres.append(genre);
    }
    if (!genres.isEmpty())
        associations.children.append(Metadata::text(QStringLiteral("Genres"), genres));

    const QJsonArray trackItems = obj.value(QStringLiteral("tracks")).toObject().value(QStringLiteral("items")).toArray();
    if (!trackItems.isEmpty()) {
        MetadataNode tracksNode{QStringLiteral("Tracks"), {}, {}};
        int count = 0;
        for (const auto &tVal : trackItems) {
            if (++count > 50)
                break;
            const QJsonObject trackObj = tVal.toObject().contains(QStringLiteral("track"))
                                             ? tVal.toObject().value(QStringLiteral("track")).toObject()
                                             : tVal.toObject();
            const QString trackName = trackObj.value(QStringLiteral("name")).toString().trimmed();
            if (!trackName.isEmpty()) {
                const int trackNum = trackObj.value(QStringLiteral("track_number")).toInt(count);
                tracksNode.children.append(Metadata::text(QStringLiteral("Track %1").arg(trackNum), {trackName}));
            }
        }
        if (!tracksNode.children.isEmpty())
            associations.children.append(tracksNode);
    }
    if (!associations.children.isEmpty())
        metadata.sections.append(associations);

    MetadataNode mediaAndLinks{QStringLiteral("Artwork and links"), {}, {}};
    const QUrl canonical = Metadata::safeWebUrl(canonicalSpotifyUrl(obj, itemType, obj.value(QStringLiteral("id")).toString()));
    if (!canonical.isEmpty())
        mediaAndLinks.values.append({QStringLiteral("Open on Spotify"), canonical, MetadataValue::Kind::Link});

    const QJsonArray images = obj.value(QStringLiteral("images")).toArray().isEmpty()
                                  ? obj.value(QStringLiteral("album")).toObject().value(QStringLiteral("images")).toArray()
                                  : obj.value(QStringLiteral("images")).toArray();
    int imageCount = 0;
    for (const auto &imgVal : images) {
        if (++imageCount > 6)
            break;
        const QJsonObject imgObj = imgVal.toObject();
        const QUrl imgUrl = Metadata::safeWebUrl(imgObj.value(QStringLiteral("url")).toString());
        if (!imgUrl.isEmpty()) {
            const int width = imgObj.value(QStringLiteral("width")).toInt();
            const int height = imgObj.value(QStringLiteral("height")).toInt();
            const QString labelText = (width > 0 && height > 0) ? QStringLiteral("Artwork (%1×%2)").arg(width).arg(height)
                                                                : QStringLiteral("Artwork");
            mediaAndLinks.values.append({labelText, imgUrl, MetadataValue::Kind::Image});
        }
    }
    if (!mediaAndLinks.values.isEmpty())
        metadata.sections.append(mediaAndLinks);

    metadata.attribution = {QStringLiteral("Data provided by Spotify Web API.")};
    return metadata;
}

SearchProvider *ProviderSpotify::clone(QObject *parent) const
{
    auto *copy = new ProviderSpotify(parent);
    copy->m_accessToken = m_accessToken;
    copy->m_types = m_types;
    return copy;
}
