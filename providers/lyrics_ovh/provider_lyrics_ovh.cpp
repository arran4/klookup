#include "provider_lyrics_ovh.h"
#include <QUrl>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderLyricsOvh::ProviderLyricsOvh(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderLyricsOvh::~ProviderLyricsOvh()
{
}

QString ProviderLyricsOvh::name() const
{
    return QStringLiteral("Lyrics (OVH)");
}

QString ProviderLyricsOvh::id() const
{
    return QStringLiteral("lyrics_ovh");
}

void ProviderLyricsOvh::search(const QString &query)
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QUrl url(QStringLiteral("https://api.lyrics.ovh/suggest/") + QUrl::toPercentEncoding(query));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup-App"));

    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::finished, this, &ProviderLyricsOvh::onReplyFinished);
}

void ProviderLyricsOvh::onReplyFinished()
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
    QJsonArray resultsArray = root.value(QStringLiteral("data")).toArray();

    QList<SearchResult> results;
    for (const QJsonValue &val : resultsArray) {
        QJsonObject obj = val.toObject();

        SearchResult res;
        res.id = QString::number(obj.value(QStringLiteral("id")).toInt());

        QJsonObject artistObj = obj.value(QStringLiteral("artist")).toObject();
        QString artistName = artistObj.value(QStringLiteral("name")).toString();

        res.title = obj.value(QStringLiteral("title")).toString();
        res.description = QStringLiteral("Song by ") + artistName;
        res.url = obj.value(QStringLiteral("link")).toString();
        res.type = QStringLiteral("song");

        QVariantMap additional;
        additional[QStringLiteral("artist")] = artistName;
        QJsonObject albumObj = obj.value(QStringLiteral("album")).toObject();
        additional[QStringLiteral("album")] = albumObj.value(QStringLiteral("title")).toString();
        res.additionalData = additional;

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderLyricsOvh::clone(QObject *parent) const
{
    return new ProviderLyricsOvh(parent);
}
