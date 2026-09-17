#include "provider_anilist.h"
#include <QUrl>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderAniList::ProviderAniList(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderAniList::~ProviderAniList()
{
}

QString ProviderAniList::name() const
{
    return QStringLiteral("AniList");
}

QString ProviderAniList::id() const
{
    return QStringLiteral("anilist");
}

void ProviderAniList::search(const QString &query)
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

    QUrl url(QStringLiteral("https://graphql.anilist.co"));

    QJsonObject requestBody;
    requestBody.insert(QStringLiteral("query"), QStringLiteral("query ($search: String) { Page { media(search: $search, type: ANIME) { id title { romaji english } description siteUrl status episodes averageScore } } }"));

    QJsonObject variables;
    variables.insert(QStringLiteral("search"), query);
    requestBody.insert(QStringLiteral("variables"), variables);

    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson();

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup-App"));

    m_reply = m_nam->post(request, data);

    connect(m_reply, &QNetworkReply::finished, this, [this, reply = QPointer<QNetworkReply>(m_reply)]() {
        if (!reply || reply != m_reply) {
            return;
        }
        this->onReplyFinished(reply);
    });
}

void ProviderAniList::onReplyFinished(QNetworkReply *reply)
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

    QJsonObject root = doc.object();
    QJsonObject dataObj = root.value(QStringLiteral("data")).toObject();
    QJsonObject pageObj = dataObj.value(QStringLiteral("Page")).toObject();
    QJsonArray mediaArray = pageObj.value(QStringLiteral("media")).toArray();

    QList<SearchResult> results;
    for (const QJsonValue &val : mediaArray) {
        QJsonObject obj = val.toObject();
        QJsonObject titleObj = obj.value(QStringLiteral("title")).toObject();

        SearchResult res;
        res.id = QString::number(obj.value(QStringLiteral("id")).toInt());

        QString englishTitle = titleObj.value(QStringLiteral("english")).toString();
        QString romajiTitle = titleObj.value(QStringLiteral("romaji")).toString();
        res.title = englishTitle.isEmpty() ? romajiTitle : englishTitle;

        res.description = obj.value(QStringLiteral("description")).toString();
        // Remove simple HTML tags often found in AniList descriptions
        res.description.remove(QRegularExpression(QStringLiteral("<[^>]*>")));

        if (res.description.length() > 200) {
            res.description = res.description.left(200) + QStringLiteral("...");
        }
        res.url = obj.value(QStringLiteral("siteUrl")).toString();
        res.type = QStringLiteral("anime");

        QVariantMap additional;
        additional[QStringLiteral("status")] = obj.value(QStringLiteral("status")).toString();
        additional[QStringLiteral("episodes")] = obj.value(QStringLiteral("episodes")).toInt();
        additional[QStringLiteral("score")] = obj.value(QStringLiteral("averageScore")).toInt();
        res.additionalData = additional;

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderAniList::clone(QObject *parent) const
{
    return new ProviderAniList(parent);
}
