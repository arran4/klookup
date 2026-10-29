// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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
#include <QStandardPaths>

bool ProviderPackageDiscovery::isValidPackageId(const QString &id)
{
    static const QRegularExpression idRegex(QStringLiteral("^[a-zA-Z0-9_-]{1,64}$"));
    return idRegex.match(id).hasMatch();
}

bool ProviderPackage::isOriginAllowed(const QUrl &url) const
{
    if (!url.isValid())
        return false;
    const QString targetScheme = url.scheme().toLower();
    const QString targetHost = url.host().toLower();
    if (targetScheme.isEmpty() || targetHost.isEmpty())
        return false;
    const int targetPort = url.port(targetScheme == QStringLiteral("https") ? 443 : (targetScheme == QStringLiteral("http") ? 80 : -1));
    if (targetPort <= 0)
        return false;

    for (const auto &allowed : m_manifest.permissions.origins) {
        const QString pattern = allowed.trimmed();
        if (pattern.isEmpty())
            continue;

        QString patternScheme;
        QString patternHost;
        int patternPort = -1;

        if (pattern.contains(QStringLiteral("://"))) {
            QUrl patternUrl(pattern);
            if (!patternUrl.isValid())
                continue;
            patternScheme = patternUrl.scheme().toLower();
            patternHost = patternUrl.host().toLower();
            patternPort = patternUrl.port(patternScheme == QStringLiteral("https") ? 443 : (patternScheme == QStringLiteral("http") ? 80 : -1));
        } else {
            // If scheme is omitted, require https on port 443 by default
            patternScheme = QStringLiteral("https");
            const int colonIdx = pattern.indexOf(QLatin1Char(':'));
            if (colonIdx != -1) {
                patternHost = pattern.left(colonIdx).toLower();
                patternPort = pattern.mid(colonIdx + 1).toInt();
            } else {
                patternHost = pattern.toLower();
                patternPort = 443;
            }
        }

        // Scheme check: exact match (prevent scheme downgrade)
        if (targetScheme != patternScheme)
            continue;

        // Effective port check
        if (targetPort != patternPort)
            continue;

        // Host check: exact or wildcard (*.example.com)
        if (patternHost == targetHost)
            return true;

        if (patternHost.startsWith(QStringLiteral("*."))) {
            const QString domain = patternHost.mid(2);
            if (targetHost == domain || targetHost.endsWith(QLatin1Char('.') + domain))
                return true;
        }
    }
    return false;
}

bool ProviderPackage::isSecretAllowed(const QString &secretKey) const
{
    return m_manifest.permissions.secrets.contains(secretKey);
}

ProviderPackage ProviderPackage::fromDirectory(const QString &dirPath, LocationType locType)
{
    ProviderPackage pkg;
    pkg.m_locationType = locType;
    pkg.m_packageDir = QDir::cleanPath(dirPath);

    QDir dir(pkg.m_packageDir);
    if (!dir.exists()) {
        pkg.m_validationError = QStringLiteral("Directory does not exist: %1").arg(pkg.m_packageDir);
        return pkg;
    }

    const QString canonicalPackageDir = QFileInfo(pkg.m_packageDir).canonicalFilePath();
    if (canonicalPackageDir.isEmpty()) {
        pkg.m_validationError = QStringLiteral("Cannot resolve canonical path for %1").arg(pkg.m_packageDir);
        return pkg;
    }

    const QString manifestPath = dir.filePath(QStringLiteral("provider.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.exists()) {
        pkg.m_validationError = QStringLiteral("provider.json not found in %1").arg(pkg.m_packageDir);
        return pkg;
    }

    if (manifestFile.size() > 1024 * 1024) {
        pkg.m_validationError = QStringLiteral("provider.json exceeds 1 MiB size limit");
        return pkg;
    }

    if (!manifestFile.open(QIODevice::ReadOnly)) {
        pkg.m_validationError = QStringLiteral("Failed to open provider.json: %1").arg(manifestFile.errorString());
        return pkg;
    }

    const QByteArray manifestBytes = manifestFile.readAll();
    manifestFile.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        pkg.m_validationError = QStringLiteral("JSON parse error in provider.json: %1").arg(parseError.errorString());
        return pkg;
    }

    const QJsonObject root = doc.object();
    pkg.m_manifest.id = root.value(QStringLiteral("id")).toString().trimmed();
    pkg.m_manifest.name = root.value(QStringLiteral("name")).toString().trimmed();
    pkg.m_manifest.description = root.value(QStringLiteral("description")).toString().trimmed();
    pkg.m_manifest.version = root.value(QStringLiteral("version")).toString().trimmed();
    pkg.m_manifest.author = root.value(QStringLiteral("author")).toString().trimmed();
    pkg.m_manifest.license = root.value(QStringLiteral("license")).toString().trimmed();
    pkg.m_manifest.runtime = root.value(QStringLiteral("runtime")).toString(QStringLiteral("javascript")).trimmed();
    pkg.m_manifest.entry = root.value(QStringLiteral("entry")).toString(QStringLiteral("main.mjs")).trimmed();

    if (!ProviderPackageDiscovery::isValidPackageId(pkg.m_manifest.id)) {
        pkg.m_validationError = QStringLiteral("Invalid package ID '%1'").arg(pkg.m_manifest.id);
        return pkg;
    }

    if (pkg.m_manifest.name.isEmpty()) {
        pkg.m_validationError = QStringLiteral("Package name is required");
        return pkg;
    }

    if (pkg.m_manifest.entry.isEmpty()) {
        pkg.m_validationError = QStringLiteral("Package entry is required");
        return pkg;
    }

    // Verify entry file path resolution and check against path traversal
    const QString entryPath = dir.filePath(pkg.m_manifest.entry);
    const QFileInfo entryInfo(entryPath);
    if (!entryInfo.exists()) {
        pkg.m_validationError = QStringLiteral("Entry file does not exist: %1").arg(pkg.m_manifest.entry);
        return pkg;
    }

    const QString canonicalEntryPath = entryInfo.canonicalFilePath();
    if (!canonicalEntryPath.startsWith(canonicalPackageDir + QLatin1Char('/')) && canonicalEntryPath != canonicalPackageDir) {
        pkg.m_validationError = QStringLiteral("Entry file escapes package directory (symlink traversal blocked)");
        return pkg;
    }
    pkg.m_entryFilePath = canonicalEntryPath;

    // Parse permissions
    if (root.contains(QStringLiteral("permissions"))) {
        const QJsonObject perms = root.value(QStringLiteral("permissions")).toObject();
        for (const auto &v : perms.value(QStringLiteral("origins")).toArray()) {
            pkg.m_manifest.permissions.origins.append(v.toString().trimmed());
        }
        for (const auto &v : perms.value(QStringLiteral("secrets")).toArray()) {
            pkg.m_manifest.permissions.secrets.append(v.toString().trimmed());
        }
    }

    // Parse capabilities
    if (root.contains(QStringLiteral("capabilities"))) {
        pkg.m_manifest.capabilities = ProviderApiV1::capabilitiesFromJson(root.value(QStringLiteral("capabilities")).toObject());
    }

    if (root.contains(QStringLiteral("defaultSettings"))) {
        pkg.m_manifest.defaultSettings = root.value(QStringLiteral("defaultSettings")).toObject();
    }

    // Calculate package content hash over manifest and all script/json assets in package directory
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QDirIterator it(canonicalPackageDir,
                    QStringList{QStringLiteral("*.json"), QStringLiteral("*.js"), QStringLiteral("*.mjs"), QStringLiteral("*.cjs")},
                    QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);
    QStringList sortedRelPaths;
    while (it.hasNext()) {
        it.next();
        sortedRelPaths.append(dir.relativeFilePath(it.filePath()));
    }
    sortedRelPaths.sort();
    for (const auto &rel : sortedRelPaths) {
        QFile f(dir.filePath(rel));
        if (f.open(QIODevice::ReadOnly)) {
            hash.addData(rel.toUtf8());
            hash.addData(f.readAll());
        }
    }
    pkg.m_contentHash = hash.result();

    pkg.m_valid = true;
    return pkg;
}

QStringList ProviderPackageDiscovery::defaultSearchPaths()
{
    QStringList paths;

    // 1. User location: ${XDG_DATA_HOME:-~/.local/share}/klookup/providers.d
    const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    const QString userDataDir = !dataHome.isEmpty()
                                    ? dataHome
                                    : (QDir::homePath() + QStringLiteral("/.local/share"));
    paths.append(QDir::cleanPath(userDataDir + QStringLiteral("/klookup/providers.d")));

    // 2. System locations: ${XDG_DATA_DIRS:-/usr/local/share:/usr/share}/klookup/providers.d
    const QString dataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
    const QStringList sysDirs = !dataDirs.isEmpty()
                                    ? dataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts)
                                    : QStringList{QStringLiteral("/usr/local/share"), QStringLiteral("/usr/share")};
    for (const auto &dir : sysDirs) {
        paths.append(QDir::cleanPath(dir + QStringLiteral("/klookup/providers.d")));
    }

    paths.removeDuplicates();
    return paths;
}

QList<ProviderPackage> ProviderPackageDiscovery::discoverPackages(const QStringList &customPaths)
{
    QList<ProviderPackage> results;
    QHash<QString, ProviderPackage> discoveredById;

    auto scanDir = [&](const QString &basePath, ProviderPackage::LocationType locType) {
        QDir dir(basePath);
        if (!dir.exists())
            return;

        const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
        for (const auto &info : entries) {
            // Passive scan: only inspect immediate subdirectory
            const QString pkgDir = info.absoluteFilePath();
            auto pkg = ProviderPackage::fromDirectory(pkgDir, locType);
            if (pkg.isValid()) {
                if (!discoveredById.contains(pkg.id())) {
                    discoveredById.insert(pkg.id(), pkg);
                    results.append(pkg);
                }
            }
        }
    };

    // 1. Custom development/test paths (highest precedence)
    for (const auto &p : customPaths) {
        scanDir(p, ProviderPackage::LocationType::Custom);
    }

    // 2. Default XDG search paths (User then System)
    const QStringList defaults = defaultSearchPaths();
    bool isFirst = true;
    for (const auto &p : defaults) {
        const auto locType = isFirst ? ProviderPackage::LocationType::User : ProviderPackage::LocationType::System;
        scanDir(p, locType);
        isFirst = false;
    }

    return results;
}
