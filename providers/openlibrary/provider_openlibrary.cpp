#include "provider_openlibrary.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderOpenLibrary::ProviderOpenLibrary(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager())
    , m_reply(nullptr)
{
}

ProviderOpenLibrary::~ProviderOpenLibrary()
{
    m_nam->deleteLater();
}

QString ProviderOpenLibrary::name() const
{
    return QStringLiteral("OpenLibrary");
}

QString ProviderOpenLibrary::id() const
{
    return QStringLiteral("openlibrary");
}

void ProviderOpenLibrary::search(const QString &query)
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QUrl url(QStringLiteral("https://openlibrary.org/search.json"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup-App"));

    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::finished, this, &ProviderOpenLibrary::onReplyFinished);
}

void ProviderOpenLibrary::onReplyFinished()
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

    QJsonObject root = doc.object();
    QJsonArray resultsArray = root.value(QStringLiteral("docs")).toArray();

    QList<SearchResult> results;
    for (const QJsonValue &val : resultsArray) {
        QJsonObject obj = val.toObject();

        SearchResult res;
        res.id = obj.value(QStringLiteral("key")).toString();
        res.title = obj.value(QStringLiteral("title")).toString();

        QJsonArray authorArray = obj.value(QStringLiteral("author_name")).toArray();
        QStringList authors;
        for (const QJsonValue &author : authorArray) {
            authors.append(author.toString());
        }
        res.description = QStringLiteral("Author(s): ") + authors.join(QStringLiteral(", "));
        res.url = QStringLiteral("https://openlibrary.org") + res.id;
        res.type = QStringLiteral("book");

        QVariantMap additional;
        additional[QStringLiteral("first_publish_year")] = obj.value(QStringLiteral("first_publish_year")).toInt();
        res.additionalData = additional;

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderOpenLibrary::clone(QObject *parent) const
{
    return new ProviderOpenLibrary(parent);
}
