// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef JSPROVIDER_H
#define JSPROVIDER_H

#include "providerapiv1.h"
#include "providerhostservice.h"
#include "providerpackage.h"

#include <QHash>
#include <QJSEngine>
#include <QJSValue>
#include <QObject>
#include <QSet>
#include <QString>

class JsHostBridge;

// QJSEngine provides in-process JavaScript execution within the KLookup application process.
// It is NOT an operating-system level sandbox. All executed scripts must be explicitly
// trusted by the user (isTrusted() == true). Capabilities and resource limits (origin matching,
// timeout cancellation, bounded streaming, secret filtering) are enforced cooperatively
// by ProviderHostService and JsHostBridge.
class JsProvider : public ProviderApiV1::ProviderV1
{
    Q_OBJECT
public:
    explicit JsProvider(const ProviderPackage &package,
                        QObject *parent = nullptr,
                        ProviderHost::ProviderHostService *hostService = nullptr);
    ~JsProvider() override;

    QString id() const override;
    QString name() const override;
    QString description() const override;
    ProviderApiV1::Capabilities capabilities() const override;

    bool isLoaded() const { return m_loaded; }
    QString loadError() const { return m_loadError; }

    quint64 search(const QString &query, quint64 requestId = 0) override;
    quint64 getPage(const QString &query, const QString &pageToken, quint64 requestId = 0) override;
    quint64 getDetails(const QString &resultId, quint64 requestId = 0) override;
    quint64 getContent(const QString &resultId, const QString &contentToken = QString(), quint64 requestId = 0) override;
    void cancel(quint64 requestId) override;
    ProviderV1 *clone(QObject *parent = nullptr) const override;

    quint64 currentOperationId() const { return m_currentOperationId; }
    void registerNetworkRequest(quint64 opId, quint64 netId);
    void unregisterNetworkRequest(quint64 netId);

private:
    bool initEngine();
    quint64 nextRequestId();

    ProviderPackage m_package;
    ProviderHost::ProviderHostService *m_hostService = nullptr;
    bool m_ownsHostService = false;

    QJSEngine *m_engine = nullptr;
    JsHostBridge *m_bridge = nullptr;
    QJSValue m_module;
    bool m_loaded = false;
    QString m_loadError;
    quint64 m_counter = 0;
    quint64 m_currentOperationId = 0;
    QMultiHash<quint64, quint64> m_opToNetIds;
    QHash<quint64, quint64> m_netToOpId;
    QSet<quint64> m_cancelledRequests;
};

#endif // JSPROVIDER_H
