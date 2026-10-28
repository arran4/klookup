// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "customhttpprovider.h"

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

void CustomHttpProvider::executeRequest(const QString &query, quint64 requestId)
{
    cancelRequests();
    m_currentRequestId = requestId;

    QString validationError;
    if (!m_definition.isValid(&validationError)) {
        emit searchFailed(id(), validationError);
        emit requestFailed(requestId, validationError);
        return;
    }

    const QString urlStr = CustomHttpUtils::formatUrl(m_definition.urlTemplate, query);
    const QUrl url(urlStr);
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
        emit searchFailed(id(), QStringLiteral("Invalid destination URL scheme."));
        emit requestFailed(requestId, QStringLiteral("Invalid destination URL scheme."));
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup/1.0 (Custom HTTP Lookup)"));

    for (auto it = m_definition.headers.cbegin(); it != m_definition.headers.cend(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    if (!m_definition.authHeader.isEmpty() && !m_secretToken.isEmpty()) {
        QString authVal = m_definition.authSecretToken.isEmpty() ? m_secretToken : m_definition.authSecretToken;
        authVal.replace(QStringLiteral("{secret}"), m_secretToken);
        request.setRawHeader(m_definition.authHeader.toUtf8(), authVal.toUtf8());
    }

    QNetworkReply *reply = nullptr;
    if (m_definition.method == CustomHttpSourceDefinition::Method::Post) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, m_definition.postContentType);
        const QByteArray body = CustomHttpUtils::formatBody(m_definition.bodyTemplate, m_definition.postContentType, query);
        reply = m_nam->post(request, body);
    } else {
        reply = m_nam->get(request);
    }

    m_currentReply = reply;

    // Guard timeout of 10 seconds
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply] {
        if (reply && reply->isRunning()) {
            reply->abort();
        }
    });
    timer->start(10000);

    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId] {
        if (!reply || m_currentReply != reply) {
            return;
        }
        m_currentReply = nullptr;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() == QNetworkReply::OperationCanceledError) {
                return;
            }
            emit searchFailed(id(), reply->errorString());
            emit requestFailed(requestId, QStringLiteral("HTTP Request Failed: %1").arg(reply->errorString()));
            return;
        }

        // Bound to 5 MB maximum response size
        const QByteArray responseData = reply->read(5 * 1024 * 1024);
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
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
