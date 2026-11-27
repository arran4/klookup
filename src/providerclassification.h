// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PROVIDERCLASSIFICATION_H
#define PROVIDERCLASSIFICATION_H
#include <QJsonValue>
#include <QMetaType>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QLocale>
#include <utility>

// Provider-level descriptors, never result/story taxonomy or authorization.
struct ProviderClassification {
    static constexpr const char *LocalVirtualFacet = "scope:local";
    QString version = QStringLiteral("1.0.0");
    QStringList facets;
    QStringList requirements;
    QMap<QString, QMap<QString, QString>> labels;
    QString label(const QString &id, const QLocale &locale = QLocale()) const;
    QString description() const;
    void addHostVirtualFacet(const QString &facet);
    static ProviderClassification builtin(const QString &providerId);
    static bool fromJson(const QJsonValue &value, ProviderClassification *result, QString *error);
};
Q_DECLARE_METATYPE(ProviderClassification)

struct QueryScope {
    bool restricted = false;
    QSet<QString> sourceIds;
    static QueryScope all() { return {false, {}}; }
    static QueryScope only(QSet<QString> ids) { return {true, std::move(ids)}; }
    bool includes(const QString &id) const { return !restricted || sourceIds.contains(id); }
    bool operator==(const QueryScope &other) const { return restricted == other.restricted && sourceIds == other.sourceIds; }
    bool operator!=(const QueryScope &other) const { return !(*this == other); }
};
Q_DECLARE_METATYPE(QueryScope)
#endif
