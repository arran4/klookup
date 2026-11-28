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

ProviderPackage ProviderPackage::fromDirectory(const QString &dirPath, LocationType locType, SymlinkPolicy symlinkPolicy)
{
    ProviderPackage pkg;
    pkg.m_locationType = locType;
    pkg.m_symlinkPolicy = symlinkPolicy;
    pkg.m_displayPath = QDir::cleanPath(QFileInfo(dirPath).absoluteFilePath());
    pkg.m_packageDir = pkg.m_displayPath;
    pkg.m_manifest.id = QFileInfo(pkg.m_displayPath).fileName();
    pkg.m_manifest.name = pkg.m_manifest.id;

    const QFileInfo dirInfo(pkg.m_displayPath);
    if (dirInfo.isSymLink())
        pkg.m_symlinkBacked = true;

    QDir dir(pkg.m_packageDir);
    if (!dirInfo.exists() || !dir.exists()) {
        pkg.m_validationError = dirInfo.isSymLink()
            ? QStringLiteral("Broken or unreadable symlinked provider directory: %1").arg(pkg.m_displayPath)
            : QStringLiteral("Directory does not exist: %1").arg(pkg.m_displayPath);
        return pkg;
    }

    const QString canonicalPackageDir = dirInfo.canonicalFilePath();
    if (canonicalPackageDir.isEmpty() || !QFileInfo(canonicalPackageDir).isDir()) {
        pkg.m_validationError = QStringLiteral("Cannot resolve canonical path for %1").arg(pkg.m_displayPath);
        return pkg;
    }

    pkg.m_packageDir = canonicalPackageDir;
    dir.setPath(canonicalPackageDir);

    auto inspectRegularFile = [&pkg, &canonicalPackageDir, symlinkPolicy](const QString &filePath, QString *resolvedOut, QString *errorOut) -> bool {
        const QFileInfo info(filePath);
        if (!info.exists()) {
            *errorOut = info.isSymLink()
                ? QStringLiteral("Broken symlink: %1").arg(QDir(canonicalPackageDir).relativeFilePath(filePath))
                : QStringLiteral("File does not exist: %1").arg(QDir(canonicalPackageDir).relativeFilePath(filePath));
            return false;
        }
        if (info.isSymLink()) {
            if (symlinkPolicy != SymlinkPolicy::AllowSafeFileSymlinks) {
                *errorOut = QStringLiteral("Package contains symlinks, special files or too many entries");
                return false;
            }
            const QString target = info.canonicalFilePath();
            const QFileInfo targetInfo(target);
            if (target.isEmpty() || !targetInfo.exists() || !targetInfo.isFile() || targetInfo.isDir()) {
                *errorOut = QStringLiteral("Unsafe or broken file symlink: %1").arg(QDir(canonicalPackageDir).relativeFilePath(filePath));
                return false;
            }
            for (const QLatin1String &forbidden : {QLatin1String("/etc/"), QLatin1String("/proc/"), QLatin1String("/sys/"),
                                                    QLatin1String("/dev/"), QLatin1String("/run/"), QLatin1String("/boot/")}) {
                if (target.startsWith(forbidden)) {
                    *errorOut = QStringLiteral("Unsafe symlink target path blocked: %1").arg(target);
                    return false;
                }
            }
#ifdef Q_OS_UNIX
            const QFileInfo pkgDirInfo(canonicalPackageDir);
            if (!target.startsWith(QStringLiteral("/nix/store/")) && targetInfo.ownerId() != pkgDirInfo.ownerId()) {
                *errorOut = QStringLiteral("Cross-owner symlink target blocked: %1").arg(target);
                return false;
            }
#endif
            pkg.m_symlinkBacked = true;
            if (resolvedOut)
                *resolvedOut = target;
            return true;
        }
        if (!info.isFile() || info.isDir()) {
            *errorOut = QStringLiteral("Special files and non-regular entries are not allowed: %1").arg(QDir(canonicalPackageDir).relativeFilePath(filePath));
            return false;
        }
        if (resolvedOut)
            *resolvedOut = info.canonicalFilePath();
        return true;
    };

    const QString manifestPath = dir.filePath(QStringLiteral("provider.json"));
    QString manifestTarget;
    QString fileErr;
    if (!QFileInfo::exists(manifestPath) && !QFileInfo(manifestPath).isSymLink()) {
        pkg.m_validationError = QStringLiteral("provider.json not found in %1").arg(pkg.m_packageDir);
        return pkg;
    }
    if (!inspectRegularFile(manifestPath, &manifestTarget, &fileErr)) {
        pkg.m_validationError = fileErr;
        return pkg;
    }
    QFile manifestFile(manifestTarget);
    if (manifestFile.size() > 1024 * 1024) {
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
    QString entryTarget;
    if (!inspectRegularFile(entryPath, &entryTarget, &fileErr)) {
        pkg.m_validationError = fileErr.isEmpty() ? QStringLiteral("Entry file does not exist: %1").arg(pkg.m_manifest.entry) : fileErr;
        return pkg;
    }

    const QString cleanedEntry = QDir::cleanPath(dir.filePath(pkg.m_manifest.entry));
    if (!cleanedEntry.startsWith(canonicalPackageDir + QLatin1Char('/')) && cleanedEntry != canonicalPackageDir) {
        pkg.m_validationError = QStringLiteral("Entry file escapes package directory (symlink traversal blocked)");
        return pkg;
    }
    pkg.m_entryFilePath = entryTarget;

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

    // Snapshot every asset, including regular-file symlinks (GNU Stow pattern) and
    // hidden files. Reject broken symlinks, directory symlinks, special files, and
    // bounded-read violations. Hash resolved target paths alongside contents.
    QDirIterator it(canonicalPackageDir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    qint64 total = 0;
    int entries = 0;
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (++entries > 512) {
            pkg.m_validationError = QStringLiteral("Package contains symlinks, special files or too many entries");
            return pkg;
        }
        if (info.isDir()) {
            if (info.isSymLink()) {
                pkg.m_validationError = QStringLiteral("Package contains internal directory symlink: %1").arg(info.fileName());
                return pkg;
            }
            continue;
        }
        QString resolvedTarget;
        if (!inspectRegularFile(info.filePath(), &resolvedTarget, &fileErr)) {
            pkg.m_validationError = fileErr;
            return pkg;
        }
        QFile file(resolvedTarget);
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
        const QString relPath = QDir(canonicalPackageDir).relativeFilePath(info.filePath());
        pkg.m_files.insert(relPath, bytes);
        pkg.m_resolvedTargets.insert(relPath, resolvedTarget);
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

QList<ConfiguredProviderDirectory> ProviderPackageDiscovery::readConfiguredDirectories(const KConfigGroup &group)
{
    QList<ConfiguredProviderDirectory> dirs;
    const int count = qBound(0, group.readEntry("Count", 0), 256);
    for (int i = 0; i < count; ++i) {
        const QString path = group.readEntry(QStringLiteral("Path%1").arg(i), QString()).trimmed();
        if (path.isEmpty())
            continue;
        ConfiguredProviderDirectory entry;
        entry.path = path;
        entry.label = group.readEntry(QStringLiteral("Label%1").arg(i), QString()).trimmed();
        entry.enabled = group.readEntry(QStringLiteral("Enabled%1").arg(i), true);
        dirs.append(entry);
    }
    return dirs;
}

void ProviderPackageDiscovery::writeConfiguredDirectories(KConfigGroup &group,
                                                          const QList<ConfiguredProviderDirectory> &dirs)
{
    for (const QString &key : group.keyList())
        group.deleteEntry(key);
    int idx = 0;
    for (const auto &dir : dirs) {
        const QString path = dir.path.trimmed();
        if (path.isEmpty())
            continue;
        group.writeEntry(QStringLiteral("Path%1").arg(idx), path);
        group.writeEntry(QStringLiteral("Label%1").arg(idx), dir.label.trimmed());
        group.writeEntry(QStringLiteral("Enabled%1").arg(idx), dir.enabled);
        ++idx;
    }
    group.writeEntry("Count", idx);
    group.sync();
}

QList<ProviderPackage> ProviderPackageDiscovery::discoverPackages(const QStringList &customPaths)
{
    return discoverPackages({}, customPaths, nullptr);
}

QList<ProviderPackage> ProviderPackageDiscovery::discoverPackages(
    const QList<ConfiguredProviderDirectory> &configuredDirs,
    const QStringList &customPaths,
    QList<ProviderPackage> *shadowed)
{
    QList<ProviderPackage> results;
    QHash<QString, ProviderPackage> discoveredById;
    if (shadowed)
        shadowed->clear();

    auto scanDir = [&](const QString &basePath, ProviderPackage::LocationType locType,
                       const QString &provenanceLabel, ProviderPackage::SymlinkPolicy symlinkPolicy) {
        QDir dir(basePath);
        if (!dir.exists())
            return;

        // Include symlinked package directories when scanning user-configured roots.
        const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
        for (const auto &info : entries) {
            const QString pkgDir = info.absoluteFilePath();
            auto pkg = ProviderPackage::fromDirectory(pkgDir, locType, symlinkPolicy);
            if (!provenanceLabel.isEmpty())
                pkg.setProvenanceLabel(provenanceLabel);
            if (ProviderPackageDiscovery::isValidPackageId(pkg.id())) {
                if (!discoveredById.contains(pkg.id())) {
                    discoveredById.insert(pkg.id(), pkg);
                    results.append(pkg);
                } else if (shadowed) {
                    pkg.setShadowedBy(discoveredById.value(pkg.id()).displayPath());
                    shadowed->append(pkg);
                }
            }
        }
    };

    const QStringList normalPaths = defaultSearchPaths();
    for (const auto &p : customPaths) {
        bool normal = false;
        for (const auto &known : normalPaths) {
            if (QDir::cleanPath(QFileInfo(p).absoluteFilePath()) == QDir::cleanPath(QFileInfo(known).absoluteFilePath())) normal = true;
        }
        if (!normal) scanDir(p, ProviderPackage::LocationType::Custom, QString(), ProviderPackage::SymlinkPolicy::AllowSafeFileSymlinks);
    }

    // Configured user provider directories in explicit precedence order.
    for (const auto &cfg : configuredDirs) {
        if (!cfg.enabled || cfg.path.trimmed().isEmpty())
            continue;
        const QString label = cfg.label.trimmed().isEmpty() ? QStringLiteral("User directory") : cfg.label.trimmed();
        scanDir(cfg.path, ProviderPackage::LocationType::Custom, label, ProviderPackage::SymlinkPolicy::AllowSafeFileSymlinks);
    }

    // Default XDG search paths (User then System)
    const QStringList defaults = defaultSearchPaths();
    bool isFirst = true;
    for (const auto &p : defaults) {
        const auto locType = isFirst ? ProviderPackage::LocationType::User : ProviderPackage::LocationType::System;
        const auto policy = isFirst ? ProviderPackage::SymlinkPolicy::AllowSafeFileSymlinks : ProviderPackage::SymlinkPolicy::PackageDirOnly;
        scanDir(p, locType, QString(), policy);
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
    QCryptographicHash fp(QCryptographicHash::Sha256);
    fp.addData(m_contentHash);
    fp.addData(QByteArrayView("\0", 1));
    fp.addData(m_packageDir.toUtf8());
    fp.addData(QByteArrayView("\0", 1));
    fp.addData(provenance().toUtf8());
    for (auto it = m_resolvedTargets.cbegin(); it != m_resolvedTargets.cend(); ++it) {
        fp.addData(QByteArrayView("\0", 1));
        fp.addData(it.key().toUtf8());
        fp.addData(QByteArrayView("=", 1));
        fp.addData(it.value().toUtf8());
    }
    return fp.result().toHex();
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
    const auto current = fromDirectory(m_packageDir, m_locationType, m_symlinkPolicy);
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
