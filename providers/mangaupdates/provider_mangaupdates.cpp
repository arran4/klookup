#include "provider_mangaupdates.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderMangaUpdates::ProviderMangaUpdates(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderMangaUpdates::~ProviderMangaUpdates()
{
}

QString ProviderMangaUpdates::name() const
{
    return QStringLiteral("MangaUpdates");
}

QString ProviderMangaUpdates::id() const
{
    return QStringLiteral("mangaupdates");
}

void ProviderMangaUpdates::search(const QString &query)
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

    QUrl url(QStringLiteral("https://api.mangaupdates.com/v1/series/search"));

    QJsonObject requestBody;
    requestBody.insert(QStringLiteral("search"), query);

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

void ProviderMangaUpdates::onReplyFinished(QNetworkReply *reply)
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
    QJsonArray resultsArray = root.value(QStringLiteral("results")).toArray();

    QList<SearchResult> results;
    for (const QJsonValue &val : resultsArray) {
        QJsonObject obj = val.toObject();
        QJsonObject record = obj.value(QStringLiteral("record")).toObject();

        SearchResult res;
        res.id = record.value(QStringLiteral("series_id")).toVariant().toString();
        res.title = record.value(QStringLiteral("title")).toString();
        res.description = record.value(QStringLiteral("description")).toString();
        if (res.description.length() > 200) {
            res.description = res.description.left(200) + QStringLiteral("...");
        }
        res.url = QStringLiteral("https://www.mangaupdates.com/series.html?id=") + res.id;
        res.type = QStringLiteral("manga");

        QVariantMap additional;
        additional[QStringLiteral("year")] = record.value(QStringLiteral("year")).toString();
        additional[QStringLiteral("rating")] = record.value(QStringLiteral("bayesian_rating")).toDouble();
        res.additionalData = additional;

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderMangaUpdates::clone(QObject *parent) const
{
    return new ProviderMangaUpdates(parent);
}
