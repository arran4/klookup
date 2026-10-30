// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providerhostservice.h"
#include <QDeadlineTimer>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <array>
#include <chrono>
#include <zlib.h>

namespace ProviderHost {
namespace {
constexpr qint64 MaxResponseBytes = 5 * 1024 * 1024;
constexpr qint64 ReadChunkBytes = 32 * 1024;

// Application-wide origin policy: tabs own independent engines/hosts, but an
// upstream quota and Retry-After apply to their combined traffic. This state
// intentionally outlives individual hosts and stores no queries or responses.
struct OriginPolicy {
    qint64 lastStarted = 0;
    qint64 retryAt = 0;
    int intervalMs = 0;
};
struct OriginPolicies {
    QMutex mutex;
    QHash<QString, OriginPolicy> entries;
};
OriginPolicies &originPolicies() { static OriginPolicies policies; return policies; }
qint64 monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
QString originKey(const QUrl &url) {
    return QStringLiteral("%1://%2:%3").arg(url.scheme().toLower(), url.host().toLower())
        .arg(url.port(url.scheme() == QLatin1String("https") ? 443 : 80));
}

bool sameOrigin(const QUrl &a, const QUrl &b)
{
    auto port = [](const QUrl &u) { return u.port(u.scheme() == QLatin1String("https") ? 443 : 80); };
    return a.scheme().compare(b.scheme(), Qt::CaseInsensitive) == 0 &&
           a.host().compare(b.host(), Qt::CaseInsensitive) == 0 && port(a) == port(b);
}

void removeHeader(QMap<QString, QString> &headers, const QString &name)
{
    for (auto it = headers.begin(); it != headers.end();) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0) it = headers.erase(it); else ++it;
    }
}

class Decoder {
public:
    explicit Decoder(int windowBits) : m_windowBits(windowBits) { m_valid = inflateInit2(&m_stream, windowBits) == Z_OK; }
    ~Decoder() { if (m_valid) inflateEnd(&m_stream); }
    bool append(const QByteArray &input, QByteArray &output, QString &error, bool &oversized) {
        if (!m_valid) { error = QStringLiteral("Cannot initialize response decompressor"); return false; }
        m_stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
        m_stream.avail_in = static_cast<uInt>(input.size());
        std::array<char, ReadChunkBytes> chunk;
        while (true) {
            if (m_ended) {
                if (!m_stream.avail_in) return true;
                // RFC 1952 permits concatenated gzip members. Every byte still
                // counts against the independent compressed limit.
                auto *next = m_stream.next_in;
                const auto available = m_stream.avail_in;
                if (m_windowBits != 31 || inflateReset2(&m_stream, m_windowBits) != Z_OK) {
                    error = QStringLiteral("Trailing data after compressed response"); return false;
                }
                m_stream.next_in = next;
                m_stream.avail_in = available;
                m_ended = false;
            }
            const qint64 remaining = MaxResponseBytes - output.size();
            const auto capacity = static_cast<uInt>(qMin<qint64>(chunk.size(), remaining + 1));
            m_stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
            m_stream.avail_out = capacity;
            const auto before = m_stream.avail_in;
            const int result = inflate(&m_stream, Z_NO_FLUSH);
            const qint64 produced = capacity - m_stream.avail_out;
            if (produced > remaining) { oversized = true; output.clear(); return false; }
            output.append(chunk.data(), produced);
            if (result == Z_STREAM_END) { m_ended = true; continue; }
            if (result != Z_OK && result != Z_BUF_ERROR) {
                error = QStringLiteral("Invalid compressed response"); return false;
            }
            if (!m_stream.avail_in && m_stream.avail_out) return true;
            if (before == m_stream.avail_in && !produced) {
                error = QStringLiteral("Invalid compressed response stream"); return false;
            }
        }
    }
    bool ended() const { return m_ended; }
private:
    z_stream m_stream{};
    int m_windowBits;
    bool m_valid = false;
    bool m_ended = false;
};
}

struct ProviderHostService::ActiveRequest {
    quint64 id = 0;
    HttpRequestOptions options;
    int hops = 0;
    QString secret;
    QPointer<QNetworkReply> reply;
    QPointer<QTimer> timer;
    QPointer<QTimer> pacingTimer;
    QDeadlineTimer deadline;
    qint64 wireBytes = 0;
    QByteArray body;
    std::unique_ptr<Decoder> decoder;
    bool encodingInitialized = false;
    bool finished = false;
};

ProviderHostService::ProviderHostService(const ProviderPackage &package, QObject *parent, QNetworkAccessManager *nam)
    : QObject(parent), m_package(package), m_nam(nam)
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    connect(m_nam, &QObject::destroyed, this, [this] {
        const auto requests = m_requests.values();
        for (const auto &request : requests) {
            // The manager itself is destroying its replies. Do not reenter it.
            request->reply = nullptr;
            fail(request, QStringLiteral("Provider transport is no longer available"));
        }
    });
}

ProviderHostService::~ProviderHostService() { cancelAllRequests(); }

void ProviderHostService::setPackage(const ProviderPackage &package)
{
    if (!package.isTrusted() || m_package.approvalFingerprint() != package.approvalFingerprint()) cancelAllRequests();
    m_package = package;
}

bool ProviderHostService::current(const Request &request) const
{
    return m_requests.value(request->id) == request && !request->finished;
}

void ProviderHostService::retire(const Request &request)
{
    if (request->timer) { request->timer->stop(); request->timer->deleteLater(); request->timer = nullptr; }
    if (request->pacingTimer) { request->pacingTimer->stop(); request->pacingTimer->deleteLater(); request->pacingTimer = nullptr; }
    if (request->reply) {
        auto *reply = request->reply.data();
        request->reply = nullptr;
        reply->disconnect(this);
        if (reply->isRunning()) reply->abort();
        reply->deleteLater();
    }
    request->secret.clear();
}

void ProviderHostService::cancelRequest(quint64 id)
{
    if (auto request = m_requests.take(id)) { request->finished = true; retire(request); }
}

void ProviderHostService::cancelAllRequests()
{
    const auto ids = m_requests.keys();
    for (auto id : ids) cancelRequest(id);
}

void ProviderHostService::finish(const Request &request, HttpResponse response)
{
    if (!current(request)) return;
    request->finished = true;
    retire(request);
    response.requestId = request->id;
    QTimer::singleShot(0, this, [this, request, response = std::move(response)] {
        if (m_requests.value(request->id) != request) return;
        m_requests.remove(request->id);
        emit httpResponseReady(request->id, response);
    });
}

void ProviderHostService::fail(const Request &request, const QString &message, bool truncated)
{
    if (!current(request)) return;
    HttpResponse response;
    if (request->reply) response.statusCode = request->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.errorMessage = message;
    response.truncated = truncated;
    request->body.clear();
    finish(request, response);
}

quint64 ProviderHostService::executeHttpRequest(const HttpRequestOptions &options)
{
    quint64 id = options.requestId;
    if (!id) { do { id = ++m_requestCounter; } while (m_requests.contains(id)); }
    cancelRequest(id);
    auto request = std::make_shared<ActiveRequest>();
    request->id = id;
    request->options = options;
    const int timeout = qBound(1, options.timeoutMs > 0 ? options.timeoutMs : 10000, 30000);
    request->deadline = QDeadlineTimer(timeout);
    request->timer = new QTimer(this);
    request->timer->setSingleShot(true);
    m_requests.insert(id, request);
    connect(request->timer, &QTimer::timeout, this, [this, request] {
        fail(request, QStringLiteral("Provider HTTP request timed out"));
    });
    request->timer->start(timeout);
    if (m_diagnosticMode && (!m_allowNetwork || !options.secretKey.isEmpty())) {
        fail(request, QStringLiteral("Network or credentials unavailable in this diagnostic mode")); return id;
    }
    if (!m_package.isValid() || !m_package.isTrusted()) {
        fail(request, QStringLiteral("Untrusted provider package '%1' cannot make network requests.").arg(m_package.id())); return id;
    }
    if (!options.url.isValid() || !options.url.userInfo().isEmpty() ||
        (options.url.scheme() != QLatin1String("http") && options.url.scheme() != QLatin1String("https"))) {
        fail(request, QStringLiteral("Invalid or unsupported URL scheme or credentials")); return id;
    }
    if (!m_package.isOriginAllowed(options.url)) {
        fail(request, QStringLiteral("Target origin '%1' is not declared in package manifest permissions.").arg(options.url.host())); return id;
    }
    if (!options.secretKey.isEmpty()) {
        if (!m_package.isSecretAllowed(options.secretKey)) {
            fail(request, QStringLiteral("Secret '%1' is not declared in package manifest permissions.").arg(options.secretKey)); return id;
        }
        if (m_credentialStore) {
            const auto storeKey = QStringLiteral("provider_pkg:%1:%2").arg(m_package.id(), options.secretKey);
            QPointer<ProviderHostService> guard(this);
            m_credentialStore->read(storeKey, true, [guard, request, options](const CredentialStoreResult &result) {
                if (!guard || !guard->current(request)) return;
                guard->startRequest(request, options, 0, result.ok ? result.value : QString());
            });
            return id;
        }
        if (m_secretResolver) {
            const auto secret = m_secretResolver(options.secretKey);
            if (current(request)) startRequest(request, options, 0, secret);
            return id;
        }
    }
    startRequest(request, options, 0, {});
    return id;
}

void ProviderHostService::startRequest(const Request &state, const HttpRequestOptions &options, int hops, const QString &secret)
{
    if (!current(state)) return;
    if (state->deadline.hasExpired()) { fail(state, QStringLiteral("Provider HTTP request timed out")); return; }
    if (!m_nam) { fail(state, QStringLiteral("Provider transport is no longer available")); return; }
    if (!m_package.isTrusted() || !m_package.isOriginAllowed(options.url) ||
        (!options.secretKey.isEmpty() && !m_package.isSecretAllowed(options.secretKey))) {
        fail(state, QStringLiteral("Package permission changed while request was pending")); return;
    }
    const auto method = options.method.trimmed().toUpper();
    if (method != QLatin1String("GET") && method != QLatin1String("POST")) {
        fail(state, QStringLiteral("Unsupported HTTP method")); return;
    }
    QNetworkRequest request(options.url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KLookup/1.0 (XDG Provider Package)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    static const QRegularExpression headerName(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
    for (auto it = options.headers.cbegin(); it != options.headers.cend(); ++it) {
        const auto name = it.key().toLower();
        if (!headerName.match(name).hasMatch() || it.value().contains(QLatin1Char('\r')) ||
            it.value().contains(QLatin1Char('\n')) || it.value().contains(QChar::Null) ||
            name == QLatin1String("host") || name == QLatin1String("content-length") ||
            name == QLatin1String("transfer-encoding") || name == QLatin1String("connection") ||
            name == QLatin1String("proxy-authorization")) {
            fail(state, QStringLiteral("Invalid or transport-controlled HTTP header")); return;
        }
        if (name != QLatin1String("accept-encoding")) request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
    // Explicit Accept-Encoding disables Qt's transparent decompressor, letting
    // us enforce independent wire/decompressed limits while streaming both.
    request.setRawHeader("Accept-Encoding", "gzip, deflate");
    if (!secret.isEmpty() && !options.secretHeader.isEmpty()) {
        const auto name = options.secretHeader.toLower();
        if (!headerName.match(options.secretHeader).hasMatch() || secret.contains(QLatin1Char('\r')) || secret.contains(QLatin1Char('\n')) ||
            secret.contains(QChar::Null) || name == QLatin1String("host") || name == QLatin1String("content-length") ||
            name == QLatin1String("transfer-encoding") || name == QLatin1String("connection") ||
            name == QLatin1String("proxy-authorization") || name == QLatin1String("accept-encoding")) {
            fail(state, QStringLiteral("Invalid or transport-controlled secret header")); return;
        }
        request.setRawHeader(options.secretHeader.toUtf8(), secret.toUtf8());
    }
    state->options = options;
    state->hops = hops;
    state->secret = secret;
    if (options.minimumIntervalMs > 0) {
        qint64 delay = 0;
        qint64 cooldown = 0;
        {
            auto &policies = originPolicies();
            QMutexLocker lock(&policies.mutex);
            auto &policy = policies.entries[originKey(options.url)];
            policy.intervalMs = qMax(policy.intervalMs, qBound(1, options.minimumIntervalMs, 60000));
            const auto now = monotonicMs();
            cooldown = policy.retryAt - now;
            delay = policy.lastStarted + policy.intervalMs - now;
            if (cooldown <= 0 && delay <= 0) policy.lastStarted = now;
        }
        if (cooldown > 0) {
            fail(state, QStringLiteral("Provider origin rate limited; retry after %1 seconds").arg((cooldown + 999) / 1000));
            return;
        }
        if (delay > 0) {
            state->pacingTimer = new QTimer(this);
            state->pacingTimer->setSingleShot(true);
            connect(state->pacingTimer, &QTimer::timeout, this, [this, state] {
                if (state->pacingTimer) { state->pacingTimer->deleteLater(); state->pacingTimer = nullptr; }
                startRequest(state, state->options, state->hops, state->secret);
            });
            state->pacingTimer->start(int(qMin(delay, state->deadline.remainingTime())));
            return;
        }
    }
    state->wireBytes = 0;
    state->body.clear();
    state->decoder.reset();
    state->encodingInitialized = false;
    auto *reply = method == QLatin1String("POST") ? m_nam->post(request, options.body) : m_nam->get(request);
    reply->setReadBufferSize(64 * 1024);
    state->reply = reply;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, state] {
        if (!current(state) || !state->reply) return;
        if (state->reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > MaxResponseBytes)
            fail(state, QStringLiteral("Response exceeded maximum streaming limit of 5 MiB (compressed/wire bytes)"), true);
    });
    connect(reply, &QIODevice::readyRead, this, [this, state] { drain(state); });
    connect(reply, &QNetworkReply::finished, this, [this, state] { complete(state); });
    if (reply->isFinished()) QTimer::singleShot(0, this, [this, state] { complete(state); });
}

bool ProviderHostService::drain(const Request &state)
{
    if (!current(state) || !state->reply) return false;
    auto *reply = state->reply.data();
    if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > MaxResponseBytes) {
        fail(state, QStringLiteral("Response exceeded maximum streaming limit of 5 MiB (compressed/wire bytes)"), true); return false;
    }
    if (!state->encodingInitialized) {
        const auto encoding = reply->rawHeader("Content-Encoding").trimmed().toLower();
        if (encoding == "gzip" || encoding == "x-gzip") state->decoder = std::make_unique<Decoder>(31);
        else if (encoding == "deflate") state->decoder = std::make_unique<Decoder>(15);
        else if (!encoding.isEmpty() && encoding != "identity") {
            fail(state, QStringLiteral("Unsupported response content encoding")); return false;
        }
        state->encodingInitialized = true;
    }
    while (reply->bytesAvailable() > 0) {
        const qint64 remaining = MaxResponseBytes - state->wireBytes;
        const auto chunk = reply->read(qMin(ReadChunkBytes, remaining + 1));
        if (chunk.isEmpty()) break;
        state->wireBytes += chunk.size();
        if (state->wireBytes > MaxResponseBytes) {
            fail(state, QStringLiteral("Response exceeded maximum streaming limit of 5 MiB (compressed/wire bytes)"), true); return false;
        }
        if (state->decoder) {
            QString error;
            bool oversized = false;
            if (!state->decoder->append(chunk, state->body, error, oversized)) {
                fail(state, oversized ? QStringLiteral("Response exceeded maximum streaming limit of 5 MiB (decompressed bytes)") : error, oversized);
                return false;
            }
        } else state->body.append(chunk);
    }
    return current(state);
}

void ProviderHostService::complete(const Request &state)
{
    if (!drain(state) || !state->reply) return;
    auto *reply = state->reply.data();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 429 && state->options.minimumIntervalMs > 0) {
        const auto retryAfter = reply->rawHeader("Retry-After").trimmed();
        bool numeric = false;
        qint64 seconds = retryAfter.toLongLong(&numeric);
        if (!numeric) {
            const auto date = QDateTime::fromString(QString::fromLatin1(retryAfter), Qt::RFC2822Date);
            seconds = date.isValid() ? QDateTime::currentDateTimeUtc().secsTo(date) : 60;
        }
        seconds = qBound(qint64(1), seconds, qint64(86400));
        auto &policies = originPolicies();
        QMutexLocker lock(&policies.mutex);
        auto &policy = policies.entries[originKey(state->options.url)];
        policy.retryAt = qMax(policy.retryAt, monotonicMs() + seconds * 1000);
    }
    const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirect.isValid()) {
        if (state->hops >= 5) { fail(state, QStringLiteral("Too many redirects: exceeded maximum of 5 hops.")); return; }
        const auto target = state->options.url.resolved(redirect.toUrl());
        if (state->options.url.scheme() == QLatin1String("https") && target.scheme() != QLatin1String("https")) {
            fail(state, QStringLiteral("Insecure redirect from HTTPS to HTTP blocked.")); return;
        }
        if (!target.isValid() || !target.userInfo().isEmpty() || !m_package.isOriginAllowed(target)) {
            fail(state, QStringLiteral("Redirect to undeclared origin '%1' blocked by package policy.").arg(target.host())); return;
        }
        auto next = state->options;
        next.url = target;
        auto nextSecret = state->secret;
        if (!sameOrigin(state->options.url, target)) {
            const auto injected = next.secretHeader;
            for (auto it = next.headers.begin(); it != next.headers.end();) {
                const auto name = it.key().toLower();
                if ((name != QLatin1String("accept") && name != QLatin1String("content-type") && name != QLatin1String("user-agent")) ||
                    it.key().compare(injected, Qt::CaseInsensitive) == 0) it = next.headers.erase(it); else ++it;
            }
            next.secretKey.clear(); next.secretHeader.clear(); nextSecret.clear();
        }
        if (status == 303 || ((status == 301 || status == 302) && next.method.compare(QStringLiteral("POST"), Qt::CaseInsensitive) == 0)) {
            next.method = QStringLiteral("GET"); next.body.clear(); removeHeader(next.headers, QStringLiteral("Content-Type"));
        }
        reply->disconnect(this);
        reply->deleteLater();
        state->reply = nullptr;
        startRequest(state, next, state->hops + 1, nextSecret);
        return;
    }
    const auto error = reply->error();
    const bool httpError = status >= 400 &&
        ((error >= QNetworkReply::ContentAccessDenied && error <= QNetworkReply::UnknownContentError) ||
         (error >= QNetworkReply::InternalServerError && error <= QNetworkReply::UnknownServerError));
    if (error != QNetworkReply::NoError && !httpError) { fail(state, QStringLiteral("HTTP transport failed: %1").arg(reply->errorString())); return; }
    if (state->decoder && !state->decoder->ended()) { fail(state, QStringLiteral("Truncated compressed response")); return; }
    HttpResponse response;
    response.statusCode = status;
    response.statusText = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    response.success = true; // Completed HTTP, including 4xx/5xx; JS response.ok remains false.
    response.body = std::move(state->body);
    for (const auto &header : reply->rawHeaderPairs()) response.headers.insert(QString::fromUtf8(header.first), QString::fromUtf8(header.second));
    finish(state, std::move(response));
}
}
