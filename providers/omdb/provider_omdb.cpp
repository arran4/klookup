#include "provider_omdb.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderOMDb::ProviderOMDb(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderOMDb::~ProviderOMDb()
{
}

QString ProviderOMDb::name() const
{
    return QStringLiteral("OMDb API");
}

QString ProviderOMDb::id() const
{
    return QStringLiteral("omdb");
}

void ProviderOMDb::setCredentials(const QString &/*username*/, const QString &password)
{
    m_apiKey = password;
}

void ProviderOMDb::search(const QString &query)
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

    if (m_apiKey.isEmpty()) {
        emit searchFailed(id(), QStringLiteral("API Key is missing. Please configure it in Settings."));
        return;
    }

    QUrl url(QStringLiteral("https://www.omdbapi.com/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("apikey"), m_apiKey);
    q.addQueryItem(QStringLiteral("s"), query);
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

void ProviderOMDb::onReplyFinished(QNetworkReply *reply)
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

    if (root.value(QStringLiteral("Response")).toString() == QStringLiteral("False")) {
        emit searchFailed(id(), root.value(QStringLiteral("Error")).toString());
        return;
    }

    QJsonArray resultsArray = root.value(QStringLiteral("Search")).toArray();

    QList<SearchResult> results;
    for (const QJsonValue &val : resultsArray) {
        QJsonObject obj = val.toObject();

        SearchResult res;
        res.id = obj.value(QStringLiteral("imdbID")).toString();
        res.title = obj.value(QStringLiteral("Title")).toString();
        res.type = obj.value(QStringLiteral("Type")).toString();
        res.url = QStringLiteral("https://www.imdb.com/title/") + res.id;

        QVariantMap additional;
        additional[QStringLiteral("year")] = obj.value(QStringLiteral("Year")).toString();
        res.additionalData = additional;

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderOMDb::clone(QObject *parent) const
{
    ProviderOMDb *cloned = new ProviderOMDb(parent);
    cloned->m_apiKey = m_apiKey;
    return cloned;
}
