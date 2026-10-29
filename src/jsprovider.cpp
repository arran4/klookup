// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jsprovider.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

// Only setInterrupted() crosses threads; Qt explicitly documents it thread-safe.
// Stop/join precedes engine destruction. No QJSValue is accessed by this thread.
class JsExecutionWatchdog {
public:
    JsExecutionWatchdog(QJSEngine *engine, QObject *context, std::function<void()> expired)
        : m_thread([this, engine, context, expired] {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (!m_stopping) {
                if (!m_armed) { m_changed.wait(lock); continue; }
                const auto generation = m_generation;
                if (m_changed.wait_until(lock, m_deadline, [this, generation] {
                    return m_stopping || generation != m_generation;
                })) continue;
                m_armed = false;
                engine->setInterrupted(true);
                QMetaObject::invokeMethod(context, expired, Qt::QueuedConnection);
            }
        }) {}
    ~JsExecutionWatchdog() {
        { std::lock_guard<std::mutex> lock(m_mutex); m_stopping = true; ++m_generation; }
        m_changed.notify_all();
        m_thread.join();
    }
    void arm(qint64 nanoseconds) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(qMax<qint64>(1, nanoseconds));
        m_armed = true;
        ++m_generation;
        m_changed.notify_all();
    }
    void disarm() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_armed = false;
        ++m_generation;
        m_changed.notify_all();
    }
private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_stopping = false;
    bool m_armed = false;
    quint64 m_generation = 0;
    std::chrono::steady_clock::time_point m_deadline;
    std::thread m_thread;
};

class JsPromiseHandler : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    std::function<void(const QJSValue &)> onResolve;
    std::function<void(const QString &)> onReject;
    void disable() { onResolve = {}; onReject = {}; }
    Q_INVOKABLE void resolve(const QJSValue &value) {
        auto callback = std::move(onResolve);
        disable();
        if (callback) callback(value);
        deleteLater();
    }
    Q_INVOKABLE void reject(const QString &error) {
        auto callback = std::move(onReject);
        disable();
        if (callback) callback(error);
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
        if (!m_provider || !m_provider->operationActive(m_provider->currentOperationId())) {
            return m_engine->evaluate(QStringLiteral("Promise.reject(new Error('host.fetch requires an active exported operation'))"));
        }
        if (!m_engine || !m_hostService) {
            return m_engine ? m_engine->newErrorObject(QJSValue::GenericError, QStringLiteral("Host service unavailable")) : QJSValue();
        }

        const QJSValue deferred = m_provider->m_helpers.property(QStringLiteral("createDeferred")).call();
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
        m_pendingFetches.insert(netId, {m_provider->currentOperationId(), deferred});

        if (m_provider && m_provider->currentOperationId() != 0) {
            m_provider->registerNetworkRequest(m_provider->currentOperationId(), netId);
        }

        return deferred.property(QStringLiteral("promise"));
    }

    Q_INVOKABLE quint64 operationToken() const { return m_provider->currentOperationId(); }
    Q_INVOKABLE bool operationActive(quint64 token) const { return m_provider->operationActive(token); }
    Q_INVOKABLE quint64 enterOperation(quint64 token) { return m_provider->enterOperation(token); }
    Q_INVOKABLE void leaveOperation(quint64 previous) { m_provider->leaveOperation(previous); }
    void dropRequest(quint64 id) { m_pendingFetches.remove(id); }
    Q_INVOKABLE quint64 promiseToken(const QJSValue &promise) const {
        for (const auto &owner : m_ownedPromises) if (owner.first.strictlyEquals(promise)) return owner.second;
        return 0;
    }
    Q_INVOKABLE QJSValue claimPromise(const QJSValue &promise, quint64 token) {
        if (!m_provider->operationActive(token)) return promise;
        for (auto &owner : m_ownedPromises) if (owner.first.strictlyEquals(promise)) { owner.second = token; return promise; }
        m_ownedPromises.append({promise, token});
        return promise;
    }
    void dropOperation(quint64 token) {
        for (auto it = m_ownedPromises.begin(); it != m_ownedPromises.end();) {
            if (it->second == token) it = m_ownedPromises.erase(it); else ++it;
        }
    }
    int pendingFetchCount() const { return m_pendingFetches.size(); }
    int retainedPromiseCount() const { return m_ownedPromises.size(); }

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
        auto pending = m_pendingFetches.take(id);
        QJSValue def = pending.deferred;
        if (!def.isObject() || !m_engine || !m_provider->operationActive(pending.token)) return;
        const auto previous = m_provider->enterOperation(pending.token);
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
        m_provider->leaveOperation(previous);
    }

    JsProvider *m_provider = nullptr;
    QJSEngine *m_engine = nullptr;
    QPointer<ProviderHost::ProviderHostService> m_hostService;
    struct PendingFetch { quint64 token; QJSValue deferred; };
    QHash<quint64, PendingFetch> m_pendingFetches;
    // Root promise identities until termination. This avoids the crash observed
    // with Qt 6.11's WeakMap in the promise-storm regression fixture.
    QList<QPair<QJSValue, quint64>> m_ownedPromises;
};

JsProvider::JsProvider(const ProviderPackage &package, QObject *parent, ProviderHost::ProviderHostService *hostService)
    : ProviderV1(parent), m_package(package), m_hostService(hostService)
{
    if (!m_hostService) m_hostService = new ProviderHost::ProviderHostService(package, this);
    initEngine();
}

JsProvider::~JsProvider()
{
    m_loaded = false;
    const auto ids = m_operations.keys();
    for (quint64 id : ids) release(id);
    m_watchdog.reset(); // Join before deleting QJSEngine or any QJSValue.
    delete m_bridge;
    m_bridge = nullptr;
    m_module = {};
    m_helpers = {};
    delete m_engine;
    m_engine = nullptr;
}

ProviderApiV1::ProviderV1 *JsProvider::clone(QObject *parent) const
{
    auto *copy = new JsProvider(m_package, parent);
    copy->setOperationTimeoutMs(m_operationTimeoutMs);
    return copy;
}

QString JsProvider::id() const { return m_package.id(); }
QString JsProvider::name() const { return m_package.name(); }
QString JsProvider::description() const { return m_package.manifest().description; }
ProviderApiV1::Capabilities JsProvider::capabilities() const { return m_package.manifest().capabilities; }
int JsProvider::pendingFetchCount() const { return m_bridge ? m_bridge->pendingFetchCount() : 0; }
int JsProvider::retainedPromiseCount() const { return m_bridge ? m_bridge->retainedPromiseCount() : 0; }
void JsProvider::setOperationTimeoutMs(int value) { m_operationTimeoutMs = qBound(1, value, 30000); }
quint64 JsProvider::nextRequestId() { do { ++m_counter; } while (m_operations.contains(m_counter)); return m_counter; }

JsProvider::Operation *JsProvider::operationForToken(quint64 token)
{
    const auto found = m_tokens.constFind(token);
    if (found == m_tokens.cend()) return nullptr;
    auto it = m_operations.find(*found);
    return it == m_operations.end() || it->token != token ? nullptr : &it.value();
}

bool JsProvider::operationActive(quint64 token) const { return token != 0 && m_tokens.contains(token); }

quint64 JsProvider::enterOperation(quint64 token)
{
    const auto previous = m_currentOperationToken;
    if (auto *old = operationForToken(previous); old && m_executionElapsed.isValid()) old->cpuNanoseconds += m_executionElapsed.nsecsElapsed();
    m_currentOperationToken = token;
    m_executionElapsed.start();
    if (auto *op = operationForToken(token)) m_watchdog->arm(100000000 - op->cpuNanoseconds);
    return previous;
}

void JsProvider::leaveOperation(quint64 previous)
{
    if (auto *op = operationForToken(m_currentOperationToken); op && m_executionElapsed.isValid()) {
        op->cpuNanoseconds += m_executionElapsed.nsecsElapsed();
        if (op->cpuNanoseconds >= 100000000) m_engine->setInterrupted(true);
    }
    m_currentOperationToken = previous;
    m_executionElapsed.start();
    if (auto *op = operationForToken(previous)) m_watchdog->arm(100000000 - op->cpuNanoseconds);
    else m_watchdog->disarm();
    if (m_engine->isInterrupted()) executionInterrupted();
}

void JsProvider::registerNetworkRequest(quint64 token, quint64 netId)
{
    if (operationActive(token) && netId) { m_opToNetIds.insert(token, netId); m_netToOpId.insert(netId, token); }
}

void JsProvider::unregisterNetworkRequest(quint64 netId)
{
    const auto token = m_netToOpId.take(netId);
    if (token) m_opToNetIds.remove(token, netId);
}

void JsProvider::release(quint64 id)
{
    auto found = m_operations.find(id);
    if (found == m_operations.end()) return;
    const Operation op = found.value();
    m_operations.erase(found);
    m_tokens.remove(op.token);
    if (op.deadline) { op.deadline->stop(); op.deadline->deleteLater(); }
    if (op.handler) { op.handler->disable(); op.handler->deleteLater(); }
    if (m_bridge) m_bridge->dropOperation(op.token);
    const auto requests = m_opToNetIds.values(op.token);
    m_opToNetIds.remove(op.token);
    for (quint64 net : requests) {
        m_netToOpId.remove(net);
        if (m_bridge) m_bridge->dropRequest(net);
        if (m_hostService) m_hostService->cancelRequest(net);
    }
}

void JsProvider::cancel(quint64 requestId) { m_terminals.remove(requestId); release(requestId); }

void JsProvider::postTerminal(quint64 id, quint64 token, std::function<void()> callback)
{
    // Never notify application code while QJSEngine is on its JS stack: a
    // consumer may synchronously close its tab and destroy this provider.
    m_terminals.insert(id, token);
    QTimer::singleShot(0, this, [this, id, token, callback = std::move(callback)] {
        if (m_terminals.value(id) != token) return;
        m_terminals.remove(id);
        callback();
    });
}

void JsProvider::emitFailure(quint64 id, quint64 token, Kind kind, const QString &resultId,
                             ProviderApiV1::ErrorCode code, const QString &message)
{
    postTerminal(id, token, [this, id, kind, resultId, code, message] {
        if (kind == Kind::Page) emit pageFailed(id, code, message);
        else if (kind == Kind::Details) emit detailsFailed(id, resultId, code, message);
        else emit contentFailed(id, resultId, code, message);
    });
}

void JsProvider::fail(quint64 token, ProviderApiV1::ErrorCode code, const QString &message)
{
    const auto *op = operationForToken(token);
    if (!op) return;
    const auto requestId = m_tokens.value(token);
    const auto kind = op->kind;
    const auto resultId = op->resultId;
    release(requestId);
    emitFailure(requestId, token, kind, resultId, code, message);
}

void JsProvider::invalidate()
{
    m_terminals.clear(); // Suppress results completed just before reload/revocation.
    m_loaded = false;
    m_loadError = QStringLiteral("Package disabled, revoked or reloaded; start a new search");
    const auto tokens = m_tokens.keys();
    for (auto token : tokens) fail(token, ProviderApiV1::ErrorCode::Cancelled, m_loadError);
    m_package.setTrusted(false);
    if (m_hostService) m_hostService->setPackage(m_package);
}

void JsProvider::executionInterrupted()
{
    if (!m_engine || !m_engine->isInterrupted()) return;
    m_loaded = false;
    m_loadError = QStringLiteral("Script execution exceeded its CPU budget; retry the provider");
    const auto tokens = m_tokens.keys();
    for (auto token : tokens) fail(token, ProviderApiV1::ErrorCode::Timeout, m_loadError);
}

bool JsProvider::initEngine()
{
    if (!m_package.isValid()) { m_loadError = m_package.validationError(); return false; }
    if (!m_package.isTrusted()) { m_loadError = QStringLiteral("Untrusted provider package cannot execute code."); return false; }
    if (!m_snapshot.isValid() || !m_package.writeVerifiedSnapshot(m_snapshot.path(), &m_loadError)) return false;
    m_engine = new QJSEngine(this);
    m_watchdog = std::make_unique<JsExecutionWatchdog>(m_engine, this, [this] { executionInterrupted(); });
    m_bridge = new JsHostBridge(this, m_engine, m_hostService, this);
    m_engine->globalObject().setProperty(QStringLiteral("__klookupBridge"), m_engine->newQObject(m_bridge));
    m_watchdog->arm(250000000);
    auto helpers = m_engine->evaluate(QStringLiteral(R"JS(
        (function(bridge) {
            var NativePromise = Promise;
            var nativeThen = NativePromise.prototype.then;
            function claim(promise, token) { return bridge.claimPromise(promise, token); }
            function OperationPromise(executor) {
                if (!(this instanceof OperationPromise)) throw new TypeError('Promise requires new');
                return claim(new NativePromise(executor), bridge.operationToken());
            }
            OperationPromise.prototype = NativePromise.prototype;
            Object.defineProperty(OperationPromise.prototype, 'constructor', {value: OperationPromise});
            ['resolve', 'reject', 'all', 'race', 'allSettled', 'any'].forEach(function(method) {
                if (typeof NativePromise[method] !== 'function') return;
                Object.defineProperty(OperationPromise, method, {value: function(value) {
                    return claim(NativePromise[method].call(NativePromise, value), bridge.operationToken());
                }});
            });
            Object.defineProperty(this, 'Promise', {value: OperationPromise, writable: false, configurable: false});
            // Module code sees only the public host API. Operation accounting
            // and the original Promise functions stay in this private closure.
            Object.defineProperty(this, 'host', {writable: false, configurable: false,
                value: Object.freeze({
                    fetch: function(url, options) { return bridge.fetch(url, options); },
                    getSecret: function(key) { return bridge.getSecret(key); },
                    log: function(message) { bridge.log(message); }
                })});
            function bind(fn, token) {
                if (typeof fn !== 'function') return fn;
                return function(value) {
                    if (!bridge.operationActive(token)) return;
                    var previous = bridge.enterOperation(token);
                    try { return fn(value); }
                    finally { bridge.leaveOperation(previous); }
                };
            }
            Object.defineProperty(OperationPromise.prototype, 'then', {configurable: false, writable: false,
                value: function(onResolved, onRejected) {
                    // Assimilation calls then() in a later engine job. Keep
                    // ownership on promises as well as registered callbacks.
                    var token = bridge.operationToken() || bridge.promiseToken(this) || 0;
                    return claim(nativeThen.call(this, bind(onResolved, token), bind(onRejected, token)), token);
                }});
            return {
                createDeferred: function() {
                    var resolve, reject;
                    var promise = new NativePromise(function(a, b) { resolve = a; reject = b; });
                    return {promise: claim(promise, bridge.operationToken()), resolve: resolve, reject: reject};
                },
                attachPromise: function(p, resolve, reject) {
                    if (p && typeof p.then === 'function') p.then(resolve, function(error) {
                        reject(error ? (error.message || String(error)) : 'Script error');
                    });
                    else resolve(p);
                }
            };
        })(__klookupBridge)
    )JS"));
    m_engine->globalObject().deleteProperty(QStringLiteral("__klookupBridge"));
    m_helpers = helpers;
    if (helpers.isError()) { m_loadError = helpers.toString(); m_watchdog->disarm(); return false; }
    m_module = m_engine->importModule(QDir(m_snapshot.path()).filePath(QDir(m_package.packageDir()).relativeFilePath(m_package.entryFilePath())));
    m_watchdog->disarm();
    if (m_engine->isInterrupted()) { m_loadError = QStringLiteral("Module initialization exceeded its CPU budget"); return false; }
    if (m_module.isError()) { m_loadError = QStringLiteral("Error importing module: %1").arg(m_module.toString()); return false; }
    if (!m_module.property(QStringLiteral("search")).isCallable()) { m_loadError = QStringLiteral("Module does not export callable search"); return false; }
    m_loaded = true;
    return true;
}

quint64 JsProvider::invoke(const QString &method, const QJSValueList &arguments, Kind kind,
                           const QString &resultId, quint64 requestedId)
{
    const quint64 id = requestedId ? requestedId : nextRequestId();
    cancel(id);
    Operation op;
    op.token = ++m_executionCounter;
    op.kind = kind;
    op.resultId = resultId;
    op.deadline = new QTimer(this);
    op.deadline->setSingleShot(true);
    const auto token = op.token;
    connect(op.deadline, &QTimer::timeout, this, [this, token] {
        fail(token, ProviderApiV1::ErrorCode::Timeout, QStringLiteral("Provider operation deadline exceeded"));
    });
    op.deadline->start(m_operationTimeoutMs);
    m_operations.insert(id, op);
    m_tokens.insert(token, id);
    if (!m_loaded || !m_module.property(method).isCallable()) {
        const auto code = m_loaded ? ProviderApiV1::ErrorCode::MethodNotFound : ProviderApiV1::ErrorCode::InternalError;
        const auto message = m_loaded ? QStringLiteral("Operation is not exported: %1").arg(method) : m_loadError;
        QTimer::singleShot(0, this, [this, token, code, message] { fail(token, code, message); });
        return id;
    }
    auto *handler = new JsPromiseHandler(this);
    m_operations[id].handler = handler;
    handler->onResolve = [this, token](const QJSValue &value) { complete(token, value); };
    handler->onReject = [this, token](const QString &error) { fail(token, ProviderApiV1::ErrorCode::InternalError, error); };
    const auto previous = enterOperation(token);
    const auto value = m_module.property(method).call(arguments);
    if (m_engine->isInterrupted()) executionInterrupted();
    else if (value.isError()) fail(token, ProviderApiV1::ErrorCode::InternalError, value.toString());
    else if (operationActive(token)) {
        const auto object = m_engine->newQObject(handler);
        const auto attached = m_helpers.property(QStringLiteral("attachPromise")).call({
            value, object.property(QStringLiteral("resolve")), object.property(QStringLiteral("reject"))});
        if (attached.isError()) fail(token, ProviderApiV1::ErrorCode::InternalError, attached.toString());
    }
    leaveOperation(previous);
    return id;
}

void JsProvider::complete(quint64 token, const QJSValue &value)
{
    const auto *op = operationForToken(token);
    if (!op) return;
    const auto id = m_tokens.value(token);
    const auto kind = op->kind;
    const auto resultId = op->resultId;
    if (kind == Kind::Content && value.isString()) {
        ProviderApiV1::ContentResponse response;
        response.resultId = resultId;
        response.content.format = ResultContent::Format::Markdown;
        response.content.description = value.toString();
        release(id);
        postTerminal(id, token, [this, id, response] { emit contentReady(id, response); });
        return;
    }
    const auto json = m_engine->globalObject().property(QStringLiteral("JSON")).property(QStringLiteral("stringify")).call({value});
    if (m_engine->isInterrupted()) { executionInterrupted(); return; }
    if (json.isError()) {
        fail(token, ProviderApiV1::ErrorCode::InternalError, QStringLiteral("Invalid provider response"));
        return;
    }
    const auto doc = QJsonDocument::fromJson(json.toString().toUtf8());
    if ((kind == Kind::Page && !doc.isArray() && !doc.isObject()) || (kind != Kind::Page && !doc.isObject())) {
        fail(token, ProviderApiV1::ErrorCode::InternalError, QStringLiteral("Invalid provider response shape"));
        return;
    }
    release(id);
    if (kind == Kind::Page) {
        SearchPage page;
        if (doc.isArray()) for (const auto &item : doc.array()) {
            if (item.isObject()) page.results.append(ProviderApiV1::searchResultFromJson(item.toObject()));
        }
        else page = ProviderApiV1::searchPageFromJson(doc.object());
        postTerminal(id, token, [this, id, page] { emit pageReady(id, page); });
    } else if (kind == Kind::Details) {
        auto details = ProviderApiV1::searchDetailsFromJson(doc.object());
        if (details.result.id.isEmpty()) details.result.id = resultId;
        postTerminal(id, token, [this, id, resultId, details] { emit detailsReady(id, resultId, details); });
    } else {
        auto response = ProviderApiV1::contentResponseFromJson(doc.object());
        if (kind == Kind::ContentFromDetails) response.content = ProviderApiV1::searchDetailsFromJson(doc.object()).content;
        if (response.resultId.isEmpty()) response.resultId = resultId;
        postTerminal(id, token, [this, id, response] { emit contentReady(id, response); });
    }
}

quint64 JsProvider::search(const QString &query, quint64 requestId) { return invoke(QStringLiteral("search"), {query}, Kind::Page, {}, requestId); }
quint64 JsProvider::getPage(const QString &query, const QString &token, quint64 requestId)
{
    if (token.isEmpty() && !m_module.property(QStringLiteral("getPage")).isCallable()) return search(query, requestId);
    return invoke(QStringLiteral("getPage"), {query, token}, Kind::Page, {}, requestId);
}
quint64 JsProvider::getDetails(const QString &resultId, quint64 requestId) { return invoke(QStringLiteral("getDetails"), {resultId}, Kind::Details, resultId, requestId); }
quint64 JsProvider::getContent(const QString &resultId, const QString &token, quint64 requestId)
{
    if (!m_module.property(QStringLiteral("getContent")).isCallable() && m_module.property(QStringLiteral("getDetails")).isCallable())
        return invoke(QStringLiteral("getDetails"), {resultId}, Kind::ContentFromDetails, resultId, requestId);
    return invoke(QStringLiteral("getContent"), {resultId, token}, Kind::Content, resultId, requestId);
}

#include "jsprovider.moc"
