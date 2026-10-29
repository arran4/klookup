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
};

class ProviderPackage {
public:
    enum class LocationType { Custom, User, System };

    ProviderPackage() = default;

    bool isValid() const { return m_valid; }
    QString validationError() const { return m_validationError; }

    const PackageManifest &manifest() const { return m_manifest; }
    QString id() const { return m_manifest.id; }
    QString name() const { return m_manifest.name; }
    QString runtime() const { return m_manifest.runtime; }
    QString packageDir() const { return m_packageDir; }
    QString entryFilePath() const { return m_entryFilePath; }
    LocationType locationType() const { return m_locationType; }
    QByteArray contentHash() const { return m_contentHash; }
    QByteArray approvalFingerprint() const;
    QString provenance() const;
    bool isDistributionPackage() const;
    bool approved(const KConfigGroup &group) const;
    void saveApproval(KConfigGroup &group, bool approve) const;
    bool writeVerifiedSnapshot(const QString &destination, QString *error) const;

    bool isTrusted() const { return m_trusted; }
    void setTrusted(bool trusted) { m_trusted = trusted; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    bool isOriginAllowed(const QUrl &url) const;
    bool isSecretAllowed(const QString &secretKey) const;

    static ProviderPackage fromDirectory(const QString &dirPath, LocationType locType = LocationType::User);

private:
    PackageManifest m_manifest;
    QString m_packageDir;
    QString m_entryFilePath;
    LocationType m_locationType = LocationType::User;
    QByteArray m_contentHash;
    QMap<QString, QByteArray> m_files;
    bool m_valid = false;
    QString m_validationError;
    bool m_trusted = false;
    bool m_enabled = false;
};

class ProviderPackageDiscovery {
public:
    static QStringList defaultSearchPaths();
    static QList<ProviderPackage> discoverPackages(const QStringList &customPaths = {});
    static bool isValidPackageId(const QString &id);
};

struct ProviderPackageStatus {
    ProviderPackage package;
    QString status;
};

#endif // PROVIDERPACKAGE_H
