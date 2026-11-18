#include "provider_duckduckgo.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ProviderDuckDuckGo::ProviderDuckDuckGo(QObject *parent)
    : SearchProvider(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
}

ProviderDuckDuckGo::~ProviderDuckDuckGo()
{
}

QString ProviderDuckDuckGo::name() const
{
    return QStringLiteral("DuckDuckGo");
}

QString ProviderDuckDuckGo::id() const
{
    return QStringLiteral("duckduckgo");
}

QString ProviderDuckDuckGo::description() const
{
    return QStringLiteral("DuckDuckGo Instant Answer API (encyclopedic abstracts, definitions, and related topics; not full web search).");
}

void ProviderDuckDuckGo::search(const QString &query)
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

    QUrl url(QStringLiteral("https://api.duckduckgo.com/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    q.addQueryItem(QStringLiteral("no_html"), QStringLiteral("1"));
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

void ProviderDuckDuckGo::onReplyFinished(QNetworkReply *reply)
{
    if (m_reply == reply) {
        m_reply = nullptr;
    }

    reply->deleteLater();



    if (reply->error() != QNetworkReply::NoError || !reply->isReadable()) {
        emit searchFailed(id(), reply->error() == QNetworkReply::NoError ? QStringLiteral("Provider reply closed before completion") : reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QString parseError;
    const QList<SearchResult> results = parseResponse(data, &parseError);
    if (!parseError.isEmpty()) {
        emit searchFailed(id(), parseError);
        return;
    }

    emit searchCompleted(id(), results);
}

QList<SearchResult> ProviderDuckDuckGo::parseResponse(const QByteArray &data, QString *errorOut)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to parse JSON response");
        return {};
    }

    QJsonObject root = doc.object();
    QList<SearchResult> results;

    // Abstract
    const QString abstractText = root.value(QStringLiteral("AbstractText")).toString().trimmed();
    if (!abstractText.isEmpty()) {
        SearchResult res;
        res.id = QStringLiteral("abstract");
        res.title = root.value(QStringLiteral("Heading")).toString().trimmed();
        if (res.title.isEmpty())
            res.title = QStringLiteral("Instant Answer");
        res.description = abstractText;
        res.url = root.value(QStringLiteral("AbstractURL")).toString();
        res.type = QStringLiteral("abstract");
        const QString source = root.value(QStringLiteral("AbstractSource")).toString().trimmed();
        if (!source.isEmpty())
            res.additionalData.insert(QStringLiteral("source"), source);
        const QString entity = root.value(QStringLiteral("Entity")).toString().trimmed();
        if (!entity.isEmpty())
            res.additionalData.insert(QStringLiteral("entity"), entity);
        results.append(res);
    }

    // Direct Answer / Definition
    const QString answer = root.value(QStringLiteral("Answer")).toString().trimmed();
    if (!answer.isEmpty()) {
        SearchResult res;
        res.id = QStringLiteral("answer");
        res.title = QStringLiteral("Instant Answer");
        res.description = answer;
        res.type = root.value(QStringLiteral("AnswerType")).toString().trimmed();
        if (res.type.isEmpty())
            res.type = QStringLiteral("answer");
        results.append(res);
    }

    const QString definition = root.value(QStringLiteral("Definition")).toString().trimmed();
    if (!definition.isEmpty()) {
        SearchResult res;
        res.id = QStringLiteral("definition");
        res.title = root.value(QStringLiteral("Heading")).toString().trimmed();
        if (res.title.isEmpty())
            res.title = QStringLiteral("Definition");
        res.description = definition;
        res.url = root.value(QStringLiteral("DefinitionURL")).toString();
        res.type = QStringLiteral("definition");
        const QString defSource = root.value(QStringLiteral("DefinitionSource")).toString().trimmed();
        if (!defSource.isEmpty())
            res.additionalData.insert(QStringLiteral("source"), defSource);
        results.append(res);
    }

    // Related Topics (top-level items and nested topic groups)
    int relatedIndex = 0;
    auto appendTopic = [&](const QJsonObject &obj, const QString &groupName) {
        const QString text = obj.value(QStringLiteral("Text")).toString().trimmed();
        if (text.isEmpty())
            return;
        SearchResult res;
        const QString firstUrl = obj.value(QStringLiteral("FirstURL")).toString();
        res.id = !firstUrl.isEmpty() ? firstUrl : QStringLiteral("related:%1").arg(relatedIndex);
        ++relatedIndex;
        res.title = text.section(QStringLiteral(" - "), 0, 0).trimmed();
        if (res.title.isEmpty())
            res.title = text;
        res.description = text;
        res.url = firstUrl;
        res.type = QStringLiteral("related");
        if (!groupName.isEmpty())
            res.additionalData.insert(QStringLiteral("topicGroup"), groupName);
        results.append(res);
    };

    const QJsonArray relatedArray = root.value(QStringLiteral("RelatedTopics")).toArray();
    for (const QJsonValue &val : relatedArray) {
        const QJsonObject obj = val.toObject();
        if (obj.contains(QStringLiteral("Topics")) && obj.value(QStringLiteral("Topics")).isArray()) {
            const QString groupName = obj.value(QStringLiteral("Name")).toString().trimmed();
            for (const QJsonValue &nested : obj.value(QStringLiteral("Topics")).toArray()) {
                appendTopic(nested.toObject(), groupName);
                if (results.size() >= 50)
                    break;
            }
        } else {
            appendTopic(obj, {});
        }
        if (results.size() >= 50)
            break;
    }

    return results;
}

SearchProvider* ProviderDuckDuckGo::clone(QObject *parent) const
{
    auto *copy = new ProviderDuckDuckGo(parent);
    copy->setEnabled(isEnabled());
    return copy;
}
