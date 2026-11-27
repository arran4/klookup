// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providerpackage.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <KConfigGroup>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSet>

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
    pkg.m_packageDir = QDir::cleanPath(QFileInfo(dirPath).absoluteFilePath());
    pkg.m_manifest.id = QFileInfo(pkg.m_packageDir).fileName();
    pkg.m_manifest.name = pkg.m_manifest.id;

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

    pkg.m_packageDir = canonicalPackageDir;
    const QString manifestPath = dir.filePath(QStringLiteral("provider.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.exists()) {
        pkg.m_validationError = QStringLiteral("provider.json not found in %1").arg(pkg.m_packageDir);
        return pkg;
    }

    if (QFileInfo(manifestPath).isSymLink() || manifestFile.size() > 1024 * 1024) {
        pkg.m_validationError = QStringLiteral("provider.json exceeds 1 MiB size limit");
        return pkg;
    }

    if (!manifestFile.open(QIODevice::ReadOnly)) {
        pkg.m_validationError = QStringLiteral("Failed to open provider.json: %1").arg(manifestFile.errorString());
        return pkg;
    }

    const QByteArray manifestBytes = manifestFile.read(1024 * 1024 + 1);
    if (manifestBytes.size() > 1024 * 1024 || !manifestFile.atEnd() || manifestFile.error() != QFile::NoError) {
        pkg.m_validationError = QStringLiteral("provider.json exceeds bounded read limit or cannot be read");
        return pkg;
    }
    manifestFile.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        pkg.m_validationError = QStringLiteral("JSON parse error in provider.json: %1").arg(parseError.errorString());
        return pkg;
    }

    const QJsonObject root = doc.object();
    for (const QString &key : {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("description"),
        QStringLiteral("version"), QStringLiteral("author"), QStringLiteral("license"), QStringLiteral("runtime"), QStringLiteral("entry")}) {
        if (root.contains(key) && !root.value(key).isString()) {
            pkg.m_validationError = QStringLiteral("Manifest field %1 must be a string").arg(key);
            return pkg;
        }
    }
    for (const QString &key : {QStringLiteral("capabilities"), QStringLiteral("defaultSettings")}) {
        if (root.contains(key) && !root.value(key).isObject()) {
            pkg.m_validationError = QStringLiteral("Manifest field %1 must be an object").arg(key);
            return pkg;
        }
    }
    pkg.m_manifest.id = root.value(QStringLiteral("id")).toString().trimmed();
    pkg.m_manifest.name = root.value(QStringLiteral("name")).toString().trimmed();
    pkg.m_manifest.description = root.value(QStringLiteral("description")).toString().trimmed();
    pkg.m_manifest.version = root.value(QStringLiteral("version")).toString().trimmed();
    if (root.contains(QStringLiteral("apiVersion"))) {
        const auto apiVal = root.value(QStringLiteral("apiVersion"));
        if (!apiVal.isDouble() || (apiVal.toInt(-1) != 1 && apiVal.toInt(-1) != 2)) {
            pkg.m_validationError = QStringLiteral("Unsupported manifest apiVersion");
            return pkg;
        }
        pkg.m_manifest.apiVersion = apiVal.toInt();
    }
    pkg.m_manifest.author = root.value(QStringLiteral("author")).toString().trimmed();
    pkg.m_manifest.license = root.value(QStringLiteral("license")).toString().trimmed();
    pkg.m_manifest.runtime = root.value(QStringLiteral("runtime")).toString(QStringLiteral("javascript")).trimmed();
    pkg.m_manifest.entry = root.value(QStringLiteral("entry")).toString(QStringLiteral("main.mjs")).trimmed();

    pkg.m_manifest.classification = ProviderClassification::builtin(pkg.id());
    if (root.contains(QStringLiteral("classification")) &&
        !ProviderClassification::fromJson(root.value(QStringLiteral("classification")), &pkg.m_manifest.classification, &pkg.m_validationError)) return pkg;

    if (!ProviderPackageDiscovery::isValidPackageId(pkg.m_manifest.id)) {
        pkg.m_validationError = QStringLiteral("Invalid package ID '%1'").arg(pkg.m_manifest.id);
        pkg.m_manifest.id = QFileInfo(pkg.m_packageDir).fileName();
        return pkg;
    }

    if (pkg.m_manifest.name.isEmpty()) {
        pkg.m_validationError = QStringLiteral("Package name is required");
        return pkg;
    }

    if (pkg.m_manifest.entry.isEmpty() || QDir::isAbsolutePath(pkg.m_manifest.entry) ||
        pkg.m_manifest.entry.contains(QLatin1Char('%')) || pkg.m_manifest.entry.contains(QLatin1Char('?')) ||
        pkg.m_manifest.entry.contains(QLatin1Char('#')) || pkg.m_manifest.entry.contains(QLatin1Char('\\')) ||
        pkg.m_manifest.entry.contains(QChar::Null)) {
        pkg.m_validationError = QStringLiteral("Package entry is required");
        return pkg;
    }

    // Verify entry file path resolution and check against path traversal
    const QString entryPath = dir.filePath(pkg.m_manifest.entry);
    const QFileInfo entryInfo(entryPath);
    if (!entryInfo.isFile() || entryInfo.isSymLink()) {
        pkg.m_validationError = QStringLiteral("Entry file does not exist: %1").arg(pkg.m_manifest.entry);
        return pkg;
    }

    const QString canonicalEntryPath = entryInfo.canonicalFilePath();
    if (!canonicalEntryPath.startsWith(canonicalPackageDir + QLatin1Char('/')) && canonicalEntryPath != canonicalPackageDir) {
        pkg.m_validationError = QStringLiteral("Entry file escapes package directory (symlink traversal blocked)");
        return pkg;
    }
    pkg.m_entryFilePath = canonicalEntryPath;

    // Permissions are displayed before approval, so reject values that the UI
    // would otherwise silently coerce or omit.
    if (root.contains(QStringLiteral("permissions"))) {
        const auto permissionsValue = root.value(QStringLiteral("permissions"));
        if (!permissionsValue.isObject()) {
            pkg.m_validationError = QStringLiteral("permissions must be an object");
            return pkg;
        }
        const auto perms = permissionsValue.toObject();
        for (const QString &key : {QStringLiteral("origins"), QStringLiteral("secrets")}) {
            const auto value = perms.value(key);
            if (!value.isUndefined() && !value.isArray()) {
                pkg.m_validationError = QStringLiteral("Permission lists must be arrays");
                return pkg;
            }
            for (const auto &v : value.toArray()) {
                if (!v.isString() || v.toString().trimmed().isEmpty()) {
                    pkg.m_validationError = QStringLiteral("Permission entries must be nonempty strings");
                    return pkg;
                }
                const QString permission = v.toString().trimmed();
                if (key == QLatin1String("origins")) {
                    QString checkedOrigin = permission;
                    // QUrl rejects '*' in hostnames; validate wildcard suffix as a normal hostname.
                    checkedOrigin.replace(QStringLiteral("*."), QStringLiteral("wildcard."));
                    const QUrl url(checkedOrigin.contains(QStringLiteral("://")) ? checkedOrigin : QStringLiteral("https://") + checkedOrigin);
                    if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")) ||
                        url.host().isEmpty() || !url.userInfo().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty() ||
                        (!url.path().isEmpty() && url.path() != QLatin1String("/"))) {
                        pkg.m_validationError = QStringLiteral("Invalid permission origin");
                        return pkg;
                    }
                    pkg.m_manifest.permissions.origins.append(permission);
                } else pkg.m_manifest.permissions.secrets.append(permission);
            }
        }
    }

    // Parse capabilities
    if (root.contains(QStringLiteral("capabilities"))) {
        pkg.m_manifest.capabilities = ProviderApiV1::capabilitiesFromJson(root.value(QStringLiteral("capabilities")).toObject());
    }

    if (root.contains(QStringLiteral("defaultSettings"))) {
        pkg.m_manifest.defaultSettings = root.value(QStringLiteral("defaultSettings")).toObject();
    }

    pkg.m_manifest.authentication.executionSupported = false;
    pkg.m_manifest.authentication.walletSupported = false;
    if (root.contains(QStringLiteral("authentication"))) {
        if (!AuthenticationDescriptorV2::fromManifest(root.value(QStringLiteral("authentication")), &pkg.m_manifest.authentication, &pkg.m_validationError)) return pkg;
    } else if (pkg.m_manifest.capabilities.authentication) {
        pkg.m_manifest.authentication.mode = AuthenticationMode::Unsupported;
    }

    // Snapshot every asset, including extensionless imports and hidden files. Fail
    // closed on unreadable files, symlinks and bounded-read violations.
    QDirIterator it(canonicalPackageDir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    qint64 total = 0;
    int entries = 0;
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (++entries > 512 || info.isSymLink() || (!info.isDir() && !info.isFile())) {
            pkg.m_validationError = QStringLiteral("Package contains symlinks, special files or too many entries");
            return pkg;
        }
        if (info.isDir()) continue;
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            pkg.m_validationError = QStringLiteral("Package asset is unreadable");
            return pkg;
        }
        const QByteArray bytes = file.read(5 * 1024 * 1024 + 1);
        total += bytes.size();
        if (file.error() != QFile::NoError || !file.atEnd() || bytes.size() > 5 * 1024 * 1024 || total > 16 * 1024 * 1024) {
            pkg.m_validationError = QStringLiteral("Package exceeds asset or total size limit");
            return pkg;
        }
        pkg.m_files.insert(QDir(canonicalPackageDir).relativeFilePath(info.filePath()), bytes);
    }
    if (pkg.m_files.value(QStringLiteral("provider.json")) != manifestBytes) {
        pkg.m_validationError = QStringLiteral("Manifest changed during discovery");
        return pkg;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (auto it = pkg.m_files.cbegin(); it != pkg.m_files.cend(); ++it) {
        // Length framing prevents ambiguous path/content concatenations.
        hash.addData(QByteArray::number(it.key().toUtf8().size()) + ':' + it.key().toUtf8());
        hash.addData(QByteArray::number(it.value().size()) + ':' + it.value());
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

    paths.append(QStringLiteral(KLOOKUP_PACKAGE_INSTALL_DIR));
    paths.append(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../share/klookup/providers.d")));
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
            if (ProviderPackageDiscovery::isValidPackageId(pkg.id())) {
                if (!discoveredById.contains(pkg.id())) {
                    discoveredById.insert(pkg.id(), pkg);
                    results.append(pkg);
                }
            }
        }
    };

    const QStringList normalPaths = defaultSearchPaths();
    // An explicit path must not relabel the same installed or user directory.
    for (const auto &p : customPaths) {
        bool normal = false;
        for (const auto &known : normalPaths) {
            if (QDir::cleanPath(QFileInfo(p).absoluteFilePath()) == QDir::cleanPath(QFileInfo(known).absoluteFilePath())) normal = true;
        }
        if (!normal) scanDir(p, ProviderPackage::LocationType::Custom);
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

QString ProviderPackage::provenance() const
{
    switch (m_locationType) {
    case LocationType::System: return QStringLiteral("system");
    case LocationType::User: return QStringLiteral("user");
    case LocationType::Custom: return QStringLiteral("custom");
    }
    return {};
}

QByteArray ProviderPackage::approvalFingerprint() const
{
    if (!m_valid) return {};
    return QCryptographicHash::hash(m_contentHash + '\0' + m_packageDir.toUtf8() + '\0' + provenance().toUtf8(),
                                    QCryptographicHash::Sha256).toHex();
}

static bool protectedPath(QString path)
{
#ifdef Q_OS_UNIX
    while (!path.isEmpty()) {
        const QFileInfo info(path);
        if (!info.exists() || info.isSymLink() || info.ownerId() != 0 ||
            (info.permissions() & (QFile::WriteGroup | QFile::WriteOther))) return false;
        const QString parent = info.dir().absolutePath();
        if (parent == path) return true;
        path = parent;
    }
#else
    Q_UNUSED(path);
#endif
    return false;
}

bool ProviderPackage::isDistributionPackage() const
{
    if (!m_valid || m_locationType != LocationType::System) return false;
    const QStringList roots = {QStringLiteral("/usr/share/klookup/providers.d"),
        QStringLiteral("/usr/local/share/klookup/providers.d"), QStringLiteral(KLOOKUP_PACKAGE_INSTALL_DIR),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../share/klookup/providers.d"))};
    bool installed = false;
    for (const auto &root : roots) {
        const QString canonical = QFileInfo(root).canonicalFilePath();
        if (!canonical.isEmpty() && QFileInfo(m_packageDir).dir().absolutePath() == canonical) installed = true;
    }
    if (!installed || !protectedPath(m_packageDir)) return false;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it)
        if (!protectedPath(QDir(m_packageDir).filePath(it.key()))) return false;
    return true;
}

bool ProviderPackage::approved(const KConfigGroup &group) const
{
    if (!m_valid || group.readEntry(id() + QStringLiteral("_Revoked"), false)) return false;
    // Legacy false means explicit revocation; legacy true never grants ID-only approval.
    if (group.hasKey(id() + QStringLiteral("_Trusted")) && !group.readEntry(id() + QStringLiteral("_Trusted"), false)) return false;
    const QByteArray saved = group.readEntry(id() + QStringLiteral("_Approval"), QByteArray());
    if (!saved.isEmpty()) return saved == approvalFingerprint();
    return isDistributionPackage();
}

void ProviderPackage::saveApproval(KConfigGroup &group, bool approve) const
{
    group.deleteEntry(id() + QStringLiteral("_Trusted"));
    group.writeEntry(id() + QStringLiteral("_Revoked"), !approve);
    group.writeEntry(id() + QStringLiteral("_Approval"), approve && m_valid ? approvalFingerprint() : QByteArray());
    group.sync();
}

QString ProviderPackage::resolveRelativeModulePath(const QString &fromFile, const QString &spec)
{
    if ((!spec.startsWith(QStringLiteral("./")) && !spec.startsWith(QStringLiteral("../"))) ||
        spec.contains(QLatin1Char(':')) || spec.contains(QLatin1Char('\\')) || spec.contains(QLatin1Char('%')) ||
        spec.contains(QLatin1Char('?')) || spec.contains(QLatin1Char('#')) || spec.contains(QChar::Null)) {
        return {};
    }
    const QString baseDir = QFileInfo(fromFile).path();
    const QString resolved = QDir::cleanPath((baseDir.isEmpty() ? QStringLiteral(".") : baseDir) + QLatin1Char('/') + spec);
    if (resolved.isEmpty() || resolved == QLatin1String(".") || resolved.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(resolved)) {
        return {};
    }
    return resolved;
}

bool ProviderPackage::verifiedFiles(QMap<QString, QByteArray> *files, QString *entryRelPath, QString *error) const
{
    const auto current = fromDirectory(m_packageDir, m_locationType);
    if (!m_valid || !current.isValid() || current.approvalFingerprint() != approvalFingerprint()) {
        if (error)
            *error = QStringLiteral("Package changed since approval/discovery; review it again");
        return false;
    }
    if (files)
        *files = m_files;
    if (entryRelPath)
        *entryRelPath = QDir(m_packageDir).relativeFilePath(m_entryFilePath);
    return true;
}

bool ProviderPackage::writeVerifiedSnapshot(const QString &destination, QString *error) const
{
    QMap<QString, QByteArray> files;
    if (!verifiedFiles(&files, nullptr, error))
        return false;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (it.key().isEmpty() || QDir::isAbsolutePath(it.key()) || it.key().startsWith(QStringLiteral("../"))) {
            if (error)
                *error = QStringLiteral("Invalid relative path in verified package");
            return false;
        }
        const QString path = QDir(destination).filePath(it.key());
        if (!QDir().mkpath(QFileInfo(path).path()))
            return false;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size()) {
            if (error)
                *error = QStringLiteral("Could not create verified package snapshot");
            return false;
        }
    }
    return true;
}
