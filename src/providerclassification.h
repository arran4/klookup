// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PROVIDERCLASSIFICATION_H
#define PROVIDERCLASSIFICATION_H
#include <QJsonValue>
#include <QMetaType>
#include <QMap>
#include <QStringList>
#include <QLocale>

// Provider-level descriptors, never result/story taxonomy or authorization.
struct ProviderClassification {
    QString version = QStringLiteral("1.0.0");
    QStringList facets;
    QStringList requirements;
    QMap<QString, QMap<QString, QString>> labels;
    QString label(const QString &id, const QLocale &locale = QLocale()) const;
    QString description() const;
    static ProviderClassification builtin(const QString &providerId);
    static bool fromJson(const QJsonValue &value, ProviderClassification *result, QString *error);
};
Q_DECLARE_METATYPE(ProviderClassification)
#endif
