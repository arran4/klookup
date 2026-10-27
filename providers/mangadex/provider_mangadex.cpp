#include "provider_mangadex.h"
#include "../manga_metadata.h"
#include "providerrequest.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

ProviderMangaDex::ProviderMangaDex(QObject *parent) : SearchProvider(parent), m_nam(new QNetworkAccessManager(this)), m_reply(nullptr) {}

ProviderMangaDex::~ProviderMangaDex()
{
    cancelRequests();
}

QString ProviderMangaDex::name() const
{
    return QStringLiteral("MangaDex");
}

QString ProviderMangaDex::id() const
{
    return QStringLiteral("mangadex");
}

void ProviderMangaDex::search(const QString &query)
{
    requestPage(query, QString(), 0);
}

void ProviderMangaDex::cancelRequests()
{
    ProviderRequest::cancel(m_reply, this);
    ProviderRequest::cancel(m_detailReply, this);
}

SearchResult ProviderMangaDex::parseResult(const QJsonObject &obj) const
{
    SearchResult res;
    QJsonValue idVal = obj.value(QStringLiteral("id"));
    if (!idVal.isString()) {
        return {}; // Missing stable identity string
    }
    res.id = idVal.toString();
    if (res.id.isEmpty())
        return {};

    QJsonObject attributes = obj.value(QStringLiteral("attributes")).toObject();
    QJsonObject titleObj = attributes.value(QStringLiteral("title")).toObject();

    res.title = titleObj.value(QStringLiteral("en")).toString();
    if (res.title.isEmpty() && !titleObj.keys().isEmpty()) {
        res.title = titleObj.value(titleObj.keys().first()).toString();
    }

    QJsonObject descObj = attributes.value(QStringLiteral("description")).toObject();
    res.description = descObj.value(QStringLiteral("en")).toString();
    if (res.description.isEmpty() && !descObj.keys().isEmpty()) {
        res.description = descObj.value(descObj.keys().first()).toString();
    }

    res.url = QStringLiteral("https://mangadex.org/title/") + res.id;
    res.type = QStringLiteral("manga");

    QVariantMap additional;
    if (attributes.contains(QStringLiteral("status")) && attributes.value(QStringLiteral("status")).isString()) {
        additional[QStringLiteral("status")] = attributes.value(QStringLiteral("status")).toString();
    }
    if (attributes.contains(QStringLiteral("year")) && attributes.value(QStringLiteral("year")).isDouble()) {
        additional[QStringLiteral("year")] = attributes.value(QStringLiteral("year")).toInt();
    }
    if (!additional.isEmpty()) {
        res.additionalData = additional;
    }

    return res;
}

void ProviderMangaDex::requestPage(const QString &query, const QString &token, quint64 requestId)
{
    bool ok = false;
    const int position = token.toInt(&ok);
    if (!token.isEmpty() && (!ok || position < 0 || position > 9999)) {
        ProviderRequest::fail(this, requestId, QStringLiteral("Invalid continuation token"));
        return;
    }
    QUrl url(QStringLiteral("https://api.mangadex.org/manga"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"), query);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    if (!token.isEmpty())
        q.addQueryItem(QStringLiteral("offset"), token);
    url.setQuery(q);
    ProviderRequest::getJson(this, m_nam, m_reply, url, requestId, [this, requestId, token](const QJsonObject &root) {
        if (!root.value(QStringLiteral("data")).isArray()) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Malformed response: missing 'data' array"));
            return;
        }
        SearchPage page;
        const auto values = root.value(QStringLiteral("data")).toArray();
        for (const auto &value : values) {
            const auto result = parseResult(value.toObject());
            if (!result.id.isEmpty())
                page.results.append(result);
        }
        const int offset = root.value(QStringLiteral("offset")).toInt();
        const int limit = root.value(QStringLiteral("limit")).toInt();
        const int total = root.value(QStringLiteral("total")).toInt();
        const qint64 nextOffset = qint64(offset) + qint64(limit);
        const int requestedOffset = token.isEmpty() ? 0 : token.toInt();
        if (offset == requestedOffset && limit > 0 && limit <= 100 && nextOffset < total && nextOffset < 10000 && !values.isEmpty())
            page.nextToken = QString::number(nextOffset);

        if (requestId)
            emit pageCompleted(requestId, page);
        else
            emit searchCompleted(id(), page.results);
    });
}

void ProviderMangaDex::requestDetails(const QString &resultId, const QString &token, quint64 requestId)
{
    if (!token.isEmpty() || !QRegularExpression(QStringLiteral("^[a-zA-Z0-9-]{1,64}$")).match(resultId).hasMatch()) {
        ProviderRequest::fail(this, requestId, QStringLiteral("Invalid result identity or detail token"));
        return;
    }
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(QStringLiteral("api.mangadex.org"));
    url.setPath(QStringLiteral("/manga/") + resultId);
    QUrlQuery q;
    for (const auto &include : {QStringLiteral("author"), QStringLiteral("artist"), QStringLiteral("cover_art")})
        q.addQueryItem(QStringLiteral("includes[]"), include);
    url.setQuery(q);
    ProviderRequest::getJson(this, m_nam, m_detailReply, url, requestId, [this, resultId, requestId](const QJsonObject &root) {
        const auto result = parseResult(root.value(QStringLiteral("data")).toObject());
        if (result.id != resultId) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Malformed detail response: result identity mismatch"));
            return;
        }
        emit detailsCompleted(requestId, SearchDetails{result, QString(), parseMetadata(root.value(QStringLiteral("data")).toObject())});
    });
}

SearchProvider *ProviderMangaDex::clone(QObject *parent) const
{
    return new ProviderMangaDex(parent);
}

ResultMetadata ProviderMangaDex::parseMetadata(const QJsonObject &obj) const
{
    using namespace MangaMetadata;
    ResultMetadata metadata;
    const auto attributes = obj.value(QStringLiteral("attributes")).toObject();
    MetadataNode series{QStringLiteral("Series"), {}, {}};
    const QList<QPair<QString, QString>> fields = {
        {QStringLiteral("status"), QStringLiteral("Publication status")},
        {QStringLiteral("year"), QStringLiteral("Year")},
        {QStringLiteral("originalLanguage"), QStringLiteral("Original language")},
        {QStringLiteral("publicationDemographic"), QStringLiteral("Demographic")},
        {QStringLiteral("contentRating"), QStringLiteral("Content rating")},
        {QStringLiteral("lastVolume"), QStringLiteral("Last volume")},
        {QStringLiteral("lastChapter"), QStringLiteral("Last chapter")},
        {QStringLiteral("availableTranslatedLanguages"), QStringLiteral("Translation languages")},
        {QStringLiteral("createdAt"), QStringLiteral("Created")},
        {QStringLiteral("updatedAt"), QStringLiteral("Updated")}};
    for (const auto &entry : fields)
        field(series, entry.second, attributes.value(entry.first));
    metadata.sections.append(series);
    MetadataNode titles{QStringLiteral("Titles"), {}, {}};
    localized(titles, attributes.value(QStringLiteral("title")).toObject());
    for (const auto &value : attributes.value(QStringLiteral("altTitles")).toArray())
        localized(titles, value.toObject());
    metadata.sections.append(titles);
    MetadataNode descriptions{QStringLiteral("Localized synopses"), {}, {}};
    localized(descriptions, attributes.value(QStringLiteral("description")).toObject());
    metadata.sections.append(descriptions);
    MetadataNode tags{QStringLiteral("Tags"), {}, {}};
    for (const auto &value : attributes.value(QStringLiteral("tags")).toArray()) {
        const auto tag = value.toObject();
        const auto data = tag.value(QStringLiteral("attributes")).toObject();
        auto &group = category(tags, data.value(QStringLiteral("group")).toString(QStringLiteral("Other")));
        const auto names = data.value(QStringLiteral("name")).toObject();
        QString label = names.value(QStringLiteral("en")).toString();
        if (label.isEmpty() && !names.isEmpty())
            label = names.begin().value().toString();
        if (label.isEmpty())
            continue;
        MetadataNode node{label, {}, {}};
        field(node, QStringLiteral("Names"), names);
        field(node, QStringLiteral("Description"), data.value(QStringLiteral("description")));
        field(node, QStringLiteral("Tag ID"), tag.value(QStringLiteral("id")));
        group.children.append(node);
    }
    metadata.sections.append(tags);
    MetadataNode creators{QStringLiteral("Creators and related records"), {}, {}};
    MetadataNode references{QStringLiteral("Sources and links"), {}, {}};
    for (const auto &value : obj.value(QStringLiteral("relationships")).toArray()) {
        const auto relation = value.toObject();
        const QString type = relation.value(QStringLiteral("type")).toString();
        const auto data = relation.value(QStringLiteral("attributes")).toObject();
        const QString name = data.value(QStringLiteral("name")).toString(relation.value(QStringLiteral("id")).toString());
        if (type == QStringLiteral("author") || type == QStringLiteral("artist"))
            field(creators, type, name);
        else if (type == QStringLiteral("cover_art")) {
            const QString fileName = data.value(QStringLiteral("fileName")).toString();
            if (QRegularExpression(QStringLiteral("^[a-zA-Z0-9-]+\\.(jpg|png|webp)$")).match(fileName).hasMatch())
                link(references, QStringLiteral("Cover image"),
                     QStringLiteral("https://uploads.mangadex.org/covers/%1/%2.256.jpg")
                         .arg(obj.value(QStringLiteral("id")).toString(), fileName),
                     MetadataValue::Kind::Image);
        } else
            field(creators, type, relation);
    }
    metadata.sections.append(creators);
    const QMap<QString, QString> knownLinks = {{QStringLiteral("al"), QStringLiteral("https://anilist.co/manga/")},
                                               {QStringLiteral("ap"), QStringLiteral("https://www.anime-planet.com/manga/")},
                                               {QStringLiteral("kt"), QStringLiteral("https://kitsu.io/manga/")},
                                               {QStringLiteral("mal"), QStringLiteral("https://myanimelist.net/manga/")},
                                               {QStringLiteral("mu"), QStringLiteral("https://www.mangaupdates.com/series/")}};
    for (const auto &key : {QStringLiteral("links"), QStringLiteral("officialLinks")}) {
        const auto links = attributes.value(key).toObject();
        for (auto it = links.begin(); it != links.end(); ++it) {
            const QString value = it.value().toString();
            if (knownLinks.contains(it.key()) && !value.isEmpty())
                link(references, it.key(), knownLinks.value(it.key()) + QString::fromLatin1(QUrl::toPercentEncoding(value)));
            else if (!Metadata::safeWebUrl(value).isEmpty())
                link(references, it.key(), value);
            else
                field(references, it.key(), it.value());
        }
    }
    metadata.sections.append(references);
    metadata.attribution = {QStringLiteral("Data provided by MangaDex; source references belong to their respective providers.")};
    return metadata;
}
