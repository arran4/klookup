#include "provider_mangabaka.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderMangaBaka::ProviderMangaBaka(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderMangaBaka::~ProviderMangaBaka()
{
}

QString ProviderMangaBaka::name() const
{
    return QStringLiteral("MangaBaka");
}

QString ProviderMangaBaka::id() const
{
    return QStringLiteral("mangabaka");
}

void ProviderMangaBaka::search(const QString &query)
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

    QUrl url(QStringLiteral("https://api.mangabaka.org/v2/series/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup-App"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");

    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::finished, this, [this, reply = QPointer<QNetworkReply>(m_reply)]() {
        if (!reply || reply != m_reply) {
            return;
        }
        this->onReplyFinished(reply);
    });
}

void ProviderMangaBaka::onReplyFinished(QNetworkReply *reply)
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
        // API returns id as number but we store it as string.
        res.id = QString::number(obj.value(QStringLiteral("id")).toInt());

        QJsonArray titlesArray = obj.value(QStringLiteral("titles")).toArray();
        for (const QJsonValue &tVal : titlesArray) {
            QJsonObject tObj = tVal.toObject();
            if (tObj.value(QStringLiteral("language")).toString() == QStringLiteral("en")) {
                res.title = tObj.value(QStringLiteral("title")).toString();
                break;
            }
        }
        if (res.title.isEmpty() && !titlesArray.isEmpty()) {
            res.title = titlesArray.first().toObject().value(QStringLiteral("title")).toString();
        }
        if (res.title.isEmpty()) {
            res.title = obj.value(QStringLiteral("title")).toString();
        }

        res.description = obj.value(QStringLiteral("description")).toString();

        if (res.description.length() > 200) {
            res.description = res.description.left(200) + QStringLiteral("...");
        }

        res.url = obj.value(QStringLiteral("canonical_url")).toString();
        res.type = obj.value(QStringLiteral("type")).toString();

        QVariantMap additional;
        if (obj.contains(QStringLiteral("status")) && !obj.value(QStringLiteral("status")).isNull()) {
            additional[QStringLiteral("status")] = obj.value(QStringLiteral("status")).toString();
        }
        if (obj.contains(QStringLiteral("year")) && !obj.value(QStringLiteral("year")).isNull()) {
            additional[QStringLiteral("year")] = obj.value(QStringLiteral("year")).toInt();
        }
        if (!additional.isEmpty()) {
            res.additionalData = additional;
        }

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderMangaBaka::clone(QObject *parent) const
{
    return new ProviderMangaBaka(parent);
}
