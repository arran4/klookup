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
    q.addQueryItem(QStringLiteral("skip_disambig"), QStringLiteral("1"));
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
    QList<SearchResult> results;

    // Abstract
    QString abstractText = root.value(QStringLiteral("AbstractText")).toString();
    if (!abstractText.isEmpty()) {
        SearchResult res;
        res.id = QStringLiteral("abstract");
        res.title = root.value(QStringLiteral("Heading")).toString();
        res.description = abstractText;
        res.url = root.value(QStringLiteral("AbstractURL")).toString();
        res.type = QStringLiteral("abstract");
        results.append(res);
    }

    // Related Topics
    QJsonArray relatedArray = root.value(QStringLiteral("RelatedTopics")).toArray();
    for (const QJsonValue &val : relatedArray) {
        QJsonObject obj = val.toObject();
        if (obj.contains(QStringLiteral("Text"))) {
            SearchResult res;
            res.title = obj.value(QStringLiteral("Text")).toString().section(QStringLiteral(" - "), 0, 0);
            res.description = obj.value(QStringLiteral("Text")).toString();
            res.url = obj.value(QStringLiteral("FirstURL")).toString();
            res.type = QStringLiteral("related");
            results.append(res);
        }
    }

    emit searchCompleted(id(), results);
}

SearchProvider* ProviderDuckDuckGo::clone(QObject *parent) const
{
    return new ProviderDuckDuckGo(parent);
}
