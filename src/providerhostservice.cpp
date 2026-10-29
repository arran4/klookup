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

        // Asynchronous resolution via CredentialStore (KWallet)
        if (m_credentialStore) {
            const QString storeKey = QStringLiteral("provider_pkg:%1:%2").arg(m_package.id(), options.secretKey);
            m_credentialStore->read(storeKey, true, [this, reqId, options](const CredentialStoreResult &res) {
                const QString secretVal = res.ok ? res.value : QString();
                startRequestInternal(reqId, options, 0, secretVal);
            });
            return reqId;
        }

        if (m_secretResolver) {
            const QString secretVal = m_secretResolver(options.secretKey);
            startRequestInternal(reqId, options, 0, secretVal);
            return reqId;
        }
    }

    startRequestInternal(reqId, options, 0, QString());
    return reqId;
}

void ProviderHostService::startRequestInternal(quint64 reqId, const HttpRequestOptions &options, int hops, const QString &resolvedSecret)
{
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
    active.options = options;
    active.hops = hops;
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

    // Streaming accumulation bound: read strictly bounded chunks, immediately aborting without buffer bloat
    connect(reply, &QIODevice::readyRead, this, [this, reqId, reply, buffer, limitExceeded] {
        if (!m_activeRequests.contains(reqId) || *limitExceeded || !reply) return;
        const qint64 remaining = MaxResponseBytes - buffer->size();
        if (remaining <= 0) {
            *limitExceeded = true;
            buffer->clear();
            reply->abort();
            return;
        }
        const qint64 available = reply->bytesAvailable();
        if (available > remaining) {
            *limitExceeded = true;
            buffer->clear();
            reply->abort();
            return;
        }
        buffer->append(reply->read(available));
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
    connect(reply, &QNetworkReply::finished, this, [this, reqId, reply, buffer, limitExceeded, options, hops, resolvedSecret] {
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

        // Bounded and permission-rechecked redirect handling (up to 5 hops)
        const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectTarget.isValid()) {
            if (hops >= 5) {
                resp.success = false;
                resp.errorMessage = QStringLiteral("Too many redirects: exceeded maximum of 5 hops.");
                emit httpResponseReady(reqId, resp);
                return;
            }
            const QUrl targetUrl = options.url.resolved(redirectTarget.toUrl());
            if (!targetUrl.isValid()) {
                resp.success = false;
                resp.errorMessage = QStringLiteral("Invalid redirect target URL.");
                emit httpResponseReady(reqId, resp);
                return;
            }
            // Enforce no HTTPS -> HTTP scheme downgrade
            if (options.url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
                targetUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
                resp.success = false;
                resp.errorMessage = QStringLiteral("Insecure redirect from HTTPS to HTTP blocked.");
                emit httpResponseReady(reqId, resp);
                return;
            }
            // Permission check on redirected target
            if (!m_package.isOriginAllowed(targetUrl)) {
                resp.success = false;
                resp.errorMessage = QStringLiteral("Redirect to undeclared origin '%1' blocked by package policy.")
                                        .arg(targetUrl.host());
                emit httpResponseReady(reqId, resp);
                return;
            }
            // If cross-origin redirect, strip secrets
            HttpRequestOptions nextOptions = options;
            nextOptions.url = targetUrl;
            QString nextSecret = resolvedSecret;
            if (targetUrl.host().compare(options.url.host(), Qt::CaseInsensitive) != 0) {
                nextSecret.clear();
                nextOptions.secretKey.clear();
                nextOptions.secretHeader.clear();
            }
            startRequestInternal(reqId, nextOptions, hops + 1, nextSecret);
            return;
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
}

} // namespace ProviderHost
