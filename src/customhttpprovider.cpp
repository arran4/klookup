// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "customhttpprovider.h"

#include <memory>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

CustomHttpProvider::CustomHttpProvider(const CustomHttpSourceDefinition &definition,
                                       QObject *parent,
                                       QNetworkAccessManager *nam)
    : SearchProvider(parent)
    , m_definition(definition)
    , m_nam(nam)
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_ownsNam = true;
    }
    setEnabled(definition.enabled);
}

CustomHttpProvider::~CustomHttpProvider()
{
    cancelRequests();
}

QString CustomHttpProvider::id() const
{
    return m_definition.id;
}

QString CustomHttpProvider::name() const
{
    return m_definition.name;
}

SearchProvider *CustomHttpProvider::clone(QObject *parent) const
{
    auto *cloned = new CustomHttpProvider(m_definition, parent);
    cloned->setSecretToken(m_secretToken);
    return cloned;
}

void CustomHttpProvider::search(const QString &query)
{
    executeRequest(query, 0);
}

void CustomHttpProvider::requestPage(const QString &query, const QString &token, quint64 requestId)
{
    Q_UNUSED(token);
    executeRequest(query, requestId);
}

void CustomHttpProvider::cancelRequests()
{
    if (m_currentReply) {
        m_currentReply->disconnect(this);
        if (m_currentReply->isRunning()) {
            m_currentReply->abort();
        }
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

CustomHttpSourceDefinition CustomHttpProvider::definition() const
{
    return m_definition;
}

void CustomHttpProvider::setDefinition(const CustomHttpSourceDefinition &definition)
{
    m_definition = definition;
    setEnabled(definition.enabled);
}

void CustomHttpProvider::setSecretToken(const QString &token)
{
    m_secretToken = token;
}

bool CustomHttpProvider::supportsAuthentication() const
{
    return m_definition.requiresSecret();
}

void CustomHttpProvider::setCredentials(const QString &username, const QString &password)
{
    Q_UNUSED(username);
    setSecretToken(password);
}

void CustomHttpProvider::executeRequest(const QString &query, quint64 requestId)
{
    cancelRequests();
    m_currentRequestId = requestId;

    if (!m_definition.trusted) {
        const QString err = QStringLiteral("Custom HTTP source '%1' is untrusted and cannot execute requests. Please review and trust it in Settings.").arg(name());
        emit searchFailed(id(), err);
        emit requestFailed(requestId, err);
        return;
    }

    QString validationError;
    if (!m_definition.isValid(&validationError)) {
        emit searchFailed(id(), validationError);
        emit requestFailed(requestId, validationError);
        return;
    }

    QString urlStr = CustomHttpUtils::formatUrl(m_definition.urlTemplate, query);
    if (m_definition.requiresSecret() && !m_secretToken.isEmpty()) {
        urlStr.replace(QStringLiteral("{secret}"), QString::fromUtf8(QUrl::toPercentEncoding(m_secretToken)));
    }

    const QUrl url(urlStr);
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
        emit searchFailed(id(), QStringLiteral("Invalid destination URL scheme."));
        emit requestFailed(requestId, QStringLiteral("Invalid destination URL scheme."));
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup/1.0 (Custom HTTP Lookup)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    for (auto it = m_definition.headers.cbegin(); it != m_definition.headers.cend(); ++it) {
        QString headerVal = it.value();
        if (m_definition.requiresSecret() && !m_secretToken.isEmpty()) {
            headerVal.replace(QStringLiteral("{secret}"), m_secretToken);
        }
        request.setRawHeader(it.key().toUtf8(), headerVal.toUtf8());
    }

    if (!m_definition.authHeader.isEmpty() && !m_secretToken.isEmpty()) {
        QString authVal = m_definition.authSecretToken.isEmpty() ? m_secretToken : m_definition.authSecretToken;
        authVal.replace(QStringLiteral("{secret}"), m_secretToken);
        request.setRawHeader(m_definition.authHeader.toUtf8(), authVal.toUtf8());
    }

    QNetworkReply *reply = nullptr;
    if (m_definition.method == CustomHttpSourceDefinition::Method::Post) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, m_definition.postContentType);
        QByteArray body = CustomHttpUtils::formatBody(m_definition.bodyTemplate, m_definition.postContentType, query);
        if (m_definition.requiresSecret() && !m_secretToken.isEmpty()) {
            body.replace("{secret}", m_secretToken.toUtf8());
        }
        reply = m_nam->post(request, body);
    } else {
        reply = m_nam->get(request);
    }

    m_currentReply = reply;

    // Hard maximum response cap of 5 MiB during reception
    static constexpr qint64 MaxResponseBytes = 5 * 1024 * 1024;
    auto buffer = std::make_shared<QByteArray>();
    auto limitExceeded = std::make_shared<bool>(false);

    // Fast-path: check Content-Length header when available
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply, limitExceeded, requestId]() {
        if (*limitExceeded || !reply) return;
        const QVariant lengthVar = reply->header(QNetworkRequest::ContentLengthHeader);
        if (lengthVar.isValid()) {
            const qint64 length = lengthVar.toLongLong();
            if (length > MaxResponseBytes) {
                *limitExceeded = true;
                reply->abort();
                const QString err = QStringLiteral("Response Content-Length (%1 bytes) exceeds maximum limit of 5 MiB.").arg(length);
                emit searchFailed(id(), err);
                emit requestFailed(requestId, err);
            }
        }
    });

    // Streaming accumulation: abort immediately if exceeded during transmission
    connect(reply, &QIODevice::readyRead, this, [this, reply, buffer, limitExceeded, requestId]() {
        if (*limitExceeded || !reply) return;
        const QByteArray chunk = reply->readAll();
        buffer->append(chunk);
        if (buffer->size() > MaxResponseBytes) {
            *limitExceeded = true;
            reply->abort();
            const QString err = QStringLiteral("Response size exceeded maximum limit of 5 MiB.");
            emit searchFailed(id(), err);
            emit requestFailed(requestId, err);
        }
    });

    // Guard timeout of 10 seconds
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply] {
        if (reply && reply->isRunning()) {
            reply->abort();
        }
    });
    timer->start(10000);

    connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, limitExceeded, requestId, url] {
        if (!reply || m_currentReply != reply) {
            return;
        }
        m_currentReply = nullptr;
        reply->deleteLater();

        if (*limitExceeded) {
            return;
        }

        // Cross-origin redirect protection for credentials
        const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectTarget.isValid()) {
            const QUrl targetUrl = url.resolved(redirectTarget.toUrl());
            if (targetUrl.scheme() != url.scheme() || targetUrl.host() != url.host() || targetUrl.port() != url.port()) {
                const QString err = QStringLiteral("Cross-origin redirect to '%1' blocked for credential protection.").arg(targetUrl.host());
                emit searchFailed(id(), err);
                emit requestFailed(requestId, err);
                return;
            }
        }

        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() == QNetworkReply::OperationCanceledError) {
                return;
            }
            emit searchFailed(id(), reply->errorString());
            emit requestFailed(requestId, QStringLiteral("HTTP Request Failed: %1").arg(reply->errorString()));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(*buffer, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit searchFailed(id(), parseError.errorString());
            emit requestFailed(requestId, QStringLiteral("JSON parsing error: %1").arg(parseError.errorString()));
            return;
        }

        const QList<SearchResult> results = CustomHttpUtils::extractResults(doc, m_definition.mapping, id());
        emit searchCompleted(id(), results);
        SearchPage page;
        page.results = results;
        emit pageCompleted(requestId, page);
    });
}
