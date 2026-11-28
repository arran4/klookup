// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jsprovider.h"
#include "providerpackage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

class CountingNetworkManager : public QNetworkAccessManager {
public:
    explicit CountingNetworkManager(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}
    int requestCount = 0;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData = nullptr) override {
        ++requestCount;
        return QNetworkAccessManager::createRequest(op, request, outgoingData);
    }
};

bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(content);
    file.close();
    return true;
}

ProviderPackage buildPackage(const QString &dirPath, const QMap<QString, QByteArray> &modules,
                             const QJsonObject &manifestOverrides = {}, bool trusted = true)
{
    QJsonObject manifest;
    manifest[QStringLiteral("id")] = QStringLiteral("script_api_v2_pkg");
    manifest[QStringLiteral("name")] = QStringLiteral("Script API v2 Package");
    manifest[QStringLiteral("version")] = QStringLiteral("1.0.0");
    manifest[QStringLiteral("apiVersion")] = 2;
    manifest[QStringLiteral("runtime")] = QStringLiteral("javascript");
    manifest[QStringLiteral("entry")] = QStringLiteral("main.mjs");
    for (auto it = manifestOverrides.constBegin(); it != manifestOverrides.constEnd(); ++it) {
        manifest[it.key()] = it.value();
    }

    writeFile(dirPath + QStringLiteral("/provider.json"), QJsonDocument(manifest).toJson());
    for (auto it = modules.cbegin(); it != modules.cend(); ++it) {
        QFileInfo info(dirPath + QLatin1Char('/') + it.key());
        QDir().mkpath(info.absolutePath());
        writeFile(info.filePath(), it.value());
    }

    auto pkg = ProviderPackage::fromDirectory(dirPath);
    pkg.setTrusted(trusted);
    return pkg;
}

} // namespace

class TestJsApiV2Security : public QObject
{
    Q_OBJECT

private slots:
    void testRuntimeCompatibilityProbeRejectsDynamicImportAndImportMeta()
    {
        QString error;
        QVERIFY2(JsProvider::verifyEngineRejectsDynamicImport(&error), qPrintable(error));
        QVERIFY(error.isEmpty());
    }

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
            "const a = host.require('./helper.mjs');\n"
            "const b = host.require('./helper.mjs');\n"
            "const c = host.require('./sub/extensionless');\n"
            "module.exports = {\n"
            "    search: function(q) { return [{ id: '1', title: a.val + ':' + (a === b) + ':' + c.sub + ':' + host.apiVersion + ':' + q + ':' + s1.length + s2.length + re.source.length + tpl.length }]; }\n"
            "};\n";

        auto pkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), mainCode},
            {QStringLiteral("helper.mjs"), QByteArrayLiteral("module.exports = { val: 'ok' };\n")},
            {QStringLiteral("sub/extensionless"), QByteArrayLiteral("module.exports = { sub: 'ext' };\n")}
        });
        QVERIFY(pkg.isValid());

        JsProvider provider(pkg);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        provider.search(QStringLiteral("hello"));
        QTRY_COMPARE(spy.count(), 1);
        const SearchPage page = spy.first().at(1).value<SearchPage>();
        QCOMPARE(page.results.size(), 1);
        QVERIFY(page.results.first().title.startsWith(QStringLiteral("ok:true:ext:2:hello:")));
    }

    void testStaticDynamicImportsAndImportMetaRejectedBeforeExecution()
    {
        QTemporaryDir pkgDir;
        QVERIFY(pkgDir.isValid());

        const QList<QByteArray> rejectedScripts = {
            QByteArrayLiteral("throw new Error('MUST_NOT_EXECUTE');\nimport { val } from './helper.mjs';\nmodule.exports = { search: function() {} };\n"),
            QByteArrayLiteral("throw new Error('MUST_NOT_EXECUTE');\nexport function search() { return []; }\n"),
            QByteArrayLiteral("throw new Error('MUST_NOT_EXECUTE');\nmodule.exports = { search: function() { return import('file:///etc/passwd'); } };\n"),
            QByteArrayLiteral("throw new Error('MUST_NOT_EXECUTE');\nfunction inner() { const f = () => `${import('./helper.mjs')}`; return f; }\nmodule.exports = { search: function() { return []; } };\n"),
            QByteArrayLiteral("throw new Error('MUST_NOT_EXECUTE');\nfunction* gen() { yield import('./helper.mjs'); }\nmodule.exports = { search: function() { return []; } };\n"),
            QByteArrayLiteral("throw new Error('MUST_NOT_EXECUTE');\nasync function a() { return await import('./helper.mjs'); }\nmodule.exports = { search: function() { return []; } };\n"),
            QByteArrayLiteral("throw new Error('MUST_NOT_EXECUTE');\nmodule.exports = { search: function() { return import.meta.url; } };\n"),
            QByteArrayLiteral("throw new Error('MUST_NOT_EXECUTE');\nconst fn = function() { return `meta=${import.meta}`; };\nmodule.exports = { search: function() { return []; } };\n"),
        };

        for (const QByteArray &script : rejectedScripts) {
            auto pkg = buildPackage(pkgDir.path(), {
                {QStringLiteral("main.mjs"), script},
                {QStringLiteral("helper.mjs"), QByteArrayLiteral("module.exports = { val: 1 };\n")}
            });
            QVERIFY(pkg.isValid());

            JsProvider provider(pkg);
            QVERIFY(!provider.isLoaded());
            QVERIFY(!provider.loadError().contains(QStringLiteral("MUST_NOT_EXECUTE")));
            QVERIFY2(provider.loadError().contains(QStringLiteral("SyntaxError")) ||
                     provider.loadError().contains(QStringLiteral("import")),
                     qPrintable(provider.loadError()));
        }

        // Also reject static/dynamic imports inside a host.require()'d helper module
        auto helperImportPkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral("const h = host.require('./helper.mjs'); module.exports = { search: function() { return []; } };\n")},
            {QStringLiteral("helper.mjs"), QByteArrayLiteral("throw new Error('HELPER_MUST_NOT_EXECUTE');\nimport { x } from './other.mjs';\nmodule.exports = { val: x };\n")},
            {QStringLiteral("other.mjs"), QByteArrayLiteral("module.exports = { x: 1 };\n")}
        });
        QVERIFY(helperImportPkg.isValid());
        JsProvider helperProvider(helperImportPkg);
        QVERIFY(!helperProvider.isLoaded());
        QVERIFY(!helperProvider.loadError().contains(QStringLiteral("HELPER_MUST_NOT_EXECUTE")));
    }

    void testRuntimeEvalAndFunctionConstructorsFailClosedForImportAndImportMeta()
    {
        QTemporaryDir pkgDir;
        QVERIFY(pkgDir.isValid());

        const QList<QByteArray> runtimePayloads = {
            QByteArrayLiteral("module.exports = { search: function() { return eval(\"import('file:///etc/passwd')\"); } };\n"),
            QByteArrayLiteral("module.exports = { search: function() { return (0, eval)(\"import('./other.mjs')\"); } };\n"),
            QByteArrayLiteral("module.exports = { search: function() { return Function(\"return import('file:///etc/passwd')\")(); } };\n"),
            QByteArrayLiteral("module.exports = { search: function() { return new Function(\"return import('./other.mjs')\")(); } };\n"),
            QByteArrayLiteral("module.exports = { search: function() { return (function(){}).constructor(\"return import.meta\")(); } };\n"),
        };

        for (const QByteArray &script : runtimePayloads) {
            auto pkg = buildPackage(pkgDir.path(), {{QStringLiteral("main.mjs"), script}});
            QVERIFY(pkg.isValid());
            JsProvider provider(pkg);
            QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));
            QSignalSpy failSpy(&provider, &ProviderApiV1::ProviderV1::pageFailed);
            provider.search(QStringLiteral("q"));
            QTRY_COMPARE(failSpy.count(), 1);
            const QString errMsg = failSpy.first().at(2).toString();
            QVERIFY2(errMsg.contains(QStringLiteral("SyntaxError")) || errMsg.contains(QStringLiteral("import")),
                     qPrintable(errMsg));
        }
    }

    void testInMemoryRequireConfinementAndCircularDependencies()
    {
        QTemporaryDir pkgDir;
        QVERIFY(pkgDir.isValid());

        for (const QString &badSpec : {
                 QStringLiteral("../secret.mjs"),
                 QStringLiteral("/etc/passwd"),
                 QStringLiteral("file:///etc/passwd"),
                 QStringLiteral("https://example.invalid/mod.mjs"),
                 QStringLiteral("bare_module"),
                 QStringLiteral("./missing.mjs"),
                 QStringLiteral("./sub/../../escape.mjs"),
                 QStringLiteral("./helper.mjs?query=1"),
                 QStringLiteral("./helper.mjs#frag"),
             }) {
            const QByteArray code = QStringLiteral(
                "host.require('%1'); module.exports = { search: function() { return []; } };\n").arg(badSpec).toUtf8();
            auto pkg = buildPackage(pkgDir.path(), {
                {QStringLiteral("main.mjs"), code},
                {QStringLiteral("helper.mjs"), QByteArrayLiteral("module.exports = { ok: true };\n")}
            });
            QVERIFY(pkg.isValid());
            JsProvider provider(pkg);
            QVERIFY2(!provider.isLoaded(), qPrintable(badSpec));
        }

        auto circular = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral("const a = host.require('./a.mjs'); module.exports = { search: function() { return []; } };\n")},
            {QStringLiteral("a.mjs"), QByteArrayLiteral("const b = host.require('./main.mjs'); module.exports = { a: 1 };\n")}
        });
        QVERIFY(circular.isValid());
        JsProvider p2(circular);
        QVERIFY(!p2.isLoaded());
        QVERIFY(p2.loadError().contains(QStringLiteral("Circular")));
    }

    void testTamperedContentAfterApprovalRejectedBeforeEvaluation()
    {
        QTemporaryDir pkgDir;
        QVERIFY(pkgDir.isValid());

        auto pkg = buildPackage(pkgDir.path(), {
            {QStringLiteral("main.mjs"), QByteArrayLiteral("const h = host.require('./helper.mjs'); module.exports = { search: function() { return []; } };\n")},
            {QStringLiteral("helper.mjs"), QByteArrayLiteral("module.exports = { ok: true };\n")}
        });
        QVERIFY(pkg.isValid());
        QVERIFY(writeFile(pkgDir.filePath(QStringLiteral("helper.mjs")),
                          QByteArrayLiteral("throw new Error('TAMPERED_EXECUTED');\nmodule.exports = { ok: false };\n")));

        JsProvider provider(pkg);
        QVERIFY(!provider.isLoaded());
        QVERIFY(!provider.loadError().contains(QStringLiteral("TAMPERED_EXECUTED")));
        QVERIFY(provider.loadError().contains(QStringLiteral("changed")));
    }

    void testManifestApiVersionValidationAndPermissionsIsolation()
    {
        QTemporaryDir pkgDir;
        QVERIFY(pkgDir.isValid());

        for (const QJsonValue &badVersion : {QJsonValue(0), QJsonValue(3), QJsonValue(QStringLiteral("2")), QJsonValue(true)}) {
            QJsonObject overrides;
            overrides[QStringLiteral("apiVersion")] = badVersion;
            auto pkg = buildPackage(pkgDir.path(), {{QStringLiteral("main.mjs"), QByteArrayLiteral("module.exports = { search: function() { return []; } };\n")}}, overrides);
            QVERIFY(!pkg.isValid());
            QVERIFY(pkg.validationError().contains(QStringLiteral("apiVersion")));
        }

        // Valid package with limited origin/secret permissions: verify bridge & host hardening
        QJsonObject perms;
        perms[QStringLiteral("origins")] = QJsonArray{QStringLiteral("https://allowed.example")};
        perms[QStringLiteral("secrets")] = QJsonArray{QStringLiteral("allowed_secret")};
        QJsonObject overrides;
        overrides[QStringLiteral("permissions")] = perms;

        const QByteArray code =
            "module.exports = {\n"
            "  search: function() {\n"
            "    if (typeof __klookupBridge !== 'undefined') throw new Error('bridge leaked');\n"
            "    if (!Object.isFrozen(host)) throw new Error('host not frozen');\n"
            "    var s = host.getSecret('undeclared_secret');\n"
            "    return host.fetch('https://forbidden.example/api').then(function() {\n"
            "      return [{ id: 'bad', title: 'unexpected success' }];\n"
            "    }, function(err) {\n"
            "      return [{ id: 'ok', title: 'blocked:' + s + ':' + err.message }];\n"
            "    });\n"
            "  }\n"
            "};\n";

        auto pkg = buildPackage(pkgDir.path(), {{QStringLiteral("main.mjs"), code}}, overrides);
        QVERIFY(pkg.isValid());

        auto *nam = new CountingNetworkManager;
        auto *host = new ProviderHost::ProviderHostService(pkg, nullptr, nam);
        JsProvider provider(pkg, nullptr, host);
        host->setParent(&provider);
        nam->setParent(&provider);
        QVERIFY2(provider.isLoaded(), qPrintable(provider.loadError()));

        QSignalSpy spy(&provider, &ProviderApiV1::ProviderV1::pageReady);
        provider.search(QStringLiteral("q"));
        QTRY_COMPARE(spy.count(), 1);
        const SearchPage page = spy.first().at(1).value<SearchPage>();
        QCOMPARE(page.results.size(), 1);
        QVERIFY(page.results.first().title.startsWith(QStringLiteral("blocked::")));
        QCOMPARE(nam->requestCount, 0);
    }
};

QTEST_MAIN(TestJsApiV2Security)
#include "test_js_apiv2_security.moc"
