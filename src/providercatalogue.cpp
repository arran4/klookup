// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providercatalogue.h"
#include "providerpackage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace
{
bool isSafeId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral(R"(^[a-z0-9][a-z0-9._-]{0,63}$)"));
    return re.match(id).hasMatch() && !id.contains(QStringLiteral(".."));
}

bool isAllowedUrl(const QUrl &url, bool allowLocalFileUrls)
{
    if (!url.isValid() || url.isEmpty())
        return false;
    if (url.scheme() == QLatin1String("https"))
        return !url.host().isEmpty();
    return allowLocalFileUrls && url.scheme() == QLatin1String("file");
}

bool isValidSha256Hex(const QString &hex)
{
    static const QRegularExpression re(QStringLiteral(R"(^[0-9a-f]{64}$)"));
    return re.match(hex.toLower()).hasMatch();
}

QStringList readStringArray(const QJsonValue &val)
{
    QStringList out;
    if (!val.isArray())
        return out;
    for (const QJsonValue &v : val.toArray()) {
        if (v.isString())
            out.append(v.toString());
    }
    return out;
}

bool copyDirRecursive(const QString &src, const QString &dst, QString *error)
{
    QDir srcDir(src);
    if (!srcDir.exists()) {
        if (error)
            *error = QStringLiteral("Source directory does not exist");
        return false;
    }
    if (!QDir().mkpath(dst)) {
        if (error)
            *error = QStringLiteral("Failed to create destination directory");
        return false;
    }
    QDirIterator it(src, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString rel = srcDir.relativeFilePath(info.filePath());
        const QString targetPath = dst + QLatin1Char('/') + rel;
        if (info.isDir()) {
            if (!QDir().mkpath(targetPath)) {
                if (error)
                    *error = QStringLiteral("Failed to create directory %1").arg(rel);
                return false;
            }
        } else if (info.isFile()) {
            QDir().mkpath(QFileInfo(targetPath).absolutePath());
            if (!QFile::copy(info.filePath(), targetPath)) {
                if (error)
                    *error = QStringLiteral("Failed to copy %1").arg(rel);
                return false;
            }
        }
    }
    return true;
}
} // namespace

bool ProviderCatalogue::parseIndex(const QByteArray &jsonBytes,
                                   CatalogueIndex *out,
                                   QString *error,
                                   bool allowLocalFileUrls)
{
    if (!out)
        return false;
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid catalogue JSON: %1").arg(parseErr.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    const int schema = root.value(QStringLiteral("schemaVersion")).toInt(0);
    if (schema != 1) {
        if (error)
            *error = QStringLiteral("Unsupported catalogue schemaVersion: %1").arg(schema);
        return false;
    }

    CatalogueIndex idx;
    idx.schemaVersion = schema;
    idx.generatedAt = root.value(QStringLiteral("generatedAt")).toString();
    idx.revokedIds = readStringArray(root.value(QStringLiteral("revokedIds")));

    const QJsonArray arr = root.value(QStringLiteral("providers")).toArray();
    for (const QJsonValue &val : arr) {
        if (!val.isObject()) {
            if (error)
                *error = QStringLiteral("Catalogue provider entry must be an object");
            return false;
        }
        const QJsonObject obj = val.toObject();
        CatalogueEntry e;
        e.id = obj.value(QStringLiteral("id")).toString();
        e.name = obj.value(QStringLiteral("name")).toString();
        e.description = obj.value(QStringLiteral("description")).toString();
        e.version = obj.value(QStringLiteral("version")).toString();
        e.minApiVersion = obj.value(QStringLiteral("minApiVersion")).toInt(1);
        e.maxApiVersion = obj.value(QStringLiteral("maxApiVersion")).toInt(1);
        e.author = obj.value(QStringLiteral("author")).toString();
        e.license = obj.value(QStringLiteral("license")).toString();
        e.repositoryUrl = QUrl(obj.value(QStringLiteral("repositoryUrl")).toString());
        e.packageUrl = QUrl(obj.value(QStringLiteral("packageUrl")).toString());
        e.sha256Hex = obj.value(QStringLiteral("sha256")).toString().toLower();
        e.languages = readStringArray(obj.value(QStringLiteral("languages")));
        e.capabilities = readStringArray(obj.value(QStringLiteral("capabilities")));
        e.origins = readStringArray(obj.value(QStringLiteral("origins")));
        e.secrets = readStringArray(obj.value(QStringLiteral("secrets")));
        e.revoked = obj.value(QStringLiteral("revoked")).toBool(false);
        e.revocationReason = obj.value(QStringLiteral("revocationReason")).toString();

        if (!isSafeId(e.id) || e.name.isEmpty() || e.version.isEmpty()) {
            if (error)
                *error = QStringLiteral("Invalid provider metadata for id '%1'").arg(e.id);
            return false;
        }
        if (!isAllowedUrl(e.repositoryUrl, allowLocalFileUrls) || !isAllowedUrl(e.packageUrl, allowLocalFileUrls)) {
            if (error)
                *error = QStringLiteral("Provider '%1' has disallowed or non-HTTPS URL").arg(e.id);
            return false;
        }
        if (!isValidSha256Hex(e.sha256Hex)) {
            if (error)
                *error = QStringLiteral("Provider '%1' is missing a valid 64-hex SHA-256 digest").arg(e.id);
            return false;
        }
        idx.entries.append(e);
    }

    *out = idx;
    return true;
}

bool ProviderCatalogue::isCompatibleWithHost(const CatalogueEntry &entry, int hostApiVersion)
{
    return entry.minApiVersion <= hostApiVersion && hostApiVersion <= entry.maxApiVersion;
}

bool ProviderCatalogue::isRevoked(const CatalogueIndex &index, const CatalogueEntry &entry, QString *reason)
{
    if (entry.revoked) {
        if (reason)
            *reason = entry.revocationReason.isEmpty() ? QStringLiteral("Entry revoked in catalogue") : entry.revocationReason;
        return true;
    }
    if (index.revokedIds.contains(entry.id)) {
        if (reason)
            *reason = QStringLiteral("Provider ID revoked in catalogue");
        return true;
    }
    return false;
}

QString ProviderCatalogue::computeDirectoryDigestHex(const QString &packageDir, QString *error)
{
    QDir root(packageDir);
    if (!root.exists()) {
        if (error)
            *error = QStringLiteral("Package directory does not exist");
        return {};
    }
    QStringList files;
    QDirIterator it(packageDir, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        files.append(root.relativeFilePath(it.filePath()));
    }
    files.sort();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString &rel : files) {
        QFile f(root.filePath(rel));
        if (!f.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Cannot read %1 for digest").arg(rel);
            return {};
        }
        hash.addData(rel.toUtf8());
        hash.addData(QByteArrayView("\0", 1));
        hash.addData(f.readAll());
        hash.addData(QByteArrayView("\0", 1));
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool ProviderCatalogue::installLocalPackage(const CatalogueEntry &entry,
                                            const QString &sourcePackageDir,
                                            const QString &installRoot,
                                            const QString &backupRoot,
                                            QString *error,
                                            int hostApiVersion)
{
    if (!isSafeId(entry.id)) {
        if (error)
            *error = QStringLiteral("Unsafe provider ID");
        return false;
    }
    if (entry.revoked) {
        if (error)
            *error = QStringLiteral("Refusing to install revoked provider");
        return false;
    }
    if (!isCompatibleWithHost(entry, hostApiVersion)) {
        if (error)
            *error = QStringLiteral("Provider API range incompatible with host");
        return false;
    }
    const QString actualDigest = computeDirectoryDigestHex(sourcePackageDir, error);
    if (actualDigest.isEmpty() || actualDigest != entry.sha256Hex.toLower()) {
        if (error)
            *error = QStringLiteral("SHA-256 verification failed for %1").arg(entry.id);
        return false;
    }

    const ProviderPackage pkg = ProviderPackage::fromDirectory(sourcePackageDir, ProviderPackage::LocationType::User);
    if (!pkg.isValid()) {
        if (error)
            *error = QStringLiteral("Invalid provider package: %1").arg(pkg.validationError());
        return false;
    }
    if (pkg.id() != entry.id || pkg.manifest().version != entry.version) {
        if (error)
            *error = QStringLiteral("Manifest identity/version does not match catalogue entry");
        return false;
    }
    const QSet<QString> manifestOrigins(pkg.manifest().permissions.origins.cbegin(), pkg.manifest().permissions.origins.cend());
    const QSet<QString> catalogueOrigins(entry.origins.cbegin(), entry.origins.cend());
    if (manifestOrigins != catalogueOrigins) {
        if (error)
            *error = QStringLiteral("Manifest permissions do not match catalogue declaration");
        return false;
    }

    const QString targetDir = QDir(installRoot).filePath(entry.id);
    const QString backupDir = QDir(backupRoot).filePath(entry.id);
    if (QFileInfo::exists(targetDir)) {
        QDir(backupDir).removeRecursively();
        if (!copyDirRecursive(targetDir, backupDir, error))
            return false;
        QDir(targetDir).removeRecursively();
    }
    if (!copyDirRecursive(sourcePackageDir, targetDir, error)) {
        if (QFileInfo::exists(backupDir)) {
            QDir(targetDir).removeRecursively();
            copyDirRecursive(backupDir, targetDir, nullptr);
        }
        return false;
    }
    return true;
}

bool ProviderCatalogue::rollbackPackage(const QString &providerId,
                                        const QString &installRoot,
                                        const QString &backupRoot,
                                        QString *error)
{
    if (!isSafeId(providerId)) {
        if (error)
            *error = QStringLiteral("Unsafe provider ID");
        return false;
    }
    const QString targetDir = QDir(installRoot).filePath(providerId);
    const QString backupDir = QDir(backupRoot).filePath(providerId);
    if (!QFileInfo::exists(backupDir)) {
        if (error)
            *error = QStringLiteral("No rollback backup available for %1").arg(providerId);
        return false;
    }
    QDir(targetDir).removeRecursively();
    return copyDirRecursive(backupDir, targetDir, error);
}
