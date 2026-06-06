#include "provider_wikipedia.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderWikipedia::ProviderWikipedia(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderWikipedia::~ProviderWikipedia()
{
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
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QUrl url(QStringLiteral("https://en.wikipedia.org/w/api.php"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    q.addQueryItem(QStringLiteral("list"), QStringLiteral("search"));
    q.addQueryItem(QStringLiteral("srsearch"), query);
    q.addQueryItem(QStringLiteral("utf8"), QStringLiteral(""));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);

    QNetworkRequest request(url);
    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::finished, this, &ProviderWikipedia::onReplyFinished);
}

void ProviderWikipedia::onReplyFinished()
{
    m_reply->deleteLater();

    if (m_reply->error() != QNetworkReply::NoError) {
        emit searchFailed(id(), m_reply->errorString());
        m_reply = nullptr;
        return;
    }

    QByteArray data = m_reply->readAll();
    m_reply = nullptr;

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError) {
        emit searchFailed(id(), QStringLiteral("Failed to parse JSON response"));
        return;
    }

    QJsonObject root = doc.object();
    QJsonObject query = root.value(QStringLiteral("query")).toObject();
    QJsonArray searchArray = query.value(QStringLiteral("search")).toArray();

    QList<SearchResult> results;
    for (const QJsonValue &val : searchArray) {
        QJsonObject obj = val.toObject();
        SearchResult res;
        res.id = QString::number(obj.value(QStringLiteral("pageid")).toInt());
        res.title = obj.value(QStringLiteral("title")).toString();
        res.description = obj.value(QStringLiteral("snippet")).toString();
        res.url = QStringLiteral("https://en.wikipedia.org/?curid=") + res.id;
        res.type = QStringLiteral("article");
        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderWikipedia::clone(QObject *parent) const
{
    return new ProviderWikipedia(parent);
}
