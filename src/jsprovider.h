// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef JSPROVIDER_H
#define JSPROVIDER_H

#include "providerapiv1.h"
#include "providerhostservice.h"
#include "providerpackage.h"
#include <QElapsedTimer>
#include <QHash>
#include <QJSEngine>
#include <QJSValue>
#include <QPointer>
#include <QTemporaryDir>
#include <memory>

class JsHostBridge;
class JsPromiseHandler;
class JsExecutionWatchdog;
class JsInterruptionGate;
class QTimer;

// Trusted in-process execution, not an OS sandbox. Package approval, host
// permissions, operation deadlines and a CPU watchdog are separate boundaries.
class JsProvider : public ProviderApiV1::ProviderV1
{
    Q_OBJECT
public:
    explicit JsProvider(const ProviderPackage &package, QObject *parent = nullptr,
                        ProviderHost::ProviderHostService *hostService = nullptr);
    ~JsProvider() override;
    QString id() const override;
    QString name() const override;
    QString description() const override;
    ProviderApiV1::Capabilities capabilities() const override;
    bool isLoaded() const { return m_loaded; }
    QString loadError() const { return m_loadError; }
    quint64 check(bool live = false, quint64 requestId = 0);
    bool hasCheck(bool live = false) const;

signals:
    void checkReady(quint64 requestId, const QJsonObject &result);
    void checkFailed(quint64 requestId, ProviderApiV1::ErrorCode code);

public:
    quint64 search(const QString &query, quint64 requestId = 0) override;
    quint64 getPage(const QString &query, const QString &pageToken, quint64 requestId = 0) override;
    quint64 getDetails(const QString &resultId, quint64 requestId = 0) override;
    quint64 getContent(const QString &resultId, const QString &contentToken = {}, quint64 requestId = 0) override;
    void cancel(quint64 requestId) override;
    void invalidate();
    ProviderV1 *clone(QObject *parent = nullptr) const override;

    // Wall lifetime includes time awaiting promises/HTTP. CPU time is separately
    // capped at 100 ms per operation, across all its promise callbacks.
    void setOperationTimeoutMs(int timeoutMs);
    int activeOperationCount() const { return m_operations.size(); }
    int pendingFetchCount() const;
    int retainedPromiseCount() const;
    quint64 currentOperationId() const { return m_currentOperationToken; }
    bool operationActive(quint64 token) const;
    quint64 enterOperation(quint64 token);
    void leaveOperation(quint64 previousToken);
    void registerNetworkRequest(quint64 token, quint64 netId);
    void unregisterNetworkRequest(quint64 netId);

protected:
    bool event(QEvent *event) override;

private:
    friend class JsHostBridge;
    friend class JsInterruptionGate;
    enum class Kind { Page, Details, Content, ContentFromDetails, Check };
    struct Operation {
        Kind kind = Kind::Page;
        quint64 token = 0;
        QString resultId;
        QPointer<JsPromiseHandler> handler;
        QTimer *deadline = nullptr;
        qint64 cpuNanoseconds = 0;
    };
    bool initEngine();
    quint64 invoke(const QString &method, const QJSValueList &arguments, Kind kind,
                   const QString &resultId, quint64 requestId);
    quint64 nextRequestId();
    Operation *operationForToken(quint64 token);
    void complete(quint64 token, const QJSValue &value);
    void fail(quint64 token, ProviderApiV1::ErrorCode code, const QString &message);
    void release(quint64 requestId);
    void executionInterrupted();
    void disposeEngine();
    void emitFailure(quint64 requestId, quint64 token, Kind kind, const QString &resultId,
                     ProviderApiV1::ErrorCode code, const QString &message);
    void postTerminal(quint64 requestId, quint64 token, std::function<void()> callback);

    ProviderPackage m_package;
    QTemporaryDir m_snapshot;
    QPointer<ProviderHost::ProviderHostService> m_hostService;
    QJSEngine *m_engine = nullptr;
    JsHostBridge *m_bridge = nullptr;
    std::unique_ptr<JsExecutionWatchdog> m_watchdog;
    QJSValue m_module;
    QJSValue m_helpers;
    bool m_loaded = false;
    bool m_disposalPosted = false;
    bool m_registeredWithGate = false;
    QString m_loadError;
    quint64 m_counter = 0;
    quint64 m_executionCounter = 0;
    quint64 m_currentOperationToken = 0;
    int m_operationTimeoutMs = 10000;
    qint64 m_executionStarted = -1;
    QHash<quint64, Operation> m_operations;
    QHash<quint64, quint64> m_tokens;
    QHash<quint64, quint64> m_terminals;
    QMultiHash<quint64, quint64> m_opToNetIds;
    QHash<quint64, quint64> m_netToOpId;
};
#endif
