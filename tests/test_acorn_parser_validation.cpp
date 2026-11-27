// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jsprovider.h"
#include "providerpackage.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
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
    manifest[QStringLiteral("id")] = QStringLiteral("script_api_v2_pkg");
    manifest[QStringLiteral("name")] = QStringLiteral("Script API v2 Package");
    manifest[QStringLiteral("version")] = QStringLiteral("1.0.0");
    manifest[QStringLiteral("apiVersion")] = 2;
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
    void testCommentsStringsRegexesTemplatesAndInMemoryRequire()
    {
        QTemporaryDir pkgDir;
        QVERIFY(pkgDir.isValid());

        const QByteArray mainCode =
            "// import { evil } from '../outside.mjs';\n"
            "/*\n"
            "   import * as bad from 'https://evil.invalid/mod.mjs';\n"
            "*/\n"
            "const s1 = \"import { fake } from './nonexistent1.mjs'\";\n"
            "const s2 = 'import(\"./nonexistent2.mjs\")';\n"
            "const re = /import\\s+foo\\s+from\\s+['\"]\\.\\/nonexistent3\\.mjs['\"]/;\n"
            "const tpl = `prefix ${\"import('./nonexistent4.mjs')\"} suffix`;\n"
            "const { val } = host.require('./helper.mjs');\n"
            "module.exports = {\n"
            "    search: function(q) { return [{ id: '1', title: val + ':' + host.apiVersion + ':' + q + ':' + s1.length + s2.length + re.source.length + tpl.length }]; }\n"
            "};\n";

        auto pkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), mainCode},
            {QStringLiteral("helper.mjs"), QByteArrayLiteral("module.exports = { val: 'ok' };\n")}
        });
        QVERIFY(pkg.isValid());

        JsProvider provider(pkg);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        provider.search(QStringLiteral("hello"));
        QTRY_COMPARE(spy.count(), 1);
        const SearchPage page = spy.first().at(1).value<SearchPage>();
        QCOMPARE(page.results.size(), 1);
        QVERIFY(page.results.first().title.startsWith(QStringLiteral("ok:2:hello:")));
    }

    void testStaticAndDynamicImportsStructurallyRejectedBeforeExecution()
    {
        QTemporaryDir pkgDir;
        QVERIFY(pkgDir.isValid());

        for (const QByteArray &script : {
                 QByteArray("throw new Error('MUST_NOT_EXECUTE');\nimport { val } from './helper.mjs';\nmodule.exports = { search: function() {} };\n"),
                 QByteArray("throw new Error('MUST_NOT_EXECUTE');\nexport function search() { return []; }\n"),
                 QByteArray("throw new Error('MUST_NOT_EXECUTE');\nmodule.exports = { search: function() { return import('file:///etc/passwd'); } };\n")}) {
            auto pkg = buildPackage(pkgDir.path(), {
                {QStringLiteral("main.mjs"), script},
                {QStringLiteral("helper.mjs"), QByteArrayLiteral("module.exports = { val: 1 };\n")}
            });
            QVERIFY(pkg.isValid());

            JsProvider provider(pkg);
            QVERIFY(!provider.isLoaded());
            QVERIFY(!provider.loadError().contains(QStringLiteral("MUST_NOT_EXECUTE")));
            QVERIFY(provider.loadError().contains(QStringLiteral("import")));
        }

        // Runtime eval('import(...)') also fails closed with SyntaxError inside Script mode
        auto evalPkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral("module.exports = { search: function() { return eval(\"import('file:///etc/passwd')\"); } };\n")}
        });
        QVERIFY(evalPkg.isValid());
        JsProvider evalProvider(evalPkg);
        QVERIFY(evalProvider.isLoaded());
        QSignalSpy failSpy(&evalProvider, &ProviderApiV1::ProviderV1::pageFailed);
        evalProvider.search(QStringLiteral("q"));
        QTRY_COMPARE(failSpy.count(), 1);
        QVERIFY(failSpy.first().at(2).toString().contains(QStringLiteral("import")));
    }

    void testInMemoryRequireBlocksEscapingAndCircularDeps()
    {
        QTemporaryDir pkgDir;
        QVERIFY(pkgDir.isValid());

        auto escaping = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral("const x = host.require('../secret.mjs'); module.exports = { search: function() { return []; } };\n")}
        });
        QVERIFY(escaping.isValid());
        JsProvider p1(escaping);
        QVERIFY(!p1.isLoaded());

        auto circular = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral("const a = host.require('./a.mjs'); module.exports = { search: function() { return []; } };\n")},
            {QStringLiteral("a.mjs"), QByteArrayLiteral("const b = host.require('./main.mjs'); module.exports = { a: 1 };\n")}
        });
        QVERIFY(circular.isValid());
        JsProvider p2(circular);
        QVERIFY(!p2.isLoaded());
        QVERIFY(p2.loadError().contains(QStringLiteral("Circular")));
    }

    void testTamperedDiskContentAfterApprovalRejectedBeforeEvaluation()
    {
        QTemporaryDir pkgDir;
        QVERIFY(pkgDir.isValid());

        auto pkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral("module.exports = { search: function() { return []; } };\n")}
        });
        QVERIFY(pkg.isValid());
        QVERIFY(writeFile(pkgDir.filePath(QStringLiteral("main.mjs")),
                          QByteArrayLiteral("throw new Error('TAMPERED_EXECUTED');\nmodule.exports = { search: function() { return []; } };\n")));

        JsProvider provider(pkg);
        QVERIFY(!provider.isLoaded());
        QVERIFY(provider.loadError().contains(QStringLiteral("changed")));
        QVERIFY(!provider.loadError().contains(QStringLiteral("TAMPERED_EXECUTED")));
    }

    void testVendoredAcornRemoved()
    {
        QVERIFY(!QFile::exists(QStringLiteral(":/klookup/acorn.js")));
    }
};

QTEST_MAIN(TestAcornParserValidation)
#include "test_acorn_parser_validation.moc"
