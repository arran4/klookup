#include "provider_tvmaze.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderTvMaze::ProviderTvMaze(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager())
    , m_reply(nullptr)
{
}

ProviderTvMaze::~ProviderTvMaze()
{
    m_nam->deleteLater();
}

QString ProviderTvMaze::name() const
{
    return QStringLiteral("TVMaze");
}

QString ProviderTvMaze::id() const
{
    return QStringLiteral("tvmaze");
}

void ProviderTvMaze::search(const QString &query)
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QUrl url(QStringLiteral("https://api.tvmaze.com/search/shows"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup-App"));

    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::finished, this, &ProviderTvMaze::onReplyFinished);
}

void ProviderTvMaze::onReplyFinished()
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

    QJsonArray resultsArray = doc.array();

    QList<SearchResult> results;
    for (const QJsonValue &val : resultsArray) {
        QJsonObject obj = val.toObject();
        QJsonObject show = obj.value(QStringLiteral("show")).toObject();

        SearchResult res;
        res.id = QString::number(show.value(QStringLiteral("id")).toInt());
        res.title = show.value(QStringLiteral("name")).toString();

        res.description = show.value(QStringLiteral("summary")).toString();
        res.description.remove(QRegularExpression(QStringLiteral("<[^>]*>"))); // Strip HTML
        if (res.description.length() > 200) {
            res.description = res.description.left(200) + QStringLiteral("...");
        }

        res.url = show.value(QStringLiteral("url")).toString();
        res.type = QStringLiteral("tvshow");

        QVariantMap additional;
        additional[QStringLiteral("language")] = show.value(QStringLiteral("language")).toString();
        additional[QStringLiteral("premiered")] = show.value(QStringLiteral("premiered")).toString();
        additional[QStringLiteral("status")] = show.value(QStringLiteral("status")).toString();
        QJsonObject rating = show.value(QStringLiteral("rating")).toObject();
        if (rating.contains(QStringLiteral("average")) && !rating.value(QStringLiteral("average")).isNull()) {
            additional[QStringLiteral("rating")] = rating.value(QStringLiteral("average")).toDouble();
        }

        res.additionalData = additional;

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderTvMaze::clone(QObject *parent) const
{
    return new ProviderTvMaze(parent);
}
