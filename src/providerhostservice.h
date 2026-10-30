// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROVIDERHOSTSERVICE_H
#define PROVIDERHOSTSERVICE_H

#include "credentialstore.h"
#include "providerpackage.h"

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

namespace ProviderHost
{

struct HttpRequestOptions {
    QUrl url;
    QString method = QStringLiteral("GET");
    QMap<QString, QString> headers;
    QByteArray body;
    int timeoutMs = 10000;
    quint64 requestId = 0;
    QString secretKey;
    QString secretHeader;
};

struct HttpResponse {
    quint64 requestId = 0;
    int statusCode = 0;
    QString statusText;
    QMap<QString, QString> headers;
    QByteArray body;
    bool success = false;
    QString errorMessage;
    bool truncated = false;
};

class ProviderHostService : public QObject
{
    Q_OBJECT
public:
    explicit ProviderHostService(const ProviderPackage &package,
                                 QObject *parent = nullptr,
                                 QNetworkAccessManager *nam = nullptr);
    ~ProviderHostService() override;

    const ProviderPackage &package() const { return m_package; }
    void setPackage(const ProviderPackage &package);
    QNetworkAccessManager *networkAccessManager() const { return m_nam; }

    void setSecretResolver(std::function<QString(const QString &)> resolver)
    {
        m_secretResolver = std::move(resolver);
    }

    void setCredentialStore(CredentialStore *store)
    {
        m_credentialStore = store;
    }
    CredentialStore *credentialStore() const { return m_credentialStore; }

    bool isDiagnosticMode() const { return m_diagnosticMode; }
    void setDiagnosticPolicy(bool allowNetwork) { m_diagnosticMode = true; m_allowNetwork = allowNetwork; }

    quint64 executeHttpRequest(const HttpRequestOptions &options);
    void cancelRequest(quint64 requestId);
    void cancelAllRequests();

signals:
    void httpResponseReady(quint64 requestId, const HttpResponse &response);

private:
    struct ActiveRequest;
    using Request = std::shared_ptr<ActiveRequest>;
    bool current(const Request &request) const;
    void startRequest(const Request &request, const HttpRequestOptions &options, int hops, const QString &secret);
    bool drain(const Request &request);
    void complete(const Request &request);
    void finish(const Request &request, HttpResponse response);
    void fail(const Request &request, const QString &message, bool truncated = false);
    void retire(const Request &request);

    bool m_diagnosticMode = false;
    bool m_allowNetwork = true;
    ProviderPackage m_package;
    QPointer<QNetworkAccessManager> m_nam;
    quint64 m_requestCounter = 0;
    std::function<QString(const QString &)> m_secretResolver;
    QPointer<CredentialStore> m_credentialStore;
    QHash<quint64, Request> m_requests;
};

} // namespace ProviderHost

#endif // PROVIDERHOSTSERVICE_H
