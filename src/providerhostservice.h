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
    void setPackage(const ProviderPackage &package) { m_package = package; }
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

    quint64 executeHttpRequest(const HttpRequestOptions &options);
    void cancelRequest(quint64 requestId);
    void cancelAllRequests();

signals:
    void httpResponseReady(quint64 requestId, const HttpResponse &response);

private:
    struct ActiveReply {
        quint64 requestId = 0;
        QUrl requestUrl;
        HttpRequestOptions options;
        int hops = 0;
        QNetworkReply *reply = nullptr;
        std::shared_ptr<QByteArray> buffer;
        std::shared_ptr<bool> limitExceeded;
    };

    void startRequestInternal(quint64 reqId, const HttpRequestOptions &options, int hops, const QString &resolvedSecret);

    ProviderPackage m_package;
    QNetworkAccessManager *m_nam = nullptr;
    bool m_ownsNam = false;
    quint64 m_requestCounter = 0;
    std::function<QString(const QString &)> m_secretResolver;
    QPointer<CredentialStore> m_credentialStore;
    QHash<quint64, ActiveReply> m_activeRequests;
};

} // namespace ProviderHost

#endif // PROVIDERHOSTSERVICE_H
