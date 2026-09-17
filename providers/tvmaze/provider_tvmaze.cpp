#include "provider_tvmaze.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderTvMaze::ProviderTvMaze(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderTvMaze::~ProviderTvMaze()
{
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
        QPointer<QNetworkReply> oldReply = m_reply;
        m_reply = nullptr;
        oldReply->disconnect(this);
        if (oldReply->isRunning()) {
            oldReply->abort();
        }
        oldReply->deleteLater();
    }

    QUrl url(QStringLiteral("https://api.tvmaze.com/search/shows"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup-App"));

    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::finished, this, [this, reply = QPointer<QNetworkReply>(m_reply)]() {
        if (!reply || reply != m_reply) {
            return;
        }
        this->onReplyFinished(reply);
    });
}

void ProviderTvMaze::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();



    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(id(), reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();

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
