#include "provider_themoviedb.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderTheMovieDB::ProviderTheMovieDB(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderTheMovieDB::~ProviderTheMovieDB()
{
}

QString ProviderTheMovieDB::name() const
{
    return QStringLiteral("The Movie Database (TMDb)");
}

QString ProviderTheMovieDB::id() const
{
    return QStringLiteral("themoviedb");
}

void ProviderTheMovieDB::setCredentials(const QString &/*username*/, const QString &password)
{
    m_apiKey = password;
}

void ProviderTheMovieDB::search(const QString &query)
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    if (m_apiKey.isEmpty()) {
        emit searchFailed(id(), QStringLiteral("API Key is missing. Please configure it in Settings."));
        return;
    }

    QUrl url(QStringLiteral("https://api.themoviedb.org/3/search/multi"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("api_key"), m_apiKey);
    q.addQueryItem(QStringLiteral("query"), query);
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup-App"));

    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::finished, this, &ProviderTheMovieDB::onReplyFinished);
}

void ProviderTheMovieDB::onReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }

    reply->deleteLater();

    if (reply != m_reply) {
        return; // Ignore responses from aborted/replaced requests
    }

    m_reply = nullptr;

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

    QJsonObject root = doc.object();
    QJsonArray resultsArray = root.value(QStringLiteral("results")).toArray();

    QList<SearchResult> results;
    for (const QJsonValue &val : resultsArray) {
        QJsonObject obj = val.toObject();

        SearchResult res;
        res.id = QString::number(obj.value(QStringLiteral("id")).toInt());

        QString mediaType = obj.value(QStringLiteral("media_type")).toString();
        if (mediaType == QStringLiteral("movie")) {
            res.title = obj.value(QStringLiteral("title")).toString();
            res.type = QStringLiteral("movie");
            res.url = QStringLiteral("https://www.themoviedb.org/movie/") + res.id;
        } else if (mediaType == QStringLiteral("tv")) {
            res.title = obj.value(QStringLiteral("name")).toString();
            res.type = QStringLiteral("tvshow");
            res.url = QStringLiteral("https://www.themoviedb.org/tv/") + res.id;
        } else {
            continue; // Skip persons or others for now
        }

        res.description = obj.value(QStringLiteral("overview")).toString();

        QVariantMap additional;
        additional[QStringLiteral("popularity")] = obj.value(QStringLiteral("popularity")).toDouble();
        if (obj.contains(QStringLiteral("release_date"))) {
            additional[QStringLiteral("release_date")] = obj.value(QStringLiteral("release_date")).toString();
        } else if (obj.contains(QStringLiteral("first_air_date"))) {
            additional[QStringLiteral("first_air_date")] = obj.value(QStringLiteral("first_air_date")).toString();
        }
        res.additionalData = additional;

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderTheMovieDB::clone(QObject *parent) const
{
    ProviderTheMovieDB *cloned = new ProviderTheMovieDB(parent);
    cloned->m_apiKey = m_apiKey;
    return cloned;
}
