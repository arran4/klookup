// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providerhostservice.h"

#include <QNetworkRequest>
#include <QTimer>

namespace ProviderHost
{

constexpr qint64 MaxResponseBytes = 5 * 1024 * 1024; // 5 MiB streaming limit

ProviderHostService::ProviderHostService(const ProviderPackage &package,
                                         QObject *parent,
                                         QNetworkAccessManager *nam)
    : QObject(parent)
    , m_package(package)
    , m_nam(nam)
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_ownsNam = true;
    }
}

ProviderHostService::~ProviderHostService()
{
    cancelAllRequests();
}

void ProviderHostService::cancelRequest(quint64 requestId)
{
    if (m_activeRequests.contains(requestId)) {
        auto active = m_activeRequests.take(requestId);
        if (active.reply) {
            active.reply->disconnect(this);
            if (active.reply->isRunning()) {
                active.reply->abort();
            }
            active.reply->deleteLater();
        }
    }
}

void ProviderHostService::cancelAllRequests()
{
    for (auto it = m_activeRequests.begin(); it != m_activeRequests.end(); ++it) {
        if (it.value().reply) {
            it.value().reply->disconnect(this);
            if (it.value().reply->isRunning()) {
                it.value().reply->abort();
            }
            it.value().reply->deleteLater();
        }
    }
    m_activeRequests.clear();
}

quint64 ProviderHostService::executeHttpRequest(const HttpRequestOptions &options)
{
    const quint64 reqId = options.requestId != 0 ? options.requestId : ++m_requestCounter;

    // 1. Trust Check
    if (!m_package.isTrusted()) {
        HttpResponse errResp;
        errResp.requestId = reqId;
        errResp.success = false;
        errResp.errorMessage = QStringLiteral("Untrusted provider package '%1' cannot make network requests.").arg(m_package.id());
        QTimer::singleShot(0, this, [this, reqId, errResp] {
            emit httpResponseReady(reqId, errResp);
        });
        return reqId;
    }

    // 2. URL Scheme Check
    if (!options.url.isValid() || (options.url.scheme() != QStringLiteral("https") && options.url.scheme() != QStringLiteral("http"))) {
        HttpResponse errResp;
        errResp.requestId = reqId;
        errResp.success = false;
        errResp.errorMessage = QStringLiteral("Invalid or unsupported URL scheme: %1").arg(options.url.scheme());
        QTimer::singleShot(0, this, [this, reqId, errResp] {
            emit httpResponseReady(reqId, errResp);
        });
        return reqId;
    }

    // 3. Origin Permission Check
    if (!m_package.isOriginAllowed(options.url)) {
        HttpResponse errResp;
        errResp.requestId = reqId;
        errResp.success = false;
        errResp.errorMessage = QStringLiteral("Target origin '%1' is not declared in package manifest permissions.")
                                   .arg(options.url.host());
        QTimer::singleShot(0, this, [this, reqId, errResp] {
            emit httpResponseReady(reqId, errResp);
        });
        return reqId;
    }

    // 4. Secret Permission & Resolution Check
    QString resolvedSecret;
    if (!options.secretKey.isEmpty()) {
        if (!m_package.isSecretAllowed(options.secretKey)) {
            HttpResponse errResp;
            errResp.requestId = reqId;
            errResp.success = false;
            errResp.errorMessage = QStringLiteral("Secret '%1' is not declared in package manifest permissions.")
                                       .arg(options.secretKey);
            QTimer::singleShot(0, this, [this, reqId, errResp] {
                emit httpResponseReady(reqId, errResp);
            });
            return reqId;
        }
        if (m_secretResolver) {
            resolvedSecret = m_secretResolver(options.secretKey);
        }
    }

    QUrl requestUrl = options.url;
    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup/1.0 (XDG Provider Package)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    for (auto it = options.headers.cbegin(); it != options.headers.cend(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    if (!resolvedSecret.isEmpty() && !options.secretHeader.isEmpty()) {
        request.setRawHeader(options.secretHeader.toUtf8(), resolvedSecret.toUtf8());
    }

    QNetworkReply *reply = nullptr;
    if (options.method.toUpper() == QStringLiteral("POST")) {
        reply = m_nam->post(request, options.body);
    } else {
        reply = m_nam->get(request);
    }

    auto buffer = std::make_shared<QByteArray>();
    auto limitExceeded = std::make_shared<bool>(false);

    ActiveReply active;
    active.requestId = reqId;
    active.requestUrl = requestUrl;
    active.reply = reply;
    active.buffer = buffer;
    active.limitExceeded = limitExceeded;
    m_activeRequests.insert(reqId, active);

    // Fast-path: Content-Length check
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reqId, reply, limitExceeded] {
        if (!m_activeRequests.contains(reqId) || *limitExceeded || !reply) return;
        const QVariant lengthVar = reply->header(QNetworkRequest::ContentLengthHeader);
        if (lengthVar.isValid()) {
            const qint64 length = lengthVar.toLongLong();
            if (length > MaxResponseBytes) {
                *limitExceeded = true;
                reply->abort();
            }
        }
    });

    // Streaming accumulation bound
    connect(reply, &QIODevice::readyRead, this, [this, reqId, reply, buffer, limitExceeded] {
        if (!m_activeRequests.contains(reqId) || *limitExceeded || !reply) return;
        buffer->append(reply->readAll());
        if (buffer->size() > MaxResponseBytes) {
            *limitExceeded = true;
            reply->abort();
        }
    });

    // Timeout timer
    const int timeout = options.timeoutMs > 0 ? options.timeoutMs : 10000;
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply] {
        if (reply && reply->isRunning()) {
            reply->abort();
        }
    });
    timer->start(timeout);

    // Completion handler
    connect(reply, &QNetworkReply::finished, this, [this, reqId, reply, buffer, limitExceeded, requestUrl] {
        if (!m_activeRequests.contains(reqId)) {
            reply->deleteLater();
            return;
        }
        m_activeRequests.remove(reqId);
        reply->deleteLater();

        HttpResponse resp;
        resp.requestId = reqId;

        if (*limitExceeded) {
            resp.success = false;
            resp.truncated = true;
            resp.errorMessage = QStringLiteral("Response exceeded maximum streaming limit of 5 MiB.");
            emit httpResponseReady(reqId, resp);
            return;
        }

        // Redirect security check
        const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectTarget.isValid()) {
            const QUrl targetUrl = requestUrl.resolved(redirectTarget.toUrl());
            if (requestUrl.scheme() == QStringLiteral("https") && targetUrl.scheme() != QStringLiteral("https")) {
                resp.success = false;
                resp.errorMessage = QStringLiteral("Insecure redirect from HTTPS to HTTP blocked.");
                emit httpResponseReady(reqId, resp);
                return;
            }
            if (targetUrl.host() != requestUrl.host() && !m_package.isOriginAllowed(targetUrl)) {
                resp.success = false;
                resp.errorMessage = QStringLiteral("Cross-origin redirect to undeclared host '%1' blocked.")
                                        .arg(targetUrl.host());
                emit httpResponseReady(reqId, resp);
                return;
            }
        }

        if (reply->error() != QNetworkReply::NoError) {
            resp.success = false;
            resp.errorMessage = reply->errorString();
            resp.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit httpResponseReady(reqId, resp);
            return;
        }

        resp.success = true;
        resp.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        resp.body = *buffer;
        for (const auto &pair : reply->rawHeaderPairs()) {
            resp.headers.insert(QString::fromUtf8(pair.first), QString::fromUtf8(pair.second));
        }

        emit httpResponseReady(reqId, resp);
    });

    return reqId;
}

} // namespace ProviderHost
