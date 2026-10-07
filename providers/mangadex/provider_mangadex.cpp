#include "provider_mangadex.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderMangaDex::ProviderMangaDex(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderMangaDex::~ProviderMangaDex()
{
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
    if (m_reply) {
        QPointer<QNetworkReply> oldReply = m_reply;
        m_reply = nullptr;
        oldReply->disconnect(this);
        if (oldReply->isRunning()) {
            oldReply->abort();
        }
        oldReply->deleteLater();
    }

    QUrl url(QStringLiteral("https://api.mangadex.org/manga"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"), query);
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

void ProviderMangaDex::onReplyFinished(QNetworkReply *reply)
{
    if (m_reply == reply) {
        m_reply = nullptr;
    }

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
    QJsonArray resultsArray = root.value(QStringLiteral("data")).toArray();

    QList<SearchResult> results;
    for (const QJsonValue &val : resultsArray) {
        QJsonObject obj = val.toObject();

        SearchResult res;
        res.id = obj.value(QStringLiteral("id")).toString();

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

        if (res.description.length() > 200) {
            res.description = res.description.left(200) + QStringLiteral("...");
        }

        res.url = QStringLiteral("https://mangadex.org/title/") + res.id;
        res.type = QStringLiteral("manga");

        QVariantMap additional;
        additional[QStringLiteral("status")] = attributes.value(QStringLiteral("status")).toString();
        additional[QStringLiteral("year")] = attributes.value(QStringLiteral("year")).toInt();
        res.additionalData = additional;

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderMangaDex::clone(QObject *parent) const
{
    return new ProviderMangaDex(parent);
}
