#include "provider_wikipedia.h"
#include "providerrequest.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrlQuery>
#include <cmath>

namespace
{
QString pageId(const QJsonValue &value)
{
    const double number = value.toDouble(-1);
    return value.isDouble() && number > 0 && number <= 9007199254740991.0 && std::floor(number) == number ? QString::number(qint64(number))
                                                                                                          : QString();
}
QString sourceUrl(const QString &id)
{
    return QStringLiteral("https://en.wikipedia.org/?curid=") + id;
}
QUrl endpoint(QUrlQuery query)
{
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("formatversion"), QStringLiteral("2"));
    QUrl url(QStringLiteral("https://en.wikipedia.org/w/api.php"));
    url.setQuery(query);
    return url;
}
} // namespace
ProviderWikipedia::ProviderWikipedia(QObject *parent) : SearchProvider(parent), m_nam(new QNetworkAccessManager(this)) {}
ProviderWikipedia::~ProviderWikipedia()
{
    cancelRequests();
}
QString ProviderWikipedia::name() const
{
    return QStringLiteral("Wikipedia");
}
QString ProviderWikipedia::id() const
{
    return QStringLiteral("wikipedia");
}
void ProviderWikipedia::search(const QString &query)
{
    requestPage(query, {}, 0);
}
void ProviderWikipedia::cancelRequests()
{
    ProviderRequest::cancel(m_reply, this);
    ProviderRequest::cancel(m_detailReply, this);
}
void ProviderWikipedia::requestPage(const QString &query, const QString &token, quint64 requestId)
{
    bool ok = false;
    const int offset = token.toInt(&ok);
    if (!token.isEmpty() && (!ok || offset < 1 || offset > 10000)) {
        ProviderRequest::fail(this, requestId, QStringLiteral("Invalid continuation token"));
        return;
    }
    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    parameters.addQueryItem(QStringLiteral("list"), QStringLiteral("search"));
    parameters.addQueryItem(QStringLiteral("srsearch"), query);
    parameters.addQueryItem(QStringLiteral("srprop"), QStringLiteral("snippet"));
    parameters.addQueryItem(QStringLiteral("srlimit"), QStringLiteral("10"));
    if (!token.isEmpty())
        parameters.addQueryItem(QStringLiteral("sroffset"), token);
    ProviderRequest::getJson(this, m_nam, m_reply, endpoint(parameters), requestId, [this, requestId, offset](const QJsonObject &root) {
        if (!root.value(QStringLiteral("query")).toObject().value(QStringLiteral("search")).isArray()) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Wikipedia search is unavailable or malformed"));
            return;
        }
        SearchPage page;
        for (const auto &value : root.value(QStringLiteral("query")).toObject().value(QStringLiteral("search")).toArray()) {
            const auto object = value.toObject();
            SearchResult result;
            result.id = pageId(object.value(QStringLiteral("pageid")));
            result.title = object.value(QStringLiteral("title")).toString();
            if (result.id.isEmpty() || result.title.isEmpty())
                continue;
            result.url = sourceUrl(result.id);
            result.type = QStringLiteral("article");
            const ResultContent content{object.value(QStringLiteral("snippet")).toString(), ResultContent::Format::LimitedHtml,
                                        QUrl(result.url)};
            result.description = Content::render(content).plainText;
            page.results.append(result);
            page.content.insert(result.id, content);
        }
        const auto next = root.value(QStringLiteral("continue")).toObject().value(QStringLiteral("sroffset"));
        const int nextOffset = next.toInt(-1);
        if (next.isDouble() && next.toDouble() == nextOffset && nextOffset > offset && nextOffset <= 10000)
            page.nextToken = QString::number(nextOffset);
        if (requestId)
            emit pageCompleted(requestId, page);
        else
            emit searchCompleted(id(), page.results);
    });
}
void ProviderWikipedia::requestDetails(const QString &resultId, const QString &token, quint64 requestId)
{
    if (!token.isEmpty() || !QRegularExpression(QStringLiteral("^[1-9][0-9]{0,14}$")).match(resultId).hasMatch()) {
        ProviderRequest::fail(this, requestId, QStringLiteral("Invalid Wikipedia result identity or detail token"));
        return;
    }
    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("action"), QStringLiteral("parse"));
    parameters.addQueryItem(QStringLiteral("pageid"), resultId);
    parameters.addQueryItem(QStringLiteral("prop"), QStringLiteral("text"));
    for (const auto &key : {QStringLiteral("disableeditsection"), QStringLiteral("disablelimitreport"), QStringLiteral("disabletoc")})
        parameters.addQueryItem(key, QStringLiteral("1"));
    ProviderRequest::getJson(
        this, m_nam, m_detailReply, endpoint(parameters), requestId, [this, resultId, requestId](const QJsonObject &root) {
            const auto article = root.value(QStringLiteral("parse")).toObject();
            if (pageId(article.value(QStringLiteral("pageid"))) != resultId || !article.value(QStringLiteral("text")).isString()) {
                ProviderRequest::fail(this, requestId, QStringLiteral("Wikipedia article is unavailable or malformed"));
                return;
            }
            SearchDetails details;
            details.result.id = resultId;
            details.result.title = article.value(QStringLiteral("title")).toString();
            details.result.type = QStringLiteral("article");
            details.result.url = sourceUrl(resultId);
            // Relative article links resolve against the wiki origin, not the API.
            QUrl articleBase(QStringLiteral("https://en.wikipedia.org"));
            articleBase.setPath(QStringLiteral("/wiki/") + details.result.title);
            details.content = {article.value(QStringLiteral("text")).toString(), ResultContent::Format::LimitedHtml, articleBase};
            const auto rendered = Content::render(details.content);
            details.result.description = rendered.plainText;
            if (rendered.plainText.trimmed().isEmpty()) {
                details.content = {
                    QStringLiteral("No article content is available. Open the source to read it."), ResultContent::Format::PlainText, {}};
                details.result.description = details.content.description;
            }
            details.metadata.attribution = {
                QStringLiteral("Wikipedia contributors — see article history and source for attribution and licensing.")};
            emit detailsCompleted(requestId, details);
        });
}
SearchProvider *ProviderWikipedia::clone(QObject *parent) const
{
    return new ProviderWikipedia(parent);
}
