// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "customhttpsource.h"
#include "resultmetadata.h"
#include "sourceurl.h"

#include <QCryptographicHash>
#include <QRegularExpression>

QJsonObject CustomHttpFieldMapping::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("resultsRoot")] = resultsRoot;
    obj[QStringLiteral("idField")] = idField;
    obj[QStringLiteral("titleField")] = titleField;
    obj[QStringLiteral("urlField")] = urlField;
    obj[QStringLiteral("descriptionField")] = descriptionField;
    obj[QStringLiteral("typeField")] = typeField;
    return obj;
}

CustomHttpFieldMapping CustomHttpFieldMapping::fromJson(const QJsonObject &obj)
{
    CustomHttpFieldMapping mapping;
    mapping.resultsRoot = obj.value(QStringLiteral("resultsRoot")).toString();
    mapping.idField = obj.value(QStringLiteral("idField")).toString();
    mapping.titleField = obj.value(QStringLiteral("titleField")).toString();
    mapping.urlField = obj.value(QStringLiteral("urlField")).toString();
    mapping.descriptionField = obj.value(QStringLiteral("descriptionField")).toString();
    mapping.typeField = obj.value(QStringLiteral("typeField")).toString();
    return mapping;
}

static QString sanitizeSecretTemplate(const QString &input)
{
    static const QRegularExpression bearerRegex(QStringLiteral("Bearer\\s+[a-zA-Z0-9_\\-\\.]{16,}"), QRegularExpression::CaseInsensitiveOption);
    QString result = input;
    result.replace(bearerRegex, QStringLiteral("Bearer {secret}"));
    return result;
}

bool CustomHttpSourceDefinition::requiresSecret() const
{
    if (hasSecret) {
        return true;
    }
    if (!authHeader.trimmed().isEmpty()) {
        return true;
    }
    if (urlTemplate.contains(QStringLiteral("{secret}"))) {
        return true;
    }
    if (bodyTemplate.contains(QStringLiteral("{secret}"))) {
        return true;
    }
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        if (it.value().contains(QStringLiteral("{secret}"))) {
            return true;
        }
    }
    return false;
}

QString CustomHttpSourceDefinition::executionFingerprint() const
{
    QString data = QStringLiteral("%1|%2|%3|%4|%5|%6|")
                       .arg(static_cast<int>(method))
                       .arg(urlTemplate)
                       .arg(postContentType)
                       .arg(bodyTemplate)
                       .arg(authHeader)
                       .arg(authSecretToken);
    QStringList sortedHeaders;
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        sortedHeaders.append(QStringLiteral("%1:%2").arg(it.key(), it.value()));
    }
    sortedHeaders.sort();
    data += sortedHeaders.join(QLatin1Char(';'));
    return QString::fromLatin1(QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString CustomHttpSourceDefinition::originString() const
{
    const QUrl url(urlTemplate);
    if (!url.isValid()) {
        return QString();
    }
    return QStringLiteral("%1://%2%3").arg(url.scheme(), url.host(), url.port() > 0 ? QStringLiteral(":%1").arg(url.port()) : QString());
}

bool CustomHttpSourceDefinition::isValid(QString *errorMsg) const
{
    if (id.trimmed().isEmpty()) {
        if (errorMsg) *errorMsg = QStringLiteral("Provider ID cannot be empty.");
        return false;
    }

    static const QStringList BuiltInProviderIds = {
        QStringLiteral("wikipedia"),
        QStringLiteral("duckduckgo"),
        QStringLiteral("github"),
        QStringLiteral("openlibrary"),
        QStringLiteral("omdb"),
        QStringLiteral("themoviedb"),
        QStringLiteral("tvmaze"),
        QStringLiteral("spotify"),
        QStringLiteral("lyrics_ovh"),
        QStringLiteral("anilist"),
        QStringLiteral("myanimelist"),
        QStringLiteral("mangaupdates"),
        QStringLiteral("mangadex"),
        QStringLiteral("mangabaka")
    };
    if (BuiltInProviderIds.contains(id.trimmed().toLower())) {
        if (errorMsg) *errorMsg = QStringLiteral("Provider ID '%1' conflicts with a built-in provider.").arg(id);
        return false;
    }

    static const QRegularExpression validIdRegex(QStringLiteral("^[a-zA-Z0-9_\\-]+$"));
    if (!validIdRegex.match(id.trimmed()).hasMatch()) {
        if (errorMsg) *errorMsg = QStringLiteral("Provider ID must only contain alphanumeric characters, underscores, or dashes.");
        return false;
    }

    if (name.trimmed().isEmpty()) {
        if (errorMsg) *errorMsg = QStringLiteral("Source name cannot be empty.");
        return false;
    }
    if (urlTemplate.trimmed().isEmpty()) {
        if (errorMsg) *errorMsg = QStringLiteral("URL template cannot be empty.");
        return false;
    }

    QUrl testUrl(urlTemplate);
    if (!testUrl.isValid() || (testUrl.scheme() != QStringLiteral("http") && testUrl.scheme() != QStringLiteral("https"))) {
        if (errorMsg) *errorMsg = QStringLiteral("URL template must start with http:// or https://");
        return false;
    }
    if (testUrl.host().trimmed().isEmpty()) {
        if (errorMsg) *errorMsg = QStringLiteral("URL template must include a valid host name.");
        return false;
    }

    if (method == Method::Get && !urlTemplate.contains(QStringLiteral("{query}"))) {
        if (errorMsg) *errorMsg = QStringLiteral("GET URL template must contain {query} placeholder.");
        return false;
    }

    if (method == Method::Post) {
        if (postContentType.trimmed().isEmpty()) {
            if (errorMsg) *errorMsg = QStringLiteral("POST content type cannot be empty.");
            return false;
        }
        if (!urlTemplate.contains(QStringLiteral("{query}")) && !bodyTemplate.contains(QStringLiteral("{query}"))) {
            if (errorMsg) *errorMsg = QStringLiteral("POST requests must contain {query} placeholder in URL or body template.");
            return false;
        }
    }

    return true;
}

QJsonObject CustomHttpSourceDefinition::toJson(bool includeSecret) const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("description")] = description;
    obj[QStringLiteral("enabled")] = enabled;
    obj[QStringLiteral("trusted")] = trusted;
    obj[QStringLiteral("hasSecret")] = requiresSecret();
    obj[QStringLiteral("method")] = method == Method::Post ? QStringLiteral("POST") : QStringLiteral("GET");
    obj[QStringLiteral("urlTemplate")] = includeSecret ? urlTemplate : sanitizeSecretTemplate(urlTemplate);

    QJsonObject headersObj;
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        headersObj[it.key()] = includeSecret ? it.value() : sanitizeSecretTemplate(it.value());
    }
    obj[QStringLiteral("headers")] = headersObj;
    obj[QStringLiteral("authHeader")] = authHeader;
    if (includeSecret && !authSecretToken.isEmpty()) {
        obj[QStringLiteral("authSecretToken")] = authSecretToken;
    }
    obj[QStringLiteral("postContentType")] = postContentType;
    obj[QStringLiteral("bodyTemplate")] = includeSecret ? bodyTemplate : sanitizeSecretTemplate(bodyTemplate);
    obj[QStringLiteral("mapping")] = mapping.toJson();

    return obj;
}

CustomHttpSourceDefinition CustomHttpSourceDefinition::fromJson(const QJsonObject &obj, bool fromImport)
{
    CustomHttpSourceDefinition def;
    def.id = obj.value(QStringLiteral("id")).toString();
    def.name = obj.value(QStringLiteral("name")).toString();
    def.description = obj.value(QStringLiteral("description")).toString();
    if (fromImport) {
        // Newly imported definitions MUST be disabled and untrusted by default
        def.enabled = false;
        def.trusted = false;
    } else {
        def.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        def.trusted = obj.value(QStringLiteral("trusted")).toBool(true);
    }
    const QString methodStr = obj.value(QStringLiteral("method")).toString();
    def.method = methodStr.compare(QStringLiteral("POST"), Qt::CaseInsensitive) == 0 ? Method::Post : Method::Get;
    def.urlTemplate = obj.value(QStringLiteral("urlTemplate")).toString();

    QJsonObject headersObj = obj.value(QStringLiteral("headers")).toObject();
    for (auto it = headersObj.begin(); it != headersObj.end(); ++it) {
        def.headers.insert(it.key(), it.value().toString());
    }

    def.authHeader = obj.value(QStringLiteral("authHeader")).toString();
    def.authSecretToken = obj.value(QStringLiteral("authSecretToken")).toString();
    def.hasSecret = obj.value(QStringLiteral("hasSecret")).toBool(false) || def.requiresSecret();
    def.postContentType = obj.value(QStringLiteral("postContentType")).toString();
    def.bodyTemplate = obj.value(QStringLiteral("bodyTemplate")).toString();
    def.mapping = CustomHttpFieldMapping::fromJson(obj.value(QStringLiteral("mapping")).toObject());

    return def;
}

namespace CustomHttpUtils {

QString urlEncodeQuery(const QString &query)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(query));
}

QString jsonEscapeQuery(const QString &query)
{
    QString escaped;
    escaped.reserve(query.size() + 8);
    for (const QChar &c : query) {
        switch (c.unicode()) {
        case '"':  escaped += QStringLiteral("\\\""); break;
        case '\\': escaped += QStringLiteral("\\\\"); break;
        case '\b': escaped += QStringLiteral("\\b"); break;
        case '\f': escaped += QStringLiteral("\\f"); break;
        case '\n': escaped += QStringLiteral("\\n"); break;
        case '\r': escaped += QStringLiteral("\\r"); break;
        case '\t': escaped += QStringLiteral("\\t"); break;
        default:
            if (c.unicode() < 32) {
                escaped += QStringLiteral("\\u%1").arg(static_cast<uint>(c.unicode()), 4, 16, QLatin1Char('0'));
            } else {
                escaped += c;
            }
            break;
        }
    }
    return escaped;
}

QString formatUrl(const QString &urlTemplate, const QString &query)
{
    QString formatted = urlTemplate;
    formatted.replace(QStringLiteral("{query}"), urlEncodeQuery(query));
    return formatted;
}

QByteArray formatBody(const QString &bodyTemplate, const QString &postContentType, const QString &query)
{
    QString formatted = bodyTemplate;
    if (postContentType.contains(QStringLiteral("json"), Qt::CaseInsensitive)) {
        formatted.replace(QStringLiteral("{query}"), jsonEscapeQuery(query));
    } else {
        formatted.replace(QStringLiteral("{query}"), urlEncodeQuery(query));
    }
    return formatted.toUtf8();
}

QJsonValue extractPath(const QJsonValue &root, const QString &path)
{
    if (path.trimmed().isEmpty() || path == QStringLiteral(".")) {
        return root;
    }

    const QStringList parts = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    QJsonValue current = root;

    for (const QString &part : parts) {
        if (part.endsWith(QLatin1Char(']'))) {
            const int bracketIndex = part.indexOf(QLatin1Char('['));
            if (bracketIndex != -1) {
                const QString fieldName = part.left(bracketIndex);
                bool ok = false;
                const int arrayIndex = part.mid(bracketIndex + 1, part.length() - bracketIndex - 2).toInt(&ok);
                if (!fieldName.isEmpty()) {
                    current = current.toObject().value(fieldName);
                }
                if (ok && current.isArray()) {
                    current = current.toArray().at(arrayIndex);
                } else {
                    return QJsonValue();
                }
                continue;
            }
        }

        if (current.isObject()) {
            current = current.toObject().value(part);
        } else {
            return QJsonValue();
        }
    }

    return current;
}

QList<SearchResult> extractResults(const QJsonDocument &doc, const CustomHttpFieldMapping &mapping, const QString &providerId)
{
    QList<SearchResult> results;
    QJsonValue rootVal = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());

    QJsonValue arrayVal;
    if (mapping.resultsRoot.trimmed().isEmpty() || mapping.resultsRoot == QStringLiteral(".")) {
        arrayVal = rootVal;
    } else {
        arrayVal = extractPath(rootVal, mapping.resultsRoot);
    }

    auto parseItem = [&](const QJsonObject &itemObj, int index) {
        SearchResult res;
        if (!mapping.idField.isEmpty()) {
            res.id = extractPath(itemObj, mapping.idField).toVariant().toString();
        }
        if (res.id.isEmpty()) {
            res.id = QStringLiteral("%1-result-%2").arg(providerId.isEmpty() ? QStringLiteral("custom") : providerId).arg(index);
        }

        if (!mapping.titleField.isEmpty()) {
            res.title = extractPath(itemObj, mapping.titleField).toVariant().toString();
        }
        if (res.title.isEmpty()) {
            res.title = QStringLiteral("Result #%1").arg(index + 1);
        }

        if (!mapping.urlField.isEmpty()) {
            const QString rawUrl = extractPath(itemObj, mapping.urlField).toVariant().toString();
            const QUrl safe = Metadata::safeWebUrl(rawUrl);
            if (!safe.isEmpty()) {
                res.url = safe.toString(QUrl::FullyEncoded);
            }
        }

        if (!mapping.descriptionField.isEmpty()) {
            res.description = extractPath(itemObj, mapping.descriptionField).toVariant().toString();
        }

        if (!mapping.typeField.isEmpty()) {
            res.type = extractPath(itemObj, mapping.typeField).toVariant().toString();
        }

        results.append(res);
    };

    if (arrayVal.isArray()) {
        const QJsonArray arr = arrayVal.toArray();
        const int count = static_cast<int>(std::min<qsizetype>(arr.size(), 100));
        for (int i = 0; i < count; ++i) {
            if (arr.at(i).isObject()) {
                parseItem(arr.at(i).toObject(), i);
            }
        }
    } else if (arrayVal.isObject()) {
        parseItem(arrayVal.toObject(), 0);
    }

    return results;
}

} // namespace CustomHttpUtils
