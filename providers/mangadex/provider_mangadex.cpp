#include "provider_mangadex.h"
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
        if (offset >= 0 && limit > 0 && offset + limit < total && offset + limit < 10000 && !values.isEmpty())
            page.nextToken = QString::number(offset + limit);

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
    ProviderRequest::getJson(this, m_nam, m_detailReply, url, requestId, [this, resultId, requestId](const QJsonObject &root) {
        const auto result = parseResult(root.value(QStringLiteral("data")).toObject());
        if (result.id != resultId) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Malformed detail response: result identity mismatch"));
            return;
        }
        emit detailsCompleted(requestId, SearchDetails{result, QString()});
    });
}

SearchProvider *ProviderMangaDex::clone(QObject *parent) const
{
    return new ProviderMangaDex(parent);
}
