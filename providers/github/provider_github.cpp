#include "provider_github.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderGithub::ProviderGithub(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager())
    , m_reply(nullptr)
{
}

ProviderGithub::~ProviderGithub()
{
    m_nam->deleteLater();
}

QString ProviderGithub::name() const
{
    return QStringLiteral("GitHub");
}

QString ProviderGithub::id() const
{
    return QStringLiteral("github");
}

void ProviderGithub::setCredentials(const QString &username, const QString &password)
{
    // For GitHub API, we use Personal Access Tokens as password
    m_authToken = password;
}

void ProviderGithub::search(const QString &query)
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QUrl url(QStringLiteral("https://api.github.com/search/repositories"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup-App"));
    request.setRawHeader("Accept", "application/vnd.github.v3+json");

    if (!m_authToken.isEmpty()) {
        request.setRawHeader("Authorization", ("token " + m_authToken).toUtf8());
    }

    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::finished, this, &ProviderGithub::onReplyFinished);
}

void ProviderGithub::onReplyFinished()
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
    QJsonArray searchArray = root.value(QStringLiteral("items")).toArray();

    QList<SearchResult> results;
    for (const QJsonValue &val : searchArray) {
        QJsonObject obj = val.toObject();
        SearchResult res;
        res.id = QString::number(obj.value(QStringLiteral("id")).toInt());
        res.title = obj.value(QStringLiteral("full_name")).toString();
        res.description = obj.value(QStringLiteral("description")).toString();
        res.url = obj.value(QStringLiteral("html_url")).toString();
        res.type = QStringLiteral("repository");

        QVariantMap additional;
        additional[QStringLiteral("stars")] = obj.value(QStringLiteral("stargazers_count")).toInt();
        additional[QStringLiteral("language")] = obj.value(QStringLiteral("language")).toString();
        res.additionalData = additional;

        results.append(res);
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderGithub::clone(QObject *parent) const
{
    ProviderGithub *cloned = new ProviderGithub(parent);
    cloned->m_authToken = m_authToken;
    return cloned;
}
