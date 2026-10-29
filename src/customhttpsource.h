// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUSTOMHTTPSOURCE_H
#define CUSTOMHTTPSOURCE_H

#include "searchprovider.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QUrl>

struct CustomHttpFieldMapping {
    QString resultsRoot;       // e.g. "results" or "items" or "" for root array
    QString idField;           // e.g. "id"
    QString titleField;        // e.g. "title" or "name"
    QString urlField;          // e.g. "url" or "link"
    QString descriptionField;  // e.g. "description" or "summary"
    QString typeField;         // e.g. "type" or "category"

    QJsonObject toJson() const;
    static CustomHttpFieldMapping fromJson(const QJsonObject &obj);
};

struct CustomHttpSourceDefinition {
    enum class Method {
        Get,
        Post
    };

    QString id;
    QString name;
    QString description;
    bool enabled = true;
    bool trusted = true;            // Untrusted by default for imports or when origin changes
    bool hasSecret = false;          // Whether this source requires secret credentials
    Method method = Method::Get;
    QString urlTemplate;            // e.g. "https://api.example.com/search?q={query}"
    QMap<QString, QString> headers; // Header name -> value/template
    QString authHeader;             // e.g. "Authorization"
    QString authSecretToken;        // Template like "Bearer {secret}" or raw key
    QString postContentType;        // e.g. "application/json" or "application/x-www-form-urlencoded"
    QString bodyTemplate;           // e.g. "{\"query\": \"{query}\"}"

    CustomHttpFieldMapping mapping;

    bool requiresSecret() const;
    QString executionFingerprint() const;
    QString originString() const;

    bool isValid(QString *errorMsg = nullptr) const;
    QJsonObject toJson(bool includeSecret = false) const;
    static CustomHttpSourceDefinition fromJson(const QJsonObject &obj, bool fromImport = false);
};

namespace CustomHttpUtils {
    QString urlEncodeQuery(const QString &query);
    QString jsonEscapeQuery(const QString &query);
    QString formatUrl(const QString &urlTemplate, const QString &query);
    QByteArray formatBody(const QString &bodyTemplate, const QString &postContentType, const QString &query);
    QJsonValue extractPath(const QJsonValue &root, const QString &path);
    QList<SearchResult> extractResults(const QJsonDocument &doc, const CustomHttpFieldMapping &mapping, const QString &providerId = QString());
}

#endif // CUSTOMHTTPSOURCE_H
