// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providercatalogue.h"
#include "providerpackage.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

namespace
{
bool writePackageFiles(const QString &dir,
                       const QString &id,
                       const QString &version,
                       const QStringList &origins,
                       const QByteArray &scriptBody)
{
    if (!QDir().mkpath(dir))
        return false;
    QJsonObject manifest;
    manifest.insert(QStringLiteral("id"), id);
    manifest.insert(QStringLiteral("name"), QStringLiteral("Example Provider"));
    manifest.insert(QStringLiteral("version"), version);
    manifest.insert(QStringLiteral("runtime"), QStringLiteral("javascript"));
    manifest.insert(QStringLiteral("entry"), QStringLiteral("main.mjs"));
    manifest.insert(QStringLiteral("apiVersion"), 1);
    QJsonObject perms;
    perms.insert(QStringLiteral("origins"), QJsonArray::fromStringList(origins));
    manifest.insert(QStringLiteral("permissions"), perms);

    QFile mf(dir + QStringLiteral("/provider.json"));
    if (!mf.open(QIODevice::WriteOnly))
        return false;
    mf.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    mf.close();

    QFile sf(dir + QStringLiteral("/main.mjs"));
    if (!sf.open(QIODevice::WriteOnly))
        return false;
    sf.write(scriptBody);
    sf.close();
    return true;
}

QByteArray buildCatalogueJson(const QString &id,
                              const QString &version,
                              const QString &digest,
                              const QString &packageUrl,
                              const QStringList &origins,
                              bool revoked = false,
                              int minApi = 1,
                              int maxApi = 1)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), id);
    entry.insert(QStringLiteral("name"), QStringLiteral("Example Provider"));
    entry.insert(QStringLiteral("version"), version);
    entry.insert(QStringLiteral("minApiVersion"), minApi);
    entry.insert(QStringLiteral("maxApiVersion"), maxApi);
    entry.insert(QStringLiteral("author"), QStringLiteral("Community Author"));
    entry.insert(QStringLiteral("license"), QStringLiteral("GPL-3.0-or-later"));
    entry.insert(QStringLiteral("repositoryUrl"), QStringLiteral("https://github.com/example/klookup-example"));
    entry.insert(QStringLiteral("packageUrl"), packageUrl);
    entry.insert(QStringLiteral("sha256"), digest);
    entry.insert(QStringLiteral("origins"), QJsonArray::fromStringList(origins));
    entry.insert(QStringLiteral("revoked"), revoked);

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("generatedAt"), QStringLiteral("2026-11-17T00:00:00Z"));
    root.insert(QStringLiteral("providers"), QJsonArray{entry});
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
} // namespace

class TestProviderCatalogue : public QObject
{
    Q_OBJECT

private slots:
    void parseRejectsInsecureUrlsBadDigestsAndInvalidIds()
    {
        CatalogueIndex idx;
        QString err;
        const QString validHex(64, QLatin1Char('a'));

        // Non-HTTPS URL rejected by default
        QVERIFY(!ProviderCatalogue::parseIndex(
            buildCatalogueJson(QStringLiteral("example-dict"), QStringLiteral("1.0.0"), validHex,
                               QStringLiteral("http://example.com/pkg.tar.gz"), {}),
            &idx, &err));

        // Bad SHA-256 rejected
        QVERIFY(!ProviderCatalogue::parseIndex(
            buildCatalogueJson(QStringLiteral("example-dict"), QStringLiteral("1.0.0"), QStringLiteral("deadbeef"),
                               QStringLiteral("https://example.com/pkg.tar.gz"), {}),
            &idx, &err));

        // Path traversal ID rejected
        QVERIFY(!ProviderCatalogue::parseIndex(
            buildCatalogueJson(QStringLiteral("../escape"), QStringLiteral("1.0.0"), validHex,
                               QStringLiteral("https://example.com/pkg.tar.gz"), {}),
            &idx, &err));

        // Valid HTTPS entry parses cleanly
        QVERIFY(ProviderCatalogue::parseIndex(
            buildCatalogueJson(QStringLiteral("example-dict"), QStringLiteral("1.0.0"), validHex,
                               QStringLiteral("https://example.com/pkg.tar.gz"), {QStringLiteral("https://api.example.com")}),
            &idx, &err));
        QCOMPARE(idx.entries.size(), 1);
    }

    void installVerifiesDigestPermissionsDefaultUnapprovedAndSupportsRollback()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString v1Src = tmp.filePath(QStringLiteral("src-v1"));
        const QString v2Src = tmp.filePath(QStringLiteral("src-v2"));
        const QString installRoot = tmp.filePath(QStringLiteral("installed"));
        const QString backupRoot = tmp.filePath(QStringLiteral("backups"));

        QVERIFY(writePackageFiles(v1Src, QStringLiteral("example-dict"), QStringLiteral("1.0.0"),
                                  {QStringLiteral("https://api.example.com")},
                                  QByteArrayLiteral("export function search() { return []; }\n")));
        QVERIFY(writePackageFiles(v2Src, QStringLiteral("example-dict"), QStringLiteral("1.1.0"),
                                  {QStringLiteral("https://api.example.com")},
                                  QByteArrayLiteral("export function search() { return [{id:'2',title:'v2'}]; }\n")));

        const QString d1 = ProviderCatalogue::computeDirectoryDigestHex(v1Src);
        const QString d2 = ProviderCatalogue::computeDirectoryDigestHex(v2Src);
        QCOMPARE(d1.size(), 64);
        QCOMPARE(d2.size(), 64);

        CatalogueIndex idx1;
        QString err;
        QVERIFY(ProviderCatalogue::parseIndex(
            buildCatalogueJson(QStringLiteral("example-dict"), QStringLiteral("1.0.0"), d1,
                               QUrl::fromLocalFile(v1Src).toString(), {QStringLiteral("https://api.example.com")}),
            &idx1, &err, true));

        // 1. Tampered digest fails
        CatalogueEntry tampered = idx1.entries.first();
        tampered.sha256Hex = QString(64, QLatin1Char('0'));
        QVERIFY(!ProviderCatalogue::installLocalPackage(tampered, v1Src, installRoot, backupRoot, &err));

        // 2. Origin mismatch between catalogue and manifest fails
        CatalogueEntry mismatchedPerms = idx1.entries.first();
        mismatchedPerms.origins = {QStringLiteral("https://evil.example.com")};
        QVERIFY(!ProviderCatalogue::installLocalPackage(mismatchedPerms, v1Src, installRoot, backupRoot, &err));

        // 3. Valid install succeeds and remains unapproved by default
        QVERIFY2(ProviderCatalogue::installLocalPackage(idx1.entries.first(), v1Src, installRoot, backupRoot, &err),
                 qPrintable(err));
        const QString installedDir = installRoot + QStringLiteral("/example-dict");
        const ProviderPackage pkgV1 = ProviderPackage::fromDirectory(installedDir, ProviderPackage::LocationType::User);
        QVERIFY(pkgV1.isValid());
        QCOMPARE(pkgV1.manifest().version, QStringLiteral("1.0.0"));

        KConfig cfg(tmp.filePath(QStringLiteral("klookuprc")), KConfig::SimpleConfig);
        KConfigGroup approvalGroup(&cfg, QStringLiteral("ProviderApprovals"));
        QVERIFY(!pkgV1.approved(approvalGroup));
        pkgV1.saveApproval(approvalGroup, true);
        QVERIFY(pkgV1.approved(approvalGroup));

        // 4. Update to v1.1.0 invalidates v1.0.0 approval and backs up v1.0.0
        CatalogueIndex idx2;
        QVERIFY(ProviderCatalogue::parseIndex(
            buildCatalogueJson(QStringLiteral("example-dict"), QStringLiteral("1.1.0"), d2,
                               QUrl::fromLocalFile(v2Src).toString(), {QStringLiteral("https://api.example.com")}),
            &idx2, &err, true));
        QVERIFY(ProviderCatalogue::installLocalPackage(idx2.entries.first(), v2Src, installRoot, backupRoot, &err));
        const ProviderPackage pkgV2 = ProviderPackage::fromDirectory(installedDir, ProviderPackage::LocationType::User);
        QVERIFY(pkgV2.isValid());
        QCOMPARE(pkgV2.manifest().version, QStringLiteral("1.1.0"));
        QVERIFY(!pkgV2.approved(approvalGroup));

        // 5. Revocation check + rollback restores v1.0.0
        CatalogueIndex revokedIdx;
        QVERIFY(ProviderCatalogue::parseIndex(
            buildCatalogueJson(QStringLiteral("example-dict"), QStringLiteral("1.1.0"), d2,
                               QUrl::fromLocalFile(v2Src).toString(), {QStringLiteral("https://api.example.com")}, true),
            &revokedIdx, &err, true));
        QString reason;
        QVERIFY(ProviderCatalogue::isRevoked(revokedIdx, revokedIdx.entries.first(), &reason));
        QVERIFY(ProviderCatalogue::rollbackPackage(QStringLiteral("example-dict"), installRoot, backupRoot, &err));
        const ProviderPackage rolledBack = ProviderPackage::fromDirectory(installedDir, ProviderPackage::LocationType::User);
        QCOMPARE(rolledBack.manifest().version, QStringLiteral("1.0.0"));
    }
};

QTEST_GUILESS_MAIN(TestProviderCatalogue)
#include "test_provider_catalogue.moc"
