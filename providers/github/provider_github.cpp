#include "provider_github.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderGithub::ProviderGithub(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderGithub::~ProviderGithub()
{
}

QString ProviderGithub::name() const
{
    return QStringLiteral("GitHub");
}

QString ProviderGithub::id() const
{
    return QStringLiteral("github");
}

void ProviderGithub::setCredentials(const QString &/*username*/, const QString &password)
{
    // For GitHub API, we use Personal Access Tokens as password
    m_authToken = password;
}

void ProviderGithub::search(const QString &query)
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

    connect(m_reply, &QNetworkReply::finished, this, [this, reply = QPointer<QNetworkReply>(m_reply)]() {
        if (!reply || reply != m_reply) {
            return;
        }
        this->onReplyFinished(reply);
    });
}

void ProviderGithub::onReplyFinished(QNetworkReply *reply)
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

    bool ok = false;
    const auto results = parseResponse(data, &ok);
    if (!ok) { emit searchFailed(id(), QStringLiteral("Invalid repository response")); return; }
    emit searchCompleted(id(), results);
}

QList<SearchResult> ProviderGithub::parseResponse(const QByteArray &data, bool *ok)
{
    *ok = false;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject() || !doc.object().value(QStringLiteral("items")).isArray()) return {};
    QList<SearchResult> results;
    for (const auto &value : doc.object().value(QStringLiteral("items")).toArray()) {
        const auto obj = value.toObject();
        if (!obj.value(QStringLiteral("id")).isDouble() || obj.value(QStringLiteral("full_name")).toString().isEmpty()) continue;
        SearchResult result;
        result.id = QString::number(obj.value(QStringLiteral("id")).toInteger());
        result.title = obj.value(QStringLiteral("full_name")).toString();
        result.description = obj.value(QStringLiteral("description")).toString();
        result.url = obj.value(QStringLiteral("html_url")).toString();
        result.type = QStringLiteral("repository");
        result.additionalData = {{QStringLiteral("stars"), obj.value(QStringLiteral("stargazers_count")).toInt()}, {QStringLiteral("language"), obj.value(QStringLiteral("language")).toString()}};
        results.append(result);
    }
    *ok = true; return results;
}

QJsonObject ProviderGithub::selfTestV1() const
{
    // Synthetic public metadata, never a credential or private repository.
    bool ok = false;
    const auto results = parseResponse(R"({"items":[{"id":42,"full_name":"fixture/雪","description":"Example","html_url":"https://github.com/fixture/example","stargazers_count":7,"language":"C++"},null]})", &ok);
    bool passed = ok && results.size() == 1 && results.first().id == QLatin1String("42") && results.first().title == QString::fromUtf8("fixture/雪") && results.first().additionalData.value(QStringLiteral("stars")).toInt() == 7;
    passed = passed && parseResponse(R"({"items":[]})", &ok).isEmpty() && ok;
    parseResponse("{", &ok); passed = passed && !ok;
    parseResponse(R"({"message":"rate limited"})", &ok); passed = passed && !ok;
    return {{QStringLiteral("passed"), passed}};
}

SearchProvider* ProviderGithub::clone(QObject *parent) const
{
    ProviderGithub *cloned = new ProviderGithub(parent);
    cloned->m_authToken = m_authToken;
    return cloned;
}
