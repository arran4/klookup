#include "provider_mangabaka.h"
#include "providerrequest.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

ProviderMangaBaka::ProviderMangaBaka(QObject *parent) : SearchProvider(parent), m_nam(new QNetworkAccessManager(this)), m_reply(nullptr) {}

ProviderMangaBaka::~ProviderMangaBaka()
{
    cancelRequests();
}

QString ProviderMangaBaka::name() const
{
    return QStringLiteral("MangaBaka");
}

QString ProviderMangaBaka::id() const
{
    return QStringLiteral("mangabaka");
}

void ProviderMangaBaka::search(const QString &query)
{
    requestPage(query, QString(), 0);
}

void ProviderMangaBaka::cancelRequests()
{
    ProviderRequest::cancel(m_reply, this);
    ProviderRequest::cancel(m_detailReply, this);
}

SearchResult ProviderMangaBaka::parseResult(const QJsonObject &obj) const
{
    SearchResult res;
    // API returns id as number but we store it as string.
    QJsonValue idVal = obj.value(QStringLiteral("id"));
    const int numericId = idVal.toInt(-1);
    if (!idVal.isDouble() || numericId <= 0 || idVal.toDouble() != numericId)
        return {}; // Stable IDs must be integral and within the API's int32 range.
    res.id = QString::number(numericId);

    QJsonArray titlesArray = obj.value(QStringLiteral("titles")).toArray();
    for (const QJsonValue &tVal : titlesArray) {
        QJsonObject tObj = tVal.toObject();
        if (tObj.value(QStringLiteral("language")).toString() == QStringLiteral("en")) {
            res.title = tObj.value(QStringLiteral("title")).toString();
            break;
        }
    }
    if (res.title.isEmpty() && !titlesArray.isEmpty()) {
        res.title = titlesArray.first().toObject().value(QStringLiteral("title")).toString();
    }
    if (res.title.isEmpty()) {
        res.title = obj.value(QStringLiteral("title")).toString();
    }

    res.description = obj.value(QStringLiteral("description")).toString();

    res.url = obj.value(QStringLiteral("canonical_url")).toString();
    res.type = obj.value(QStringLiteral("type")).toString();

    QVariantMap additional;
    if (obj.contains(QStringLiteral("status")) && obj.value(QStringLiteral("status")).isString()) {
        additional[QStringLiteral("status")] = obj.value(QStringLiteral("status")).toString();
    }
    if (obj.contains(QStringLiteral("year")) && obj.value(QStringLiteral("year")).isDouble()) {
        additional[QStringLiteral("year")] = obj.value(QStringLiteral("year")).toInt();
    }
    if (!additional.isEmpty()) {
        res.additionalData = additional;
    }

    return res;
}

void ProviderMangaBaka::requestPage(const QString &query, const QString &token, quint64 requestId)
{
    bool ok = false;
    const int position = token.toInt(&ok);
    if (!token.isEmpty() && (!ok || position < 1 || position > 1200)) {
        ProviderRequest::fail(this, requestId, QStringLiteral("Invalid continuation token"));
        return;
    }
    QUrl url(QStringLiteral("https://api.mangabaka.org/v2/series/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    if (!token.isEmpty())
        q.addQueryItem(QStringLiteral("page"), token);
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
        const auto pagination = root.value(QStringLiteral("pagination")).toObject();
        const QUrl next(pagination.value(QStringLiteral("next")).toString(), QUrl::StrictMode);
        const auto nextQuery = QUrlQuery(next);
        bool ok = false;
        const int pageNumber = nextQuery.queryItemValue(QStringLiteral("page")).toInt(&ok);
        const int current = token.isEmpty() ? 1 : token.toInt();
        if (next.isValid() && next.scheme() == QStringLiteral("https") && next.host() == QStringLiteral("api.mangabaka.org") &&
            next.path() == QStringLiteral("/v2/series/search") && next.userInfo().isEmpty() && ok && pageNumber > current &&
            pageNumber <= 1200) {
            page.nextToken = QString::number(pageNumber);
        }

        if (requestId)
            emit pageCompleted(requestId, page);
        else
            emit searchCompleted(id(), page.results);
    });
}

void ProviderMangaBaka::requestDetails(const QString &resultId, const QString &token, quint64 requestId)
{
    if (!token.isEmpty() || !QRegularExpression(QStringLiteral("^[1-9][0-9]{0,9}$")).match(resultId).hasMatch()) {
        ProviderRequest::fail(this, requestId, QStringLiteral("Invalid result identity or detail token"));
        return;
    }
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(QStringLiteral("api.mangabaka.org"));
    url.setPath(QStringLiteral("/v2/series/") + resultId);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("schema"), QStringLiteral("full"));
    url.setQuery(q);
    ProviderRequest::getJson(this, m_nam, m_detailReply, url, requestId, [this, resultId, requestId](const QJsonObject &root) {
        const auto result = parseResult(root.value(QStringLiteral("data")).toObject());
        if (result.id != resultId) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Malformed detail response: result identity mismatch"));
            return;
        }
        emit detailsCompleted(requestId, SearchDetails{result, QString()});
    });
}

SearchProvider *ProviderMangaBaka::clone(QObject *parent) const
{
    return new ProviderMangaBaka(parent);
}
