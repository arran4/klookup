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



    if (reply->error() != QNetworkReply::NoError || !reply->isReadable()) {
        const QByteArray errBody = reply->isReadable() ? reply->readAll() : QByteArray();
        const QJsonDocument errDoc = QJsonDocument::fromJson(errBody);
        const QString apiMsg = errDoc.isObject() ? errDoc.object().value(QStringLiteral("message")).toString().trimmed() : QString();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if ((statusCode == 403 || statusCode == 429) &&
            (apiMsg.contains(QLatin1String("rate limit"), Qt::CaseInsensitive) ||
             reply->rawHeader("X-RateLimit-Remaining") == "0")) {
            emit searchFailed(id(), QStringLiteral("GitHub API rate limit exceeded; retry later or configure a token in Settings."));
            return;
        }
        emit searchFailed(id(), !apiMsg.isEmpty() ? apiMsg
                                                  : (reply->error() == QNetworkReply::NoError
                                                         ? QStringLiteral("Provider reply closed before completion")
                                                         : reply->errorString()));
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
        result.description = obj.value(QStringLiteral("description")).toString().trimmed();
        result.url = obj.value(QStringLiteral("html_url")).toString();
        result.type = QStringLiteral("repository");

        QVariantMap extra;
        if (obj.contains(QStringLiteral("stargazers_count")) && obj.value(QStringLiteral("stargazers_count")).isDouble())
            extra.insert(QStringLiteral("stars"), obj.value(QStringLiteral("stargazers_count")).toInt());
        const QString lang = obj.value(QStringLiteral("language")).toString().trimmed();
        if (!lang.isEmpty())
            extra.insert(QStringLiteral("language"), lang);

        QStringList flags;
        if (obj.value(QStringLiteral("private")).toBool())
            flags.append(QStringLiteral("Private"));
        if (obj.value(QStringLiteral("archived")).toBool())
            flags.append(QStringLiteral("Archived"));
        if (obj.value(QStringLiteral("fork")).toBool())
            flags.append(QStringLiteral("Fork"));
        if (!flags.isEmpty())
            extra.insert(QStringLiteral("status"), flags.join(QStringLiteral(", ")));

        if (obj.contains(QStringLiteral("forks_count")) && obj.value(QStringLiteral("forks_count")).isDouble())
            extra.insert(QStringLiteral("forks"), obj.value(QStringLiteral("forks_count")).toInt());
        if (obj.contains(QStringLiteral("open_issues_count")) && obj.value(QStringLiteral("open_issues_count")).isDouble())
            extra.insert(QStringLiteral("openIssues"), obj.value(QStringLiteral("open_issues_count")).toInt());
        if (obj.contains(QStringLiteral("watchers_count")) && obj.value(QStringLiteral("watchers_count")).isDouble())
            extra.insert(QStringLiteral("watchers"), obj.value(QStringLiteral("watchers_count")).toInt());

        const QJsonObject licenseObj = obj.value(QStringLiteral("license")).toObject();
        QString license = licenseObj.value(QStringLiteral("spdx_id")).toString().trimmed();
        if (license.isEmpty() || license == QLatin1String("NOASSERTION"))
            license = licenseObj.value(QStringLiteral("name")).toString().trimmed();
        if (!license.isEmpty() && license != QLatin1String("NOASSERTION"))
            extra.insert(QStringLiteral("license"), license);

        const QString updatedAt = obj.value(QStringLiteral("updated_at")).toString().left(10);
        if (updatedAt.size() == 10)
            extra.insert(QStringLiteral("updatedAt"), updatedAt);
        const QString pushedAt = obj.value(QStringLiteral("pushed_at")).toString().left(10);
        if (pushedAt.size() == 10)
            extra.insert(QStringLiteral("pushedAt"), pushedAt);

        const QString homepageRaw = obj.value(QStringLiteral("homepage")).toString().trimmed();
        const QUrl homepageUrl(homepageRaw);
        if (homepageUrl.isValid() && (homepageUrl.scheme() == QLatin1String("https") || homepageUrl.scheme() == QLatin1String("http")) &&
            !homepageUrl.host().isEmpty()) {
            extra.insert(QStringLiteral("homepage"), homepageUrl.toString(QUrl::FullyEncoded));
        }

        QStringList topics;
        for (const QJsonValue &tVal : obj.value(QStringLiteral("topics")).toArray()) {
            const QString topic = tVal.toString().trimmed();
            if (!topic.isEmpty() && !topics.contains(topic)) {
                topics.append(topic);
                if (topics.size() >= 12)
                    break;
            }
        }
        if (!topics.isEmpty())
            extra.insert(QStringLiteral("topics"), topics);

        extra.insert(QStringLiteral("rawSources"), QVariantList{
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("github_repo_json")},
                {QStringLiteral("label"), QStringLiteral("GitHub Repository JSON")},
                {QStringLiteral("mimeType"), QStringLiteral("application/json")},
                {QStringLiteral("provenance"), QStringLiteral("GitHub REST API /search/repositories")},
                {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented))}
            }
        });

        result.additionalData = extra;
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
    cloned->setEnabled(isEnabled());
    cloned->m_authToken = m_authToken;
    return cloned;
}
