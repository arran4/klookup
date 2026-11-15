// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providerpackage.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

namespace {

bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(content);
    file.close();
    return true;
}

ProviderPackage buildPackage(const QString &dirPath, const QMap<QString, QByteArray> &modules)
{
    QJsonObject manifest;
    manifest[QStringLiteral("id")] = QStringLiteral("acorn_audit_pkg");
    manifest[QStringLiteral("name")] = QStringLiteral("Acorn Audit Package");
    manifest[QStringLiteral("version")] = QStringLiteral("1.0.0");
    manifest[QStringLiteral("runtime")] = QStringLiteral("javascript");
    manifest[QStringLiteral("entry")] = QStringLiteral("main.mjs");

    writeFile(dirPath + QStringLiteral("/provider.json"), QJsonDocument(manifest).toJson());
    for (auto it = modules.cbegin(); it != modules.cend(); ++it) {
        QFileInfo info(dirPath + QLatin1Char('/') + it.key());
        QDir().mkpath(info.absolutePath());
        writeFile(info.filePath(), it.value());
    }

    auto pkg = ProviderPackage::fromDirectory(dirPath);
    pkg.setTrusted(true);
    return pkg;
}

} // namespace

class TestAcornParserValidation : public QObject
{
    Q_OBJECT

private slots:
    void testCommentsStringsRegexesAndTemplatesDoNotTriggerFalseImports()
    {
        QTemporaryDir pkgDir, snapDir;
        QVERIFY(pkgDir.isValid() && snapDir.isValid());

        // Real static import is ./helper.mjs; all other "import" strings are inside comments,
        // string literals, regex literals, and template substitutions.
        const QByteArray mainCode =
            "// import { evil } from '../outside.mjs';\n"
            "/*\n"
            "   import * as bad from 'https://evil.invalid/mod.mjs';\n"
            "*/\n"
            "const s1 = \"import { fake } from './nonexistent1.mjs'\";\n"
            "const s2 = 'import(\"./nonexistent2.mjs\")';\n"
            "const re = /import\\s+foo\\s+from\\s+['\"]\\.\\/nonexistent3\\.mjs['\"]/;\n"
            "const tpl = `prefix ${\"import('./nonexistent4.mjs')\"} suffix`;\n"
            "import { val } from './helper.mjs';\n"
            "export function search(q) { return [{ id: '1', title: s1 + s2 + re.source + tpl + val }]; }\n";

        auto pkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), mainCode},
            {QStringLiteral("helper.mjs"), QByteArrayLiteral("export const val = 'ok';\n")}
        });
        QVERIFY(pkg.isValid());

        QString error;
        QVERIFY2(pkg.writeVerifiedSnapshot(snapDir.path(), &error), qPrintable(error));
        QVERIFY(QFile::exists(snapDir.filePath(QStringLiteral("main.mjs"))));
        QVERIFY(QFile::exists(snapDir.filePath(QStringLiteral("helper.mjs"))));
    }

    void testStaticReexportsAndDuplicateDiamondGraph()
    {
        QTemporaryDir pkgDir, snapDir;
        QVERIFY(pkgDir.isValid() && snapDir.isValid());

        auto pkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral(
                "import { a } from './a.mjs';\n"
                "export { b } from './b.mjs';\n"
                "export * from './shared.mjs';\n"
                "export function search() { return []; }\n"
            )},
            {QStringLiteral("a.mjs"), QByteArrayLiteral("import { s } from './shared.mjs'; export const a = s;\n")},
            {QStringLiteral("b.mjs"), QByteArrayLiteral("import { s } from './shared.mjs'; export const b = s;\n")},
            {QStringLiteral("shared.mjs"), QByteArrayLiteral("export const s = 42;\n")}
        });
        QVERIFY(pkg.isValid());

        QString error;
        QVERIFY2(pkg.writeVerifiedSnapshot(snapDir.path(), &error), qPrintable(error));
        QVERIFY(QFile::exists(snapDir.filePath(QStringLiteral("shared.mjs"))));
    }

    void testDynamicImportRejectedBeforeExecution()
    {
        QTemporaryDir pkgDir, snapDir;
        QVERIFY(pkgDir.isValid() && snapDir.isValid());

        // Top-level throw ensures we prove validation never executes module code!
        auto pkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral(
                "throw new Error('MUST_NOT_EXECUTE_DURING_VALIDATION');\n"
                "export function load() { return import('./helper.mjs'); }\n"
            )},
            {QStringLiteral("helper.mjs"), QByteArrayLiteral("export const x = 1;\n")}
        });
        QVERIFY(pkg.isValid());

        QString error;
        QVERIFY(!pkg.writeVerifiedSnapshot(snapDir.path(), &error));
        QVERIFY(error.contains(QStringLiteral("Dynamic imports are unsupported")));
        QVERIFY(!error.contains(QStringLiteral("MUST_NOT_EXECUTE")));
    }

    void testTraversalAndEscapingImportsRejected()
    {
        QTemporaryDir pkgDir, snapDir;
        QVERIFY(pkgDir.isValid() && snapDir.isValid());

        auto pkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral("import { x } from '../secret.mjs';\n")}
        });
        QVERIFY(pkg.isValid());

        QString error;
        QVERIFY(!pkg.writeVerifiedSnapshot(snapDir.path(), &error));
        QVERIFY(error.contains(QStringLiteral("Unsupported or escaping module import")));
    }

    void testMalformedSyntaxRejectedWithoutExecution()
    {
        QTemporaryDir pkgDir, snapDir;
        QVERIFY(pkgDir.isValid() && snapDir.isValid());

        auto pkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral("function broken( { return ;;;\n")}
        });
        QVERIFY(pkg.isValid());

        QString error;
        QVERIFY(!pkg.writeVerifiedSnapshot(snapDir.path(), &error));
        QVERIFY(error.contains(QStringLiteral("Module syntax/import validation failed")));
    }
};

QTEST_MAIN(TestAcornParserValidation)
#include "test_acorn_parser_validation.moc"
