#include "provider_myanimelist.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderMyAnimeList::ProviderMyAnimeList(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderMyAnimeList::~ProviderMyAnimeList()
{
}

QString ProviderMyAnimeList::name() const
{
    return QStringLiteral("MyAnimeList (via Jikan)");
}

QString ProviderMyAnimeList::id() const
{
    return QStringLiteral("myanimelist");
}

void ProviderMyAnimeList::search(const QString &query)
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

    // We use Jikan (the unofficial MAL API) since MAL's official API requires OAuth tokens
    QUrl url(QStringLiteral("https://api.jikan.moe/v4/anime"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
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

void ProviderMyAnimeList::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(id(), reply->errorString());
        m_reply = nullptr;
        return;
    }

    QByteArray data = reply->readAll();
    m_reply = nullptr;

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
        res.id = QString::number(obj.value(QStringLiteral("mal_id")).toInt());
        res.title = obj.value(QStringLiteral("title")).toString();

        QString englishTitle = obj.value(QStringLiteral("title_english")).toString();
        if (!englishTitle.isEmpty()) {
            res.title = englishTitle;
        }

        res.description = obj.value(QStringLiteral("synopsis")).toString();
        if (res.description.length() > 200) {
            res.description = res.description.left(200) + QStringLiteral("...");
        }

        res.url = obj.value(QStringLiteral("url")).toString();
        res.type = QStringLiteral("anime");

        QVariantMap additional;
        additional[QStringLiteral("episodes")] = obj.value(QStringLiteral("episodes")).toInt();
        additional[QStringLiteral("score")] = obj.value(QStringLiteral("score")).toDouble();
        additional[QStringLiteral("status")] = obj.value(QStringLiteral("status")).toString();
        res.additionalData = additional;

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderMyAnimeList::clone(QObject *parent) const
{
    return new ProviderMyAnimeList(parent);
}
