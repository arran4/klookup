// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jsprovider.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

class JsPromiseHandler : public QObject
{
    Q_OBJECT
public:
    explicit JsPromiseHandler(QObject *parent = nullptr) : QObject(parent) {}

    std::function<void(const QJSValue &)> onResolve;
    std::function<void(const QString &)> onReject;

    Q_INVOKABLE void resolve(const QJSValue &val)
    {
        if (onResolve) onResolve(val);
        deleteLater();
    }

    Q_INVOKABLE void reject(const QString &err)
    {
        if (onReject) onReject(err);
        deleteLater();
    }
};

class JsHostBridge : public QObject
{
    Q_OBJECT
public:
    JsHostBridge(JsProvider *provider,
                 QJSEngine *engine,
                 ProviderHost::ProviderHostService *hostService,
                 QObject *parent = nullptr)
        : QObject(parent)
        , m_provider(provider)
        , m_engine(engine)
        , m_hostService(hostService)
    {
        if (m_hostService) {
            connect(m_hostService, &ProviderHost::ProviderHostService::httpResponseReady,
                    this, &JsHostBridge::onHttpResponseReady);
        }
    }

    Q_INVOKABLE QJSValue fetch(const QString &url, const QJSValue &options = QJSValue())
    {
        if (!m_engine || !m_hostService) {
            return m_engine ? m_engine->newErrorObject(QJSValue::GenericError, QStringLiteral("Host service unavailable")) : QJSValue();
        }

        const QJSValue deferred = m_engine->globalObject().property(QStringLiteral("createDeferred")).call();
        if (!deferred.isObject()) {
            return m_engine->newErrorObject(QJSValue::GenericError, QStringLiteral("Failed to create promise"));
        }

        ProviderHost::HttpRequestOptions opts;
        opts.url = QUrl(url);

        if (options.isObject()) {
            opts.method = options.property(QStringLiteral("method")).toString().toUpper();
            if (opts.method.isEmpty()) opts.method = QStringLiteral("GET");

            if (options.hasProperty(QStringLiteral("timeoutMs"))) {
                opts.timeoutMs = options.property(QStringLiteral("timeoutMs")).toInt();
            }

            if (options.hasProperty(QStringLiteral("headers"))) {
                const QJSValue headersVal = options.property(QStringLiteral("headers"));
                if (headersVal.isObject()) {
                    const QString jsonStr = m_engine->globalObject().property(QStringLiteral("JSON"))
                                                    .property(QStringLiteral("stringify")).call({ headersVal }).toString();
                    const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
                    if (doc.isObject()) {
                        const auto obj = doc.object();
                        for (auto hit = obj.constBegin(), hend = obj.constEnd(); hit != hend; ++hit) {
                            opts.headers.insert(hit.key(), hit.value().toString());
                        }
                    }
                }
            }

            if (options.hasProperty(QStringLiteral("body"))) {
                const auto bodyVal = options.property(QStringLiteral("body"));
                if (bodyVal.isString()) {
                    opts.body = bodyVal.toString().toUtf8();
                } else if (bodyVal.isObject()) {
                    opts.body = m_engine->globalObject().property(QStringLiteral("JSON"))
                                        .property(QStringLiteral("stringify")).call({ bodyVal }).toString().toUtf8();
                }
            }

            if (options.hasProperty(QStringLiteral("secretKey"))) {
                opts.secretKey = options.property(QStringLiteral("secretKey")).toString();
                opts.secretHeader = options.property(QStringLiteral("secretHeader")).toString();
            }
        }

        const quint64 netId = m_hostService->executeHttpRequest(opts);
        m_pendingFetches.insert(netId, deferred);

        if (m_provider && m_provider->currentOperationId() != 0) {
            m_provider->registerNetworkRequest(m_provider->currentOperationId(), netId);
        }

        return deferred;
    }

    Q_INVOKABLE QString getSecret(const QString &key)
    {
        if (!m_hostService) return QString();
        if (!m_hostService->package().isSecretAllowed(key)) {
            return QString();
        }
        return QString();
    }

    Q_INVOKABLE void log(const QString &msg)
    {
        qDebug() << "[JS Provider]" << msg;
    }

private:
    void onHttpResponseReady(quint64 id, const ProviderHost::HttpResponse &resp)
    {
        if (m_provider) {
            m_provider->unregisterNetworkRequest(id);
        }
        if (!m_pendingFetches.contains(id))
            return;
        QJSValue def = m_pendingFetches.take(id);
        if (!def.isObject() || !m_engine)
            return;

        if (resp.success) {
            QJSValue respObj = m_engine->newObject();
            respObj.setProperty(QStringLiteral("status"), resp.statusCode);
            respObj.setProperty(QStringLiteral("ok"), resp.statusCode >= 200 && resp.statusCode < 300);
            respObj.setProperty(QStringLiteral("statusText"), QStringLiteral("OK"));
            respObj.setProperty(QStringLiteral("body"), QString::fromUtf8(resp.body));

            QJSValue headersObj = m_engine->newObject();
            for (auto hit = resp.headers.cbegin(), hend = resp.headers.cend(); hit != hend; ++hit) {
                headersObj.setProperty(hit.key().toLower(), hit.value());
            }
            respObj.setProperty(QStringLiteral("headers"), headersObj);

            respObj.setProperty(QStringLiteral("text"), m_engine->evaluate(
                QStringLiteral("(function() { return Promise.resolve(this.body); })")));
            respObj.setProperty(QStringLiteral("json"), m_engine->evaluate(
                QStringLiteral("(function() { return Promise.resolve(JSON.parse(this.body)); })")));

            def.property(QStringLiteral("resolve")).callWithInstance(def, { respObj });
        } else {
            QJSValue errObj = m_engine->newErrorObject(QJSValue::GenericError, resp.errorMessage);
            def.property(QStringLiteral("reject")).callWithInstance(def, { errObj });
        }
    }

    JsProvider *m_provider = nullptr;
    QJSEngine *m_engine = nullptr;
    ProviderHost::ProviderHostService *m_hostService = nullptr;
    QHash<quint64, QJSValue> m_pendingFetches;
};

JsProvider::JsProvider(const ProviderPackage &package,
                       QObject *parent,
                       ProviderHost::ProviderHostService *hostService)
    : ProviderV1(parent)
    , m_package(package)
    , m_hostService(hostService)
{
    if (!m_hostService) {
        m_hostService = new ProviderHost::ProviderHostService(package, this);
        m_ownsHostService = true;
    }
    initEngine();
}

JsProvider::~JsProvider()
{
    if (m_hostService) {
        for (auto it = m_opToNetIds.constBegin(); it != m_opToNetIds.constEnd(); ++it) {
            m_hostService->cancelRequest(it.value());
        }
    }
    m_opToNetIds.clear();
    m_netToOpId.clear();
    if (m_ownsHostService && m_hostService) {
        delete m_hostService;
        m_hostService = nullptr;
    }
}

ProviderApiV1::ProviderV1 *JsProvider::clone(QObject *parent) const
{
    auto *clonedHost = new ProviderHost::ProviderHostService(m_package, parent, m_hostService ? m_hostService->networkAccessManager() : nullptr);
    return new JsProvider(m_package, parent, clonedHost);
}

void JsProvider::registerNetworkRequest(quint64 opId, quint64 netId)
{
    if (opId != 0 && netId != 0) {
        m_opToNetIds.insert(opId, netId);
        m_netToOpId.insert(netId, opId);
    }
}

void JsProvider::unregisterNetworkRequest(quint64 netId)
{
    if (m_netToOpId.contains(netId)) {
        const quint64 opId = m_netToOpId.take(netId);
        m_opToNetIds.remove(opId, netId);
    }
}

QString JsProvider::id() const
{
    return m_package.id();
}

QString JsProvider::name() const
{
    return m_package.name();
}

QString JsProvider::description() const
{
    return m_package.manifest().description;
}

ProviderApiV1::Capabilities JsProvider::capabilities() const
{
    return m_package.manifest().capabilities;
}

quint64 JsProvider::nextRequestId()
{
    return ++m_counter;
}

bool JsProvider::initEngine()
{
    if (!m_package.isValid()) {
        m_loadError = m_package.validationError();
        return false;
    }

    if (!m_package.isTrusted()) {
        m_loadError = QStringLiteral("Untrusted provider package cannot execute code.");
        return false;
    }

    m_engine = new QJSEngine(this);
    m_bridge = new JsHostBridge(this, m_engine, m_hostService, this);

    // Install host API and promise helpers
    m_engine->globalObject().setProperty(QStringLiteral("host"), m_engine->newQObject(m_bridge));
    auto evalRes = m_engine->evaluate(QStringLiteral(
        "function createDeferred() {\n"
        "    var res, rej;\n"
        "    var p = new Promise(function(resolve, reject) { res = resolve; rej = reject; });\n"
        "    p.resolve = res;\n"
        "    p.reject = rej;\n"
        "    return p;\n"
        "}\n"
        "function attachPromise(p, onRes, onRej) {\n"
        "    if (p && typeof p.then === 'function') {\n"
        "        p.then(function(val) { onRes(val); }, function(err) { onRej(err ? (err.message || String(err)) : 'Error'); });\n"
        "    } else {\n"
        "        onRes(p);\n"
        "    }\n"
        "}\n"
    ));
    if (evalRes.isError()) {
        qWarning() << "EVAL ERROR:" << evalRes.toString();
    }

    // Import ECMAScript module (.mjs)
    m_module = m_engine->importModule(m_package.entryFilePath());
    if (m_module.isError()) {
        m_loadError = QStringLiteral("Error importing module '%1': %2")
                          .arg(m_package.entryFilePath(), m_module.toString());
        return false;
    }

    if (!m_module.property(QStringLiteral("search")).isCallable()) {
        m_loadError = QStringLiteral("Module does not export a callable 'search' function.");
        return false;
    }

    m_loaded = true;
    return true;
}

quint64 JsProvider::search(const QString &query, quint64 requestId)
{
    const quint64 reqId = requestId != 0 ? requestId : nextRequestId();

    if (!m_loaded) {
        QTimer::singleShot(0, this, [this, reqId] {
            emit pageFailed(reqId, ProviderApiV1::ErrorCode::InternalError, m_loadError);
        });
        return reqId;
    }

    m_currentOperationId = reqId;

    auto *handler = new JsPromiseHandler(this);
    handler->onResolve = [this, reqId](const QJSValue &val) {
        m_opToNetIds.remove(reqId);
        if (m_cancelledRequests.contains(reqId)) {
            m_cancelledRequests.remove(reqId);
            return;
        }

        const QString jsonStr = m_engine->globalObject().property(QStringLiteral("JSON"))
                                        .property(QStringLiteral("stringify")).call({ val }).toString();
        const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());

        SearchPage page;
        if (doc.isArray()) {
            for (const auto &rv : doc.array()) {
                if (rv.isObject()) {
                    page.results.append(ProviderApiV1::searchResultFromJson(rv.toObject()));
                }
            }
        } else if (doc.isObject()) {
            page = ProviderApiV1::searchPageFromJson(doc.object());
        }

        emit pageReady(reqId, page);
    };

    handler->onReject = [this, reqId](const QString &err) {
        m_opToNetIds.remove(reqId);
        if (m_cancelledRequests.contains(reqId)) {
            m_cancelledRequests.remove(reqId);
            return;
        }
        emit pageFailed(reqId, ProviderApiV1::ErrorCode::InternalError, err);
    };

    const QJSValue resVal = m_module.property(QStringLiteral("search")).call({ query });
    if (resVal.isError()) {
        emit pageFailed(reqId, ProviderApiV1::ErrorCode::InternalError, resVal.toString());
        delete handler;
        return reqId;
    }

    QJSValue handlerObj = m_engine->newQObject(handler);
    auto attachFn = m_engine->globalObject().property(QStringLiteral("attachPromise"));
    if (!attachFn.isCallable()) {
        qWarning() << "attachPromise is NOT callable!";
    }
    auto attachRes = attachFn.call({
        resVal,
        handlerObj.property(QStringLiteral("resolve")),
        handlerObj.property(QStringLiteral("reject"))
    });
    if (attachRes.isError()) {
        qWarning() << "ATTACH ERROR:" << attachRes.toString();
    }

    return reqId;
}

quint64 JsProvider::getPage(const QString &query, const QString &pageToken, quint64 requestId)
{
    const quint64 reqId = requestId != 0 ? requestId : nextRequestId();

    if (!m_loaded) {
        QTimer::singleShot(0, this, [this, reqId] {
            emit pageFailed(reqId, ProviderApiV1::ErrorCode::InternalError, m_loadError);
        });
        return reqId;
    }

    if (!m_module.property(QStringLiteral("getPage")).isCallable()) {
        if (pageToken.isEmpty() && m_module.property(QStringLiteral("search")).isCallable()) {
            return search(query, reqId);
        }
        QTimer::singleShot(0, this, [this, reqId] {
            emit pageFailed(reqId, ProviderApiV1::ErrorCode::MethodNotFound, QStringLiteral("getPage is not exported by module"));
        });
        return reqId;
    }

    m_currentOperationId = reqId;

    auto *handler = new JsPromiseHandler(this);
    handler->onResolve = [this, reqId](const QJSValue &val) {
        m_opToNetIds.remove(reqId);
        if (m_cancelledRequests.contains(reqId)) {
            m_cancelledRequests.remove(reqId);
            return;
        }

        const QString jsonStr = m_engine->globalObject().property(QStringLiteral("JSON"))
                                        .property(QStringLiteral("stringify")).call({ val }).toString();
        const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());

        SearchPage page;
        if (doc.isArray()) {
            for (const auto &rv : doc.array()) {
                if (rv.isObject()) {
                    page.results.append(ProviderApiV1::searchResultFromJson(rv.toObject()));
                }
            }
        } else if (doc.isObject()) {
            page = ProviderApiV1::searchPageFromJson(doc.object());
        }

        emit pageReady(reqId, page);
    };

    handler->onReject = [this, reqId](const QString &err) {
        m_opToNetIds.remove(reqId);
        if (m_cancelledRequests.contains(reqId)) {
            m_cancelledRequests.remove(reqId);
            return;
        }
        emit pageFailed(reqId, ProviderApiV1::ErrorCode::InternalError, err);
    };

    const QJSValue resVal = m_module.property(QStringLiteral("getPage")).call({ query, pageToken });
    if (resVal.isError()) {
        emit pageFailed(reqId, ProviderApiV1::ErrorCode::InternalError, resVal.toString());
        delete handler;
        return reqId;
    }

    QJSValue handlerObj = m_engine->newQObject(handler);
    auto attachFn = m_engine->globalObject().property(QStringLiteral("attachPromise"));
    if (!attachFn.isCallable()) {
        qWarning() << "attachPromise is NOT callable!";
    }
    auto attachRes = attachFn.call({
        resVal,
        handlerObj.property(QStringLiteral("resolve")),
        handlerObj.property(QStringLiteral("reject"))
    });
    if (attachRes.isError()) {
        qWarning() << "ATTACH ERROR:" << attachRes.toString();
    }

    return reqId;
}

quint64 JsProvider::getDetails(const QString &resultId, quint64 requestId)
{
    const quint64 reqId = requestId != 0 ? requestId : nextRequestId();

    if (!m_loaded) {
        QTimer::singleShot(0, this, [this, reqId, resultId] {
            emit detailsFailed(reqId, resultId, ProviderApiV1::ErrorCode::InternalError, m_loadError);
        });
        return reqId;
    }

    if (!m_module.property(QStringLiteral("getDetails")).isCallable()) {
        QTimer::singleShot(0, this, [this, reqId, resultId] {
            emit detailsFailed(reqId, resultId, ProviderApiV1::ErrorCode::MethodNotFound, QStringLiteral("getDetails is not exported by module"));
        });
        return reqId;
    }

    m_currentOperationId = reqId;

    auto *handler = new JsPromiseHandler(this);
    handler->onResolve = [this, reqId, resultId](const QJSValue &val) {
        m_opToNetIds.remove(reqId);
        if (m_cancelledRequests.contains(reqId)) {
            m_cancelledRequests.remove(reqId);
            return;
        }

        const QString jsonStr = m_engine->globalObject().property(QStringLiteral("JSON"))
                                        .property(QStringLiteral("stringify")).call({ val }).toString();
        const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());

        SearchDetails details;
        if (doc.isObject()) {
            details = ProviderApiV1::searchDetailsFromJson(doc.object());
            if (details.result.id.isEmpty()) {
                details.result.id = resultId;
            }
        }

        emit detailsReady(reqId, resultId, details);
    };

    handler->onReject = [this, reqId, resultId](const QString &err) {
        m_opToNetIds.remove(reqId);
        if (m_cancelledRequests.contains(reqId)) {
            m_cancelledRequests.remove(reqId);
            return;
        }
        emit detailsFailed(reqId, resultId, ProviderApiV1::ErrorCode::InternalError, err);
    };

    const QJSValue resVal = m_module.property(QStringLiteral("getDetails")).call({ resultId });
    if (resVal.isError()) {
        emit detailsFailed(reqId, resultId, ProviderApiV1::ErrorCode::InternalError, resVal.toString());
        delete handler;
        return reqId;
    }

    QJSValue handlerObj = m_engine->newQObject(handler);
    auto attachFn = m_engine->globalObject().property(QStringLiteral("attachPromise"));
    if (!attachFn.isCallable()) {
        qWarning() << "attachPromise is NOT callable!";
    }
    auto attachRes = attachFn.call({
        resVal,
        handlerObj.property(QStringLiteral("resolve")),
        handlerObj.property(QStringLiteral("reject"))
    });
    if (attachRes.isError()) {
        qWarning() << "ATTACH ERROR:" << attachRes.toString();
    }

    return reqId;
}

quint64 JsProvider::getContent(const QString &resultId, const QString &contentToken, quint64 requestId)
{
    const quint64 reqId = requestId != 0 ? requestId : nextRequestId();

    if (!m_loaded) {
        QTimer::singleShot(0, this, [this, reqId, resultId] {
            emit contentFailed(reqId, resultId, ProviderApiV1::ErrorCode::InternalError, m_loadError);
        });
        return reqId;
    }

    if (!m_module.property(QStringLiteral("getContent")).isCallable()) {
        // Fallback: check if getDetails is available
        if (m_module.property(QStringLiteral("getDetails")).isCallable()) {
            connect(this, &ProviderV1::detailsReady, this, [this, reqId, resultId](quint64 dId, const QString &rId, const SearchDetails &details) {
                if (dId == reqId && rId == resultId) {
                    ProviderApiV1::ContentResponse resp;
                    resp.resultId = resultId;
                    resp.content = details.content;
                    emit contentReady(reqId, resp);
                }
            });
            getDetails(resultId, reqId);
            return reqId;
        }

        QTimer::singleShot(0, this, [this, reqId, resultId] {
            emit contentFailed(reqId, resultId, ProviderApiV1::ErrorCode::MethodNotFound, QStringLiteral("getContent is not exported by module"));
        });
        return reqId;
    }

    m_currentOperationId = reqId;

    auto *handler = new JsPromiseHandler(this);
    handler->onResolve = [this, reqId, resultId](const QJSValue &val) {
        m_opToNetIds.remove(reqId);
        if (m_cancelledRequests.contains(reqId)) {
            m_cancelledRequests.remove(reqId);
            return;
        }

        ProviderApiV1::ContentResponse resp;
        resp.resultId = resultId;

        if (val.isString()) {
            resp.content.format = ResultContent::Format::Markdown;
            resp.content.description = val.toString();
        } else {
            const QString jsonStr = m_engine->globalObject().property(QStringLiteral("JSON"))
                                            .property(QStringLiteral("stringify")).call({ val }).toString();
            const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (doc.isObject()) {
                resp = ProviderApiV1::contentResponseFromJson(doc.object());
                if (resp.resultId.isEmpty()) {
                    resp.resultId = resultId;
                }
            }
        }

        emit contentReady(reqId, resp);
    };

    handler->onReject = [this, reqId, resultId](const QString &err) {
        m_opToNetIds.remove(reqId);
        if (m_cancelledRequests.contains(reqId)) {
            m_cancelledRequests.remove(reqId);
            return;
        }
        emit contentFailed(reqId, resultId, ProviderApiV1::ErrorCode::InternalError, err);
    };

    const QJSValue resVal = m_module.property(QStringLiteral("getContent")).call({ resultId, contentToken });
    if (resVal.isError()) {
        emit contentFailed(reqId, resultId, ProviderApiV1::ErrorCode::InternalError, resVal.toString());
        delete handler;
        return reqId;
    }

    QJSValue handlerObj = m_engine->newQObject(handler);
    auto attachFn = m_engine->globalObject().property(QStringLiteral("attachPromise"));
    if (!attachFn.isCallable()) {
        qWarning() << "attachPromise is NOT callable!";
    }
    auto attachRes = attachFn.call({
        resVal,
        handlerObj.property(QStringLiteral("resolve")),
        handlerObj.property(QStringLiteral("reject"))
    });
    if (attachRes.isError()) {
        qWarning() << "ATTACH ERROR:" << attachRes.toString();
    }

    return reqId;
}

void JsProvider::cancel(quint64 requestId)
{
    m_cancelledRequests.insert(requestId);
    if (m_hostService) {
        const auto netIds = m_opToNetIds.values(requestId);
        for (quint64 netId : netIds) {
            m_hostService->cancelRequest(netId);
            m_netToOpId.remove(netId);
        }
        m_opToNetIds.remove(requestId);
    }
}

#include "jsprovider.moc"
