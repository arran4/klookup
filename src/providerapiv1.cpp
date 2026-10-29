// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providerapiv1.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace ProviderApiV1
{

static QString formatToString(ResultContent::Format format)
{
    switch (format) {
    case ResultContent::Format::PlainText: return QStringLiteral("plainText");
    case ResultContent::Format::LimitedHtml: return QStringLiteral("limitedHtml");
    case ResultContent::Format::Markdown: return QStringLiteral("markdown");
    case ResultContent::Format::StructuredBlocks: return QStringLiteral("structuredBlocks");
    }
    return QStringLiteral("plainText");
}

static ResultContent::Format formatFromString(const QString &str)
{
    if (str == QLatin1String("limitedHtml")) return ResultContent::Format::LimitedHtml;
    if (str == QLatin1String("markdown")) return ResultContent::Format::Markdown;
    if (str == QLatin1String("structuredBlocks")) return ResultContent::Format::StructuredBlocks;
    return ResultContent::Format::PlainText;
}

static QString blockTypeToString(ContentBlock::Type type)
{
    switch (type) {
    case ContentBlock::Type::Heading: return QStringLiteral("heading");
    case ContentBlock::Type::Paragraph: return QStringLiteral("paragraph");
    case ContentBlock::Type::CodeBlock: return QStringLiteral("code");
    case ContentBlock::Type::List: return QStringLiteral("list");
    case ContentBlock::Type::Table: return QStringLiteral("table");
    case ContentBlock::Type::FieldList: return QStringLiteral("fields");
    case ContentBlock::Type::Quote: return QStringLiteral("quote");
    case ContentBlock::Type::Image: return QStringLiteral("image");
    }
    return QStringLiteral("paragraph");
}

static ContentBlock::Type blockTypeFromString(const QString &str)
{
    if (str == QLatin1String("heading")) return ContentBlock::Type::Heading;
    if (str == QLatin1String("code")) return ContentBlock::Type::CodeBlock;
    if (str == QLatin1String("list")) return ContentBlock::Type::List;
    if (str == QLatin1String("table")) return ContentBlock::Type::Table;
    if (str == QLatin1String("fields")) return ContentBlock::Type::FieldList;
    if (str == QLatin1String("quote")) return ContentBlock::Type::Quote;
    if (str == QLatin1String("image")) return ContentBlock::Type::Image;
    return ContentBlock::Type::Paragraph;
}

QJsonObject toJson(const SearchResult &result)
{
    QJsonObject json;
    json[QStringLiteral("id")] = result.id;
    json[QStringLiteral("title")] = result.title;
    if (!result.description.isEmpty())
        json[QStringLiteral("description")] = result.description;
    if (!result.url.isEmpty())
        json[QStringLiteral("url")] = result.url;
    if (!result.type.isEmpty())
        json[QStringLiteral("type")] = result.type;
    if (!result.parentId.isEmpty())
        json[QStringLiteral("parentId")] = result.parentId;
    if (!result.children.isEmpty()) {
        QJsonArray arr;
        for (const auto &child : result.children) {
            arr.append(toJson(child));
        }
        json[QStringLiteral("children")] = arr;
    }
    if (!result.additionalData.isEmpty()) {
        json[QStringLiteral("additionalData")] = QJsonObject::fromVariantMap(result.additionalData);
    }
    return json;
}

SearchResult searchResultFromJson(const QJsonObject &json, bool *ok)
{
    SearchResult result;
    if (ok) *ok = true;
    result.id = json.value(QStringLiteral("id")).toString();
    result.title = json.value(QStringLiteral("title")).toString();
    result.description = json.value(QStringLiteral("description")).toString();
    result.url = json.value(QStringLiteral("url")).toString();
    result.type = json.value(QStringLiteral("type")).toString();
    result.parentId = json.value(QStringLiteral("parentId")).toString();

    if (json.contains(QStringLiteral("children"))) {
        const auto arr = json.value(QStringLiteral("children")).toArray();
        for (const auto &v : arr) {
            if (v.isObject()) {
                result.children.append(searchResultFromJson(v.toObject(), ok));
            }
        }
    }
    if (json.contains(QStringLiteral("additionalData"))) {
        result.additionalData = json.value(QStringLiteral("additionalData")).toObject().toVariantMap();
    }
    return result;
}

QJsonObject toJson(const SearchPage &page)
{
    QJsonObject json;
    QJsonArray resultsArr;
    for (const auto &res : page.results) {
        resultsArr.append(toJson(res));
    }
    json[QStringLiteral("results")] = resultsArr;
    if (!page.nextToken.isEmpty())
        json[QStringLiteral("nextToken")] = page.nextToken;
    if (!page.content.isEmpty()) {
        QJsonObject contentObj;
        for (auto it = page.content.cbegin(), end = page.content.cend(); it != end; ++it) {
            contentObj[it.key()] = toJson(it.value());
        }
        json[QStringLiteral("content")] = contentObj;
    }
    return json;
}

SearchPage searchPageFromJson(const QJsonObject &json, bool *ok)
{
    SearchPage page;
    if (ok) *ok = true;
    const auto arr = json.value(QStringLiteral("results")).toArray();
    for (const auto &v : arr) {
        if (v.isObject()) {
            page.results.append(searchResultFromJson(v.toObject(), ok));
        }
    }
    page.nextToken = json.value(QStringLiteral("nextToken")).toString();
    if (json.contains(QStringLiteral("content"))) {
        const auto contentObj = json.value(QStringLiteral("content")).toObject();
        for (auto it = contentObj.constBegin(), end = contentObj.constEnd(); it != end; ++it) {
            if (it.value().isObject()) {
                page.content.insert(it.key(), resultContentFromJson(it.value().toObject(), ok));
            }
        }
    }
    return page;
}

static QJsonObject metadataNodeToJson(const MetadataNode &node)
{
    QJsonObject json;
    json[QStringLiteral("label")] = node.label;
    if (!node.values.isEmpty()) {
        QJsonArray valArr;
        for (const auto &val : node.values) {
            QJsonObject valObj;
            valObj[QStringLiteral("text")] = val.text;
            if (!val.url.isEmpty())
                valObj[QStringLiteral("url")] = val.url.toString(QUrl::FullyEncoded);
            valObj[QStringLiteral("kind")] = val.kind == MetadataValue::Kind::Image
                                                ? QStringLiteral("image")
                                                : (val.kind == MetadataValue::Kind::Link ? QStringLiteral("link") : QStringLiteral("text"));
            valArr.append(valObj);
        }
        json[QStringLiteral("values")] = valArr;
    }
    if (!node.children.isEmpty()) {
        QJsonArray childArr;
        for (const auto &child : node.children) {
            childArr.append(metadataNodeToJson(child));
        }
        json[QStringLiteral("children")] = childArr;
    }
    return json;
}

static MetadataNode metadataNodeFromJson(const QJsonObject &json)
{
    MetadataNode node;
    node.label = json.value(QStringLiteral("label")).toString();
    if (json.contains(QStringLiteral("values"))) {
        const auto arr = json.value(QStringLiteral("values")).toArray();
        for (const auto &v : arr) {
            if (v.isObject()) {
                const auto vo = v.toObject();
                MetadataValue val;
                val.text = vo.value(QStringLiteral("text")).toString();
                if (vo.contains(QStringLiteral("url"))) {
                    val.url = QUrl(vo.value(QStringLiteral("url")).toString());
                }
                const QString k = vo.value(QStringLiteral("kind")).toString();
                if (k == QLatin1String("image")) val.kind = MetadataValue::Kind::Image;
                else if (k == QLatin1String("link")) val.kind = MetadataValue::Kind::Link;
                else val.kind = MetadataValue::Kind::Text;
                node.values.append(val);
            }
        }
    }
    if (json.contains(QStringLiteral("children"))) {
        const auto arr = json.value(QStringLiteral("children")).toArray();
        for (const auto &v : arr) {
            if (v.isObject()) {
                node.children.append(metadataNodeFromJson(v.toObject()));
            }
        }
    }
    return node;
}

QJsonObject toJson(const ResultMetadata &metadata)
{
    QJsonObject json;
    if (!metadata.attribution.isEmpty())
        json[QStringLiteral("attribution")] = QJsonArray::fromStringList(metadata.attribution);
    if (!metadata.sections.isEmpty()) {
        QJsonArray secArr;
        for (const auto &sec : metadata.sections) {
            secArr.append(metadataNodeToJson(sec));
        }
        json[QStringLiteral("sections")] = secArr;
    }
    return json;
}

ResultMetadata resultMetadataFromJson(const QJsonObject &json, bool *ok)
{
    ResultMetadata metadata;
    if (ok) *ok = true;
    if (json.contains(QStringLiteral("attribution"))) {
        const auto attrVal = json.value(QStringLiteral("attribution"));
        if (attrVal.isArray()) {
            for (const auto &v : attrVal.toArray()) {
                metadata.attribution.append(v.toString());
            }
        } else if (attrVal.isString() && !attrVal.toString().isEmpty()) {
            metadata.attribution.append(attrVal.toString());
        }
    }
    if (json.contains(QStringLiteral("sections"))) {
        const auto arr = json.value(QStringLiteral("sections")).toArray();
        for (const auto &v : arr) {
            if (v.isObject()) {
                metadata.sections.append(metadataNodeFromJson(v.toObject()));
            }
        }
    }
    return metadata;
}

QJsonObject toJson(const ContentBlock &block)
{
    QJsonObject json;
    json[QStringLiteral("type")] = blockTypeToString(block.type);
    if (!block.text.isEmpty()) json[QStringLiteral("text")] = block.text;
    if (block.type == ContentBlock::Type::Heading) json[QStringLiteral("level")] = block.level;
    if (block.type == ContentBlock::Type::CodeBlock && !block.language.isEmpty())
        json[QStringLiteral("language")] = block.language;
    if (block.type == ContentBlock::Type::List && !block.items.isEmpty())
        json[QStringLiteral("items")] = QJsonArray::fromStringList(block.items);
    if (block.type == ContentBlock::Type::Table && !block.tableRows.isEmpty()) {
        QJsonArray rowsArr;
        for (const auto &row : block.tableRows) {
            rowsArr.append(QJsonArray::fromStringList(row));
        }
        json[QStringLiteral("tableRows")] = rowsArr;
    }
    if (block.type == ContentBlock::Type::FieldList && !block.fields.isEmpty()) {
        QJsonArray fieldsArr;
        for (const auto &f : block.fields) {
            QJsonObject fObj;
            fObj[QStringLiteral("key")] = f.first;
            fObj[QStringLiteral("value")] = f.second;
            fieldsArr.append(fObj);
        }
        json[QStringLiteral("fields")] = fieldsArr;
    }
    if (block.type == ContentBlock::Type::Image) {
        if (!block.imageUrl.isEmpty()) json[QStringLiteral("imageUrl")] = block.imageUrl.toString(QUrl::FullyEncoded);
        if (!block.caption.isEmpty()) json[QStringLiteral("caption")] = block.caption;
    }
    return json;
}

ContentBlock contentBlockFromJson(const QJsonObject &json, bool *ok)
{
    ContentBlock block;
    if (ok) *ok = true;
    block.type = blockTypeFromString(json.value(QStringLiteral("type")).toString());
    block.text = json.value(QStringLiteral("text")).toString();
    block.level = json.value(QStringLiteral("level")).toInt(1);
    block.language = json.value(QStringLiteral("language")).toString();
    if (json.contains(QStringLiteral("items"))) {
        for (const auto &v : json.value(QStringLiteral("items")).toArray()) {
            block.items.append(v.toString());
        }
    }
    if (json.contains(QStringLiteral("tableRows"))) {
        for (const auto &rowVal : json.value(QStringLiteral("tableRows")).toArray()) {
            QStringList row;
            for (const auto &colVal : rowVal.toArray()) {
                row.append(colVal.toString());
            }
            block.tableRows.append(row);
        }
    }
    if (json.contains(QStringLiteral("fields"))) {
        for (const auto &fVal : json.value(QStringLiteral("fields")).toArray()) {
            const auto fObj = fVal.toObject();
            block.fields.append({fObj.value(QStringLiteral("key")).toString(), fObj.value(QStringLiteral("value")).toString()});
        }
    }
    if (json.contains(QStringLiteral("imageUrl"))) {
        block.imageUrl = QUrl(json.value(QStringLiteral("imageUrl")).toString());
    }
    block.caption = json.value(QStringLiteral("caption")).toString();
    return block;
}

QJsonObject toJson(const ResultContent &content)
{
    QJsonObject json;
    if (!content.description.isEmpty())
        json[QStringLiteral("description")] = content.description;
    json[QStringLiteral("format")] = formatToString(content.format);
    if (!content.baseUrl.isEmpty())
        json[QStringLiteral("baseUrl")] = content.baseUrl.toString(QUrl::FullyEncoded);
    if (!content.blocks.isEmpty()) {
        QJsonArray blocksArr;
        for (const auto &b : content.blocks) {
            blocksArr.append(toJson(b));
        }
        json[QStringLiteral("blocks")] = blocksArr;
    }
    return json;
}

ResultContent resultContentFromJson(const QJsonObject &json, bool *ok)
{
    ResultContent content;
    if (ok) *ok = true;
    content.description = json.value(QStringLiteral("description")).toString();
    content.format = formatFromString(json.value(QStringLiteral("format")).toString());
    if (json.contains(QStringLiteral("baseUrl"))) {
        content.baseUrl = QUrl(json.value(QStringLiteral("baseUrl")).toString());
    }
    if (json.contains(QStringLiteral("blocks"))) {
        for (const auto &bv : json.value(QStringLiteral("blocks")).toArray()) {
            if (bv.isObject()) {
                content.blocks.append(contentBlockFromJson(bv.toObject(), ok));
            }
        }
    }
    return content;
}

QJsonObject toJson(const SearchDetails &details)
{
    QJsonObject json;
    json[QStringLiteral("result")] = toJson(details.result);
    if (!details.nextToken.isEmpty())
        json[QStringLiteral("nextToken")] = details.nextToken;
    if (!details.metadata.sections.isEmpty() || !details.metadata.attribution.isEmpty())
        json[QStringLiteral("metadata")] = toJson(details.metadata);
    if (!details.content.description.isEmpty() || !details.content.blocks.isEmpty())
        json[QStringLiteral("content")] = toJson(details.content);
    return json;
}

SearchDetails searchDetailsFromJson(const QJsonObject &json, bool *ok)
{
    SearchDetails details;
    if (ok) *ok = true;
    if (json.contains(QStringLiteral("result"))) {
        details.result = searchResultFromJson(json.value(QStringLiteral("result")).toObject(), ok);
    }
    details.nextToken = json.value(QStringLiteral("nextToken")).toString();
    if (json.contains(QStringLiteral("metadata"))) {
        details.metadata = resultMetadataFromJson(json.value(QStringLiteral("metadata")).toObject(), ok);
    }
    if (json.contains(QStringLiteral("content"))) {
        details.content = resultContentFromJson(json.value(QStringLiteral("content")).toObject(), ok);
    }
    return details;
}

QJsonObject toJson(const Capabilities &caps)
{
    QJsonObject json;
    json[QStringLiteral("pagination")] = caps.pagination;
    json[QStringLiteral("details")] = caps.details;
    json[QStringLiteral("separateContent")] = caps.separateContent;
    json[QStringLiteral("hierarchy")] = caps.hierarchy;
    json[QStringLiteral("authentication")] = caps.authentication;
    if (!caps.authType.isEmpty())
        json[QStringLiteral("authType")] = caps.authType;
    return json;
}

Capabilities capabilitiesFromJson(const QJsonObject &json, bool *ok)
{
    Capabilities caps;
    if (ok) *ok = true;
    caps.pagination = json.value(QStringLiteral("pagination")).toBool(false);
    caps.details = json.value(QStringLiteral("details")).toBool(false);
    caps.separateContent = json.value(QStringLiteral("separateContent")).toBool(false);
    caps.hierarchy = json.value(QStringLiteral("hierarchy")).toBool(false);
    caps.authentication = json.value(QStringLiteral("authentication")).toBool(false);
    caps.authType = json.value(QStringLiteral("authType")).toString();
    return caps;
}

QJsonObject toJson(const ContentResponse &contentResp)
{
    QJsonObject json;
    json[QStringLiteral("resultId")] = contentResp.resultId;
    json[QStringLiteral("content")] = toJson(contentResp.content);
    if (!contentResp.nextContentToken.isEmpty())
        json[QStringLiteral("nextContentToken")] = contentResp.nextContentToken;
    if (!contentResp.attribution.isEmpty())
        json[QStringLiteral("attribution")] = contentResp.attribution;
    return json;
}

ContentResponse contentResponseFromJson(const QJsonObject &json, bool *ok)
{
    ContentResponse resp;
    if (ok) *ok = true;
    resp.resultId = json.value(QStringLiteral("resultId")).toString();
    if (json.contains(QStringLiteral("content"))) {
        resp.content = resultContentFromJson(json.value(QStringLiteral("content")).toObject(), ok);
    }
    resp.nextContentToken = json.value(QStringLiteral("nextContentToken")).toString();
    resp.attribution = json.value(QStringLiteral("attribution")).toString();
    return resp;
}

QJsonObject toJsonRpcRequest(quint64 id, const QString &method, const QJsonObject &params)
{
    QJsonObject json;
    json[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    json[QStringLiteral("id")] = static_cast<qint64>(id);
    json[QStringLiteral("method")] = method;
    json[QStringLiteral("params")] = params;
    return json;
}

QJsonObject toJsonRpcSuccess(quint64 id, const QJsonObject &result)
{
    QJsonObject json;
    json[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    json[QStringLiteral("id")] = static_cast<qint64>(id);
    json[QStringLiteral("result")] = result;
    return json;
}

QJsonObject toJsonRpcError(quint64 id, ErrorCode code, const QString &message, const QJsonValue &data)
{
    QJsonObject json;
    json[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    json[QStringLiteral("id")] = static_cast<qint64>(id);
    QJsonObject err;
    err[QStringLiteral("code")] = static_cast<int>(code);
    err[QStringLiteral("message")] = message;
    if (!data.isUndefined() && !data.isNull()) {
        err[QStringLiteral("data")] = data;
    }
    json[QStringLiteral("error")] = err;
    return json;
}

ProviderV1::ProviderV1(QObject *parent)
    : QObject(parent)
{
}

// -----------------------------------------------------------------------------
// LegacyProviderV1Adapter
// -----------------------------------------------------------------------------

LegacyProviderV1Adapter::LegacyProviderV1Adapter(SearchProvider *legacyProvider, QObject *parent)
    : ProviderV1(parent)
    , m_legacy(legacyProvider)
    , m_continuation(qobject_cast<SearchProviderContinuation *>(legacyProvider))
{
    if (m_legacy) {
        connect(m_legacy, &SearchProvider::searchCompleted, this, &LegacyProviderV1Adapter::onSearchCompleted);
        connect(m_legacy, &SearchProvider::searchFailed, this, &LegacyProviderV1Adapter::onSearchFailed);
        connect(m_legacy, &SearchProvider::pageCompleted, this, &LegacyProviderV1Adapter::onPageCompleted);
        connect(m_legacy, &SearchProvider::detailsCompleted, this, &LegacyProviderV1Adapter::onDetailsCompleted);
        connect(m_legacy, &SearchProvider::requestFailed, this, &LegacyProviderV1Adapter::onRequestFailed);
    }
}

LegacyProviderV1Adapter::~LegacyProviderV1Adapter()
{
}

QString LegacyProviderV1Adapter::id() const
{
    return m_legacy ? m_legacy->id() : QString();
}

QString LegacyProviderV1Adapter::name() const
{
    return m_legacy ? m_legacy->name() : QString();
}

QString LegacyProviderV1Adapter::description() const
{
    return m_legacy ? m_legacy->name() : QString();
}

Capabilities LegacyProviderV1Adapter::capabilities() const
{
    Capabilities caps;
    caps.pagination = (m_continuation != nullptr);
    caps.details = (m_continuation != nullptr && m_continuation->supportsDetails());
    caps.separateContent = caps.details; // can be retrieved via details
    caps.hierarchy = false;
    caps.authentication = (m_legacy != nullptr && m_legacy->supportsAuthentication());
    return caps;
}

quint64 LegacyProviderV1Adapter::nextRequestId()
{
    return ++m_counter;
}

quint64 LegacyProviderV1Adapter::search(const QString &query, quint64 requestId)
{
    const quint64 extId = requestId != 0 ? requestId : nextRequestId();
    const quint64 intId = nextRequestId();

    PendingRequest req;
    req.externalId = extId;
    req.internalId = intId;
    req.type = RequestType::Search;
    m_byExternalId.insert(extId, req);
    m_byInternalId.insert(intId, req);

    if (m_continuation) {
        m_continuation->requestPage(query, QString(), intId);
    } else if (m_legacy) {
        m_legacy->search(query);
    }
    return extId;
}

quint64 LegacyProviderV1Adapter::getPage(const QString &query, const QString &pageToken, quint64 requestId)
{
    const quint64 extId = requestId != 0 ? requestId : nextRequestId();
    if (!m_continuation) {
        emit pageFailed(extId, ErrorCode::MethodNotFound, QStringLiteral("Pagination not supported"));
        return extId;
    }
    const quint64 intId = nextRequestId();
    PendingRequest req;
    req.externalId = extId;
    req.internalId = intId;
    req.type = RequestType::Page;
    m_byExternalId.insert(extId, req);
    m_byInternalId.insert(intId, req);

    m_continuation->requestPage(query, pageToken, intId);
    return extId;
}

quint64 LegacyProviderV1Adapter::getDetails(const QString &resultId, quint64 requestId)
{
    const quint64 extId = requestId != 0 ? requestId : nextRequestId();
    if (m_cachedDetails.contains(resultId)) {
        emit detailsReady(extId, resultId, m_cachedDetails.value(resultId));
        return extId;
    }
    if (!m_continuation || !m_continuation->supportsDetails()) {
        emit detailsFailed(extId, resultId, ErrorCode::MethodNotFound, QStringLiteral("Details not supported"));
        return extId;
    }
    const quint64 intId = nextRequestId();
    PendingRequest req;
    req.externalId = extId;
    req.internalId = intId;
    req.type = RequestType::Details;
    req.targetId = resultId;
    m_byExternalId.insert(extId, req);
    m_byInternalId.insert(intId, req);

    m_continuation->requestDetails(resultId, QString(), intId);
    return extId;
}

quint64 LegacyProviderV1Adapter::getContent(const QString &resultId, const QString &contentToken, quint64 requestId)
{
    Q_UNUSED(contentToken);
    const quint64 extId = requestId != 0 ? requestId : nextRequestId();
    if (m_cachedDetails.contains(resultId)) {
        const auto &det = m_cachedDetails.value(resultId);
        ContentResponse resp;
        resp.resultId = resultId;
        resp.content = det.content;
        emit contentReady(extId, resp);
        return extId;
    }
    if (!m_continuation || !m_continuation->supportsDetails()) {
        emit contentFailed(extId, resultId, ErrorCode::MethodNotFound, QStringLiteral("Separate content not supported"));
        return extId;
    }
    const quint64 intId = nextRequestId();
    PendingRequest req;
    req.externalId = extId;
    req.internalId = intId;
    req.type = RequestType::Content;
    req.targetId = resultId;
    m_byExternalId.insert(extId, req);
    m_byInternalId.insert(intId, req);

    m_continuation->requestDetails(resultId, QString(), intId);
    return extId;
}

void LegacyProviderV1Adapter::cancel(quint64 requestId)
{
    if (m_byExternalId.contains(requestId)) {
        m_byExternalId[requestId].cancelled = true;
        const quint64 intId = m_byExternalId[requestId].internalId;
        if (m_byInternalId.contains(intId)) {
            m_byInternalId[intId].cancelled = true;
        }
    }
    if (m_continuation) {
        m_continuation->cancelRequests();
    }
}

void LegacyProviderV1Adapter::onSearchCompleted(const QString &providerId, const QList<SearchResult> &results)
{
    Q_UNUSED(providerId);
    // Locate active search request
    quint64 foundExtId = 0;
    for (auto it = m_byExternalId.begin(); it != m_byExternalId.end(); ++it) {
        if (it.value().type == RequestType::Search && !it.value().cancelled) {
            foundExtId = it.key();
            break;
        }
    }
    if (foundExtId == 0) return;
    auto req = m_byExternalId.take(foundExtId);
    m_byInternalId.remove(req.internalId);
    if (req.cancelled) return;

    SearchPage page;
    page.results = results;
    emit pageReady(foundExtId, page);
}

void LegacyProviderV1Adapter::onSearchFailed(const QString &providerId, const QString &errorString)
{
    Q_UNUSED(providerId);
    quint64 foundExtId = 0;
    for (auto it = m_byExternalId.begin(); it != m_byExternalId.end(); ++it) {
        if (it.value().type == RequestType::Search && !it.value().cancelled) {
            foundExtId = it.key();
            break;
        }
    }
    if (foundExtId == 0) return;
    auto req = m_byExternalId.take(foundExtId);
    m_byInternalId.remove(req.internalId);
    if (req.cancelled) return;

    emit pageFailed(foundExtId, ErrorCode::InternalError, errorString);
}

void LegacyProviderV1Adapter::onPageCompleted(quint64 reqId, const SearchPage &page)
{
    if (!m_byInternalId.contains(reqId)) return;
    auto req = m_byInternalId.take(reqId);
    m_byExternalId.remove(req.externalId);
    if (req.cancelled) return;

    emit pageReady(req.externalId, page);
}

void LegacyProviderV1Adapter::onDetailsCompleted(quint64 reqId, const SearchDetails &details)
{
    if (!m_byInternalId.contains(reqId)) return;
    auto req = m_byInternalId.take(reqId);
    m_byExternalId.remove(req.externalId);
    if (req.cancelled) return;

    if (!req.targetId.isEmpty()) {
        m_cachedDetails.insert(req.targetId, details);
    }

    if (req.type == RequestType::Content) {
        ContentResponse resp;
        resp.resultId = req.targetId;
        resp.content = details.content;
        emit contentReady(req.externalId, resp);
    } else {
        emit detailsReady(req.externalId, req.targetId, details);
    }
}

void LegacyProviderV1Adapter::onRequestFailed(quint64 reqId, const QString &error, int retryAfterMs)
{
    if (!m_byInternalId.contains(reqId)) return;
    auto req = m_byInternalId.take(reqId);
    m_byExternalId.remove(req.externalId);
    if (req.cancelled) return;

    ErrorCode code = retryAfterMs > 0 ? ErrorCode::RateLimited : ErrorCode::InternalError;
    if (req.type == RequestType::Search || req.type == RequestType::Page) {
        emit pageFailed(req.externalId, code, error);
    } else if (req.type == RequestType::Details) {
        emit detailsFailed(req.externalId, req.targetId, code, error);
    } else if (req.type == RequestType::Content) {
        emit contentFailed(req.externalId, req.targetId, code, error);
    }
}

// -----------------------------------------------------------------------------
// ProviderV1ToLegacyAdapter
// -----------------------------------------------------------------------------

ProviderV1ToLegacyAdapter::ProviderV1ToLegacyAdapter(ProviderV1 *providerV1, QObject *parent)
    : SearchProvider(parent)
    , m_v1(providerV1)
{
    if (m_v1) {
        connect(m_v1, &ProviderV1::pageReady, this, &ProviderV1ToLegacyAdapter::onPageReady);
        connect(m_v1, &ProviderV1::pageFailed, this, &ProviderV1ToLegacyAdapter::onPageFailed);
        connect(m_v1, &ProviderV1::detailsReady, this, &ProviderV1ToLegacyAdapter::onDetailsReady);
        connect(m_v1, &ProviderV1::detailsFailed, this, &ProviderV1ToLegacyAdapter::onDetailsFailed);
        connect(m_v1, &ProviderV1::contentReady, this, &ProviderV1ToLegacyAdapter::onContentReady);
        connect(m_v1, &ProviderV1::contentFailed, this, &ProviderV1ToLegacyAdapter::onContentFailed);
    }
}

ProviderV1ToLegacyAdapter::~ProviderV1ToLegacyAdapter()
{
}

QString ProviderV1ToLegacyAdapter::id() const
{
    return m_v1 ? m_v1->id() : QString();
}

QString ProviderV1ToLegacyAdapter::name() const
{
    return m_v1 ? m_v1->name() : QString();
}

void ProviderV1ToLegacyAdapter::search(const QString &query)
{
    if (!m_v1) return;
    const quint64 reqId = ++m_searchCounter;
    m_activeSearchId = reqId;
    m_v1->search(query, reqId);
}

SearchProvider *ProviderV1ToLegacyAdapter::clone(QObject *parent) const
{
    Q_UNUSED(parent);
    return nullptr;
}

void ProviderV1ToLegacyAdapter::requestPage(const QString &query, const QString &token, quint64 requestId)
{
    if (!m_v1) return;
    m_activePageRequests.insert(requestId, query);
    m_v1->getPage(query, token, requestId);
}

bool ProviderV1ToLegacyAdapter::supportsDetails() const
{
    return m_v1 ? m_v1->capabilities().details : false;
}

void ProviderV1ToLegacyAdapter::requestDetails(const QString &resultId, const QString &token, quint64 requestId)
{
    Q_UNUSED(token);
    if (!m_v1) return;
    m_activeDetailsRequests.insert(requestId, resultId);
    m_v1->getDetails(resultId, requestId);
}

void ProviderV1ToLegacyAdapter::cancelRequests()
{
    if (!m_v1) return;
    if (m_activeSearchId != 0) {
        m_v1->cancel(m_activeSearchId);
        m_activeSearchId = 0;
    }
    for (auto it = m_activePageRequests.keyBegin(); it != m_activePageRequests.keyEnd(); ++it) {
        m_v1->cancel(*it);
    }
    m_activePageRequests.clear();
    for (auto it = m_activeDetailsRequests.keyBegin(); it != m_activeDetailsRequests.keyEnd(); ++it) {
        m_v1->cancel(*it);
    }
    m_activeDetailsRequests.clear();
}

void ProviderV1ToLegacyAdapter::onPageReady(quint64 reqId, const SearchPage &page)
{
    if (reqId == m_activeSearchId) {
        m_activeSearchId = 0;
        emit searchCompleted(id(), page.results);
    }
    if (m_activePageRequests.contains(reqId)) {
        m_activePageRequests.remove(reqId);
        emit pageCompleted(reqId, page);
    }
}

void ProviderV1ToLegacyAdapter::onPageFailed(quint64 reqId, ErrorCode code, const QString &message)
{
    if (reqId == m_activeSearchId) {
        m_activeSearchId = 0;
        emit searchFailed(id(), message);
    }
    if (m_activePageRequests.contains(reqId)) {
        m_activePageRequests.remove(reqId);
        emit requestFailed(reqId, message, code == ErrorCode::RateLimited ? 1000 : 0);
    }
}

void ProviderV1ToLegacyAdapter::onDetailsReady(quint64 reqId, const QString &resultId, const SearchDetails &details)
{
    Q_UNUSED(resultId);
    if (m_activeDetailsRequests.contains(reqId)) {
        m_activeDetailsRequests.remove(reqId);
        emit detailsCompleted(reqId, details);
    }
}

void ProviderV1ToLegacyAdapter::onDetailsFailed(quint64 reqId, const QString &resultId, ErrorCode code, const QString &message)
{
    Q_UNUSED(resultId);
    if (m_activeDetailsRequests.contains(reqId)) {
        m_activeDetailsRequests.remove(reqId);
        emit requestFailed(reqId, message, code == ErrorCode::RateLimited ? 1000 : 0);
    }
}

void ProviderV1ToLegacyAdapter::onContentReady(quint64 reqId, const ContentResponse &response)
{
    if (m_activeDetailsRequests.contains(reqId)) {
        m_activeDetailsRequests.remove(reqId);
        SearchDetails det;
        det.result.id = response.resultId;
        det.content = response.content;
        emit detailsCompleted(reqId, det);
    }
}

void ProviderV1ToLegacyAdapter::onContentFailed(quint64 reqId, const QString &resultId, ErrorCode code, const QString &message)
{
    Q_UNUSED(resultId);
    if (m_activeDetailsRequests.contains(reqId)) {
        m_activeDetailsRequests.remove(reqId);
        emit requestFailed(reqId, message, code == ErrorCode::RateLimited ? 1000 : 0);
    }
}

} // namespace ProviderApiV1
