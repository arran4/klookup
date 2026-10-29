// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROVIDERAPIV1_H
#define PROVIDERAPIV1_H

#include "resultcontent.h"
#include "resultmetadata.h"
#include "searchcontinuation.h"
#include "searchprovider.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QUrl>

namespace ProviderApiV1
{

constexpr const char *ProtocolVersion = "klookup.provider.v1";

enum class ErrorCode {
    Success = 0,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    Cancelled = -32000,
    NetworkError = -32001,
    RateLimited = -32002,
    AuthenticationRequired = -32003,
    ItemNotFound = -32004,
    ContentTooLarge = -32005,
    Timeout = -32006
};

struct Capabilities {
    bool pagination = false;
    bool details = false;
    bool separateContent = false;
    bool hierarchy = false;
    bool authentication = false;
    QString authType; // "none", "token", "oauth2_pkce"
};

struct Manifest {
    QString id;
    QString name;
    QString description;
    QString version;
    QString author;
    QString license;
    Capabilities capabilities;
    QJsonObject defaultSettings;
};

struct ContentResponse {
    QString resultId;
    ResultContent content;
    QString nextContentToken;
    QString attribution;
};

// Serialization & Deserialization Helpers
QJsonObject toJson(const SearchResult &result);
SearchResult searchResultFromJson(const QJsonObject &json, bool *ok = nullptr);

QJsonObject toJson(const SearchPage &page);
SearchPage searchPageFromJson(const QJsonObject &json, bool *ok = nullptr);

QJsonObject toJson(const SearchDetails &details);
SearchDetails searchDetailsFromJson(const QJsonObject &json, bool *ok = nullptr);

QJsonObject toJson(const ContentBlock &block);
ContentBlock contentBlockFromJson(const QJsonObject &json, bool *ok = nullptr);

QJsonObject toJson(const ResultContent &content);
ResultContent resultContentFromJson(const QJsonObject &json, bool *ok = nullptr);

QJsonObject toJson(const ResultMetadata &metadata);
ResultMetadata resultMetadataFromJson(const QJsonObject &json, bool *ok = nullptr);

QJsonObject toJson(const Capabilities &caps);
Capabilities capabilitiesFromJson(const QJsonObject &json, bool *ok = nullptr);

QJsonObject toJson(const ContentResponse &contentResp);
ContentResponse contentResponseFromJson(const QJsonObject &json, bool *ok = nullptr);

// JSON-RPC 2.0 Envelopes
QJsonObject toJsonRpcRequest(quint64 id, const QString &method, const QJsonObject &params);
QJsonObject toJsonRpcSuccess(quint64 id, const QJsonObject &result);
QJsonObject toJsonRpcError(quint64 id, ErrorCode code, const QString &message, const QJsonValue &data = QJsonValue());

// Abstract Provider API v1 interface
class ProviderV1 : public QObject
{
    Q_OBJECT
public:
    explicit ProviderV1(QObject *parent = nullptr);
    ~ProviderV1() override = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual Capabilities capabilities() const = 0;

    // Asynchronous commands with explicit request IDs
    virtual quint64 search(const QString &query, quint64 requestId = 0) = 0;
    virtual quint64 getPage(const QString &query, const QString &pageToken, quint64 requestId = 0) = 0;
    virtual quint64 getDetails(const QString &resultId, quint64 requestId = 0) = 0;
    virtual quint64 getContent(const QString &resultId, const QString &contentToken = QString(), quint64 requestId = 0) = 0;
    virtual void cancel(quint64 requestId) = 0;

    virtual ProviderV1 *clone(QObject *parent = nullptr) const
    {
        Q_UNUSED(parent);
        return nullptr;
    }

signals:
    void pageReady(quint64 requestId, const SearchPage &page);
    void pageFailed(quint64 requestId, ErrorCode code, const QString &message);

    void detailsReady(quint64 requestId, const QString &resultId, const SearchDetails &details);
    void detailsFailed(quint64 requestId, const QString &resultId, ErrorCode code, const QString &message);

    void contentReady(quint64 requestId, const ContentResponse &response);
    void contentFailed(quint64 requestId, const QString &resultId, ErrorCode code, const QString &message);
};

// Legacy C++ SearchProvider -> ProviderV1 adapter
class LegacyProviderV1Adapter : public ProviderV1
{
    Q_OBJECT
public:
    explicit LegacyProviderV1Adapter(SearchProvider *legacyProvider, QObject *parent = nullptr);
    ~LegacyProviderV1Adapter() override;

    QString id() const override;
    QString name() const override;
    QString description() const override;
    Capabilities capabilities() const override;

    quint64 search(const QString &query, quint64 requestId = 0) override;
    quint64 getPage(const QString &query, const QString &pageToken, quint64 requestId = 0) override;
    quint64 getDetails(const QString &resultId, quint64 requestId = 0) override;
    quint64 getContent(const QString &resultId, const QString &contentToken = QString(), quint64 requestId = 0) override;
    void cancel(quint64 requestId) override;
    ProviderV1 *clone(QObject *parent = nullptr) const override;

    SearchProvider *underlyingProvider() const { return m_legacy; }

private:
    void onSearchCompleted(const QString &providerId, const QList<SearchResult> &results);
    void onSearchFailed(const QString &providerId, const QString &errorString);
    void onPageCompleted(quint64 reqId, const SearchPage &page);
    void onDetailsCompleted(quint64 reqId, const SearchDetails &details);
    void onRequestFailed(quint64 reqId, const QString &error, int retryAfterMs);

    quint64 nextRequestId();

    SearchProvider *m_legacy = nullptr;
    SearchProviderContinuation *m_continuation = nullptr;
    quint64 m_counter = 0;

    enum class RequestType { Search, Page, Details, Content };
    struct PendingRequest {
        quint64 externalId = 0;
        quint64 internalId = 0;
        RequestType type = RequestType::Search;
        QString targetId;
        bool cancelled = false;
    };

    QHash<quint64, PendingRequest> m_byInternalId;
    QHash<quint64, PendingRequest> m_byExternalId;
    QHash<QString, SearchDetails> m_cachedDetails;
};

// ProviderV1 -> Legacy SearchProvider & SearchProviderContinuation adapter
class ProviderV1ToLegacyAdapter : public SearchProvider, public SearchProviderContinuation
{
    Q_OBJECT
    Q_INTERFACES(SearchProvider SearchProviderContinuation)
public:
    using Factory = std::function<ProviderV1 *(QObject *parent)>;

    explicit ProviderV1ToLegacyAdapter(ProviderV1 *providerV1, QObject *parent = nullptr);
    explicit ProviderV1ToLegacyAdapter(Factory factory, QObject *parent = nullptr);
    ~ProviderV1ToLegacyAdapter() override;

    QString id() const override;
    QString name() const override;
    void search(const QString &query) override;
    SearchProvider *clone(QObject *parent = nullptr) const override;

    // SearchProviderContinuation overrides
    void requestPage(const QString &query, const QString &token, quint64 requestId) override;
    bool supportsDetails() const override;
    void requestDetails(const QString &resultId, const QString &token, quint64 requestId) override;
    void cancelRequests() override;

    ProviderV1 *underlyingProvider() const { return m_v1; }

private:
    void initV1Connections();
    void onPageReady(quint64 reqId, const SearchPage &page);
    void onPageFailed(quint64 reqId, ErrorCode code, const QString &message);
    void onDetailsReady(quint64 reqId, const QString &resultId, const SearchDetails &details);
    void onDetailsFailed(quint64 reqId, const QString &resultId, ErrorCode code, const QString &message);
    void onContentReady(quint64 reqId, const ContentResponse &response);
    void onContentFailed(quint64 reqId, const QString &resultId, ErrorCode code, const QString &message);

    ProviderV1 *m_v1 = nullptr;
    Factory m_factory;
    bool m_ownsV1 = false;
    quint64 m_searchCounter = 0;
    quint64 m_activeSearchId = 0;
    QHash<quint64, QString> m_activePageRequests;
    QHash<quint64, QString> m_activeDetailsRequests;
};

} // namespace ProviderApiV1

#endif // PROVIDERAPIV1_H
