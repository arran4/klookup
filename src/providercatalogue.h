// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROVIDERCATALOGUE_H
#define PROVIDERCATALOGUE_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

struct CatalogueEntry {
    QString id;
    QString name;
    QString description;
    QString version;
    int minApiVersion = 1;
    int maxApiVersion = 1;
    QString author;
    QString license;
    QUrl repositoryUrl;
    QUrl packageUrl;
    QString sha256Hex;
    QStringList languages;
    QStringList capabilities;
    QStringList origins;
    QStringList secrets;
    bool revoked = false;
    QString revocationReason;
};

struct CatalogueIndex {
    int schemaVersion = 1;
    QString generatedAt;
    QList<CatalogueEntry> entries;
    QStringList revokedIds;
};

namespace ProviderCatalogue
{
constexpr int MaxPackageFiles = 256;
constexpr qint64 MaxPackageFileBytes = 2 * 1024 * 1024;
constexpr qint64 MaxPackageTotalBytes = 16 * 1024 * 1024;

bool parseIndex(const QByteArray &jsonBytes,
                CatalogueIndex *out,
                QString *error,
                bool allowLocalFileUrls = false);

bool isCompatibleWithHost(const CatalogueEntry &entry, int hostApiVersion = 1);
bool isRevoked(const CatalogueIndex &index, const CatalogueEntry &entry, QString *reason = nullptr);
QString computeDirectoryDigestHex(const QString &packageDir, QString *error = nullptr);

bool installLocalPackage(const CatalogueIndex &index,
                         const CatalogueEntry &entry,
                         const QString &sourcePackageDir,
                         const QString &installRoot,
                         const QString &backupRoot,
                         QString *error,
                         int hostApiVersion = 1);

bool rollbackPackage(const QString &providerId,
                     const QString &installRoot,
                     const QString &backupRoot,
                     QString *error);
} // namespace ProviderCatalogue

#endif // PROVIDERCATALOGUE_H
