#ifndef PROVIDERREQUEST_H
#define PROVIDERREQUEST_H

#include "searchprovider.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QUrl>
#include <functional>

namespace ProviderRequest
{
inline void cancel(QPointer<QNetworkReply> &active, QObject *receiver)
{
    const auto reply = active;
    active = nullptr;
    if (reply) {
        reply->disconnect(receiver);
        reply->abort();
        // An observer of finished() may synchronously delete an aborted reply.
        if (reply)
            reply->deleteLater();
    }
}

// Independent page/detail lanes, bounded transfers and no automatic rate-limit
// retry. The UI retains the failed token and honours Retry-After on manual retry.
inline void getJson(SearchProvider *owner, QNetworkAccessManager *manager, QPointer<QNetworkReply> &active, const QUrl &url,
                    quint64 requestId, std::function<void(const QJsonObject &)> success)
{
    cancel(active, owner);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup/0.1 (https://github.com/arran4/klookup)"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    active = manager->get(request);
    auto *reply = active.data();
    reply->setReadBufferSize(8 * 1024 * 1024 + 1);
    QObject::connect(reply, &QNetworkReply::readyRead, owner, [reply] {
        if (reply->bytesAvailable() > 8 * 1024 * 1024) {
            reply->setProperty("responseTooLarge", true);
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, owner, [owner, &active, reply = QPointer<QNetworkReply>(reply), requestId, success] {
        if (!reply || active != reply)
            return;
        active = nullptr;
        reply->deleteLater();
        QString error;
        int retryMs = 0;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->property("responseTooLarge").toBool() || reply->bytesAvailable() > 8 * 1024 * 1024) {
            error = QStringLiteral("Response exceeds 8 MiB limit");
        } else if (status && (status < 200 || status >= 300)) {
            error = QStringLiteral("HTTP Error: %1").arg(status);
            if (status == 429 || status == 503) {
                const auto retry = reply->rawHeader("Retry-After");
                bool ok = false;
                const qint64 seconds = retry.toLongLong(&ok);
                const qint64 delay =
                    ok ? qBound(qint64(0), seconds, qint64(86400)) * 1000
                       : QDateTime::currentDateTimeUtc().msecsTo(QDateTime::fromString(QString::fromLatin1(retry), Qt::RFC2822Date));
                retryMs = int(qBound(qint64(1000), delay, qint64(86400000)));
            }
        } else if (reply->error() != QNetworkReply::NoError) {
            error = reply->error() == QNetworkReply::TimeoutError ? QStringLiteral("Request timed out")
                                                                  : QStringLiteral("Network request failed");
        }
        QJsonParseError parseError;
        const auto doc = error.isEmpty() ? QJsonDocument::fromJson(reply->readAll(), &parseError) : QJsonDocument();
        if (error.isEmpty() && (parseError.error != QJsonParseError::NoError || !doc.isObject()))
            error = QStringLiteral("Failed to parse JSON response");
        if (!error.isEmpty()) {
            if (requestId)
                emit owner->requestFailed(requestId, error, retryMs);
            else
                emit owner->searchFailed(owner->id(), error);
            return;
        }
        success(doc.object());
    });
}

inline void fail(SearchProvider *owner, quint64 requestId, const QString &error)
{
    if (requestId)
        emit owner->requestFailed(requestId, error);
    else
        emit owner->searchFailed(owner->id(), error);
}
} // namespace ProviderRequest
#endif
