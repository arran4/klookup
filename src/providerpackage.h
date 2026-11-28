// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROVIDERPACKAGE_H
#define PROVIDERPACKAGE_H

#include "providerapiv1.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMap>

#include <QString>
#include <QStringList>
#include <QUrl>

class KConfigGroup;

struct PackagePermissions {
    QStringList origins;
    QStringList secrets;
};

struct PackageManifest {
    int apiVersion = 2;
    QString id;
    QString name;
    QString description;
    QString version;
    QString author;
    QString license;
    QString runtime = QStringLiteral("javascript");
    QString entry = QStringLiteral("main.mjs");
    PackagePermissions permissions;
    ProviderApiV1::Capabilities capabilities;
    QJsonObject defaultSettings;
    ProviderClassification classification;
    AuthenticationDescriptorV2 authentication;
};

struct ConfiguredProviderDirectory {
    QString path;
    QString label;
    bool enabled = true;
};

class ProviderPackage {
public:
    enum class LocationType { Custom, User, System };
    enum class SymlinkPolicy { PackageDirOnly, AllowSafeFileSymlinks };

    ProviderPackage() = default;

    bool isValid() const { return m_valid; }
    QString validationError() const { return m_validationError; }

    const PackageManifest &manifest() const { return m_manifest; }
    QString id() const { return m_manifest.id; }
    QString name() const { return m_manifest.name; }
    QString runtime() const { return m_manifest.runtime; }
    QString packageDir() const { return m_packageDir; }
    QString displayPath() const { return m_displayPath.isEmpty() ? m_packageDir : m_displayPath; }
    QString canonicalPath() const { return m_packageDir; }
    bool isSymlinkBacked() const { return m_symlinkBacked; }
    SymlinkPolicy symlinkPolicy() const { return m_symlinkPolicy; }
    QString provenanceLabel() const { return m_provenanceLabel.isEmpty() ? provenance() : m_provenanceLabel; }
    void setProvenanceLabel(const QString &label) { m_provenanceLabel = label; }
    QString shadowedBy() const { return m_shadowedBy; }
    void setShadowedBy(const QString &ownerPath) { m_shadowedBy = ownerPath; }
    QString entryFilePath() const { return m_entryFilePath; }
    LocationType locationType() const { return m_locationType; }
    QByteArray contentHash() const { return m_contentHash; }
    QByteArray approvalFingerprint() const;
    QString provenance() const;
    bool isDistributionPackage() const;
    bool approved(const KConfigGroup &group) const;
    void saveApproval(KConfigGroup &group, bool approve) const;
    bool verifiedFiles(QMap<QString, QByteArray> *files, QString *entryRelPath, QString *error) const;
    bool writeVerifiedSnapshot(const QString &destination, QString *error) const;

    static QString resolveRelativeModulePath(const QString &fromFile, const QString &spec);

    bool isTrusted() const { return m_trusted; }
    void setTrusted(bool trusted) { m_trusted = trusted; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    bool isOriginAllowed(const QUrl &url) const;
    bool isSecretAllowed(const QString &secretKey) const;

    static ProviderPackage fromDirectory(const QString &dirPath,
                                       LocationType locType = LocationType::User,
                                       SymlinkPolicy symlinkPolicy = SymlinkPolicy::PackageDirOnly);

private:
    PackageManifest m_manifest;
    QString m_packageDir;
    QString m_displayPath;
    QString m_provenanceLabel;
    QString m_shadowedBy;
    QString m_entryFilePath;
    LocationType m_locationType = LocationType::User;
    SymlinkPolicy m_symlinkPolicy = SymlinkPolicy::PackageDirOnly;
    QByteArray m_contentHash;
    QMap<QString, QByteArray> m_files;
    QMap<QString, QString> m_resolvedTargets;
    bool m_symlinkBacked = false;
    bool m_valid = false;
    QString m_validationError;
    bool m_trusted = false;
    bool m_enabled = false;
};

class ProviderPackageDiscovery {
public:
    static QStringList defaultSearchPaths();
    static QList<ConfiguredProviderDirectory> readConfiguredDirectories(const KConfigGroup &group);
    static void writeConfiguredDirectories(KConfigGroup &group, const QList<ConfiguredProviderDirectory> &dirs);
    static QList<ProviderPackage> discoverPackages(const QStringList &customPaths = {});
    static QList<ProviderPackage> discoverPackages(const QList<ConfiguredProviderDirectory> &configuredDirs,
                                                   const QStringList &customPaths = {},
                                                   QList<ProviderPackage> *shadowed = nullptr);
    static bool isValidPackageId(const QString &id);
};

struct ProviderPackageStatus {
    ProviderPackage package;
    QString status;
};

#endif // PROVIDERPACKAGE_H
