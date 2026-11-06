// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providerauthentication.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

QStringList AuthenticationDescriptor::methods() const
{
    if (!declaredMethods.isEmpty()) return declaredMethods;
    switch (mode) {
    case AuthenticationMode::None: return {QStringLiteral("urn:klookup:auth:none")};
    case AuthenticationMode::ManualToken: return {QStringLiteral("urn:klookup:auth:token")};
    case AuthenticationMode::OAuth2Pkce: return {QStringLiteral("urn:klookup:auth:oauth2-pkce")};
    case AuthenticationMode::Unsupported: return {QStringLiteral("urn:klookup:auth:unsupported")};
    }
    return {};
}

bool AuthenticationDescriptor::fromManifest(const QJsonValue &value, AuthenticationDescriptor *result, QString *error)
{
    auto fail = [error](const QString &reason) { if (error) *error = reason; return false; };
    if (!value.isObject()) return fail(QStringLiteral("authentication must be an object"));
    const auto object = value.toObject();
    const QSet<QString> fields{QStringLiteral("version"), QStringLiteral("methods"), QStringLiteral("tokenLabel"), QStringLiteral("guidance")};
    for (auto it = object.begin(); it != object.end(); ++it) if (!fields.contains(it.key())) return fail(QStringLiteral("Unsupported authentication field"));
    if (object.value(QStringLiteral("version")).toString() != QLatin1String("1.0.0") || !object.value(QStringLiteral("methods")).isArray()) return fail(QStringLiteral("Unsupported authentication version or methods"));
    const auto methods = object.value(QStringLiteral("methods")).toArray();
    if (methods.isEmpty() || methods.size() > 8) return fail(QStringLiteral("Invalid authentication method count"));
    AuthenticationDescriptor parsed;
    // Script credential execution and production KWallet are tracked separately in #85.
    parsed.executionSupported = false; parsed.walletSupported = false;
    for (const auto &method : methods) {
        const auto id = method.toString();
        if (!method.isString() || !QRegularExpression(QStringLiteral("^urn:[a-z][a-z0-9:-]{1,95}$")).match(id).hasMatch()) return fail(QStringLiteral("Invalid authentication method identifier"));
        if (!parsed.declaredMethods.contains(id)) parsed.declaredMethods.append(id);
    }
    if (parsed.declaredMethods.contains(QStringLiteral("urn:klookup:auth:none")) && parsed.declaredMethods.size() != 1) return fail(QStringLiteral("No-authentication cannot be combined with other methods"));
    const auto single = parsed.declaredMethods.size() == 1 ? parsed.declaredMethods.first() : QString();
    if (single == QLatin1String("urn:klookup:auth:none")) parsed.mode = AuthenticationMode::None;
    else if (single == QLatin1String("urn:klookup:auth:token")) parsed.mode = AuthenticationMode::ManualToken;
    else if (single == QLatin1String("urn:klookup:auth:oauth2-pkce")) parsed.mode = AuthenticationMode::OAuth2Pkce;
    else parsed.mode = AuthenticationMode::Unsupported;
    for (const auto &key : {QStringLiteral("tokenLabel"), QStringLiteral("guidance")}) {
        if (!object.contains(key)) continue;
        const auto text = object.value(key).toString();
        if (!object.value(key).isString() || text.size() > 512 || text.contains(QChar::Null)) return fail(QStringLiteral("Invalid authentication text"));
        if (key == QLatin1String("tokenLabel")) parsed.tokenLabel = text;
        else parsed.guidance = text;
    }
    *result = parsed;
    return true;
}
