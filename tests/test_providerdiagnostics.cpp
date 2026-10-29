// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providerdiagnostics.h"
#include "settingsdialog.h"
#include "searchprovider.h"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestProviderDiagnostics : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    ProviderDiagnostics::Source source(const QString &id, const QByteArray &script, bool trusted = true, bool enabled = true) {
        const auto path = root.filePath(id);
        QDir().mkpath(path);
        QFile manifest(path + QStringLiteral("/provider.json"));
        if (!manifest.open(QIODevice::WriteOnly)) return {};
        manifest.write(QJsonDocument(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), id}, {QStringLiteral("permissions"), QJsonObject{{QStringLiteral("origins"), QJsonArray{QStringLiteral("https://example.invalid")}}}}}).toJson()); manifest.close();
        QFile entry(path + QStringLiteral("/main.mjs"));
        if (!entry.open(QIODevice::WriteOnly)) return {};
        entry.write(script); entry.close();
        auto pkg = ProviderPackage::fromDirectory(path);
        pkg.setTrusted(trusted); pkg.setEnabled(enabled);
        return {id, QStringLiteral("javascript"), path, QStringLiteral("test"), pkg, {}, enabled};
    }
    QJsonObject check(const QList<ProviderDiagnostics::Source> &sources, ProviderDiagnostics::Mode mode) {
        ProviderDiagnostics runner;
        QSignalSpy done(&runner, &ProviderDiagnostics::finished);
        runner.run(sources, mode);
        if (!done.wait(10000)) return {};
        return done.first()[1].toJsonObject();
    }
private slots:
    void preflightNeverEvaluatesCodeAndFailuresAreIsolated() {
        auto throwing = source(QStringLiteral("throwing"), "throw new Error('SECRET_do_not_show'); export function search() {}");
        auto disabled = source(QStringLiteral("disabled"), "export function search() {}", true, false);
        auto untrusted = source(QStringLiteral("untrusted"), "throw new Error('SECRET_do_not_show'); export function search() {}", false);
        auto missing = source(QStringLiteral("missing"), "export function search() {}");
        QFile::remove(missing.path + QStringLiteral("/main.mjs"));
        auto report = check({throwing, disabled, untrusted, missing}, ProviderDiagnostics::Mode::Preflight);
        QCOMPARE(report.value(QStringLiteral("results")).toArray().size(), 4);
        const auto rows = report.value(QStringLiteral("results")).toArray();
        QCOMPARE(rows[0].toObject().value(QStringLiteral("code")).toString(), QStringLiteral("package.preflight"));
        QCOMPARE(rows[1].toObject().value(QStringLiteral("status")).toString(), QStringLiteral("skipped"));
        QCOMPARE(rows[2].toObject().value(QStringLiteral("status")).toString(), QStringLiteral("warn"));
        QCOMPARE(rows[3].toObject().value(QStringLiteral("status")).toString(), QStringLiteral("fail"));
        QVERIFY(!QJsonDocument(report).toJson().contains("SECRET"));
    }
    void optionalOperationsNetworkDenialAndSafeErrors() {
        auto noCheck = source(QStringLiteral("optional"), "export function search() {}");
        auto good = source(QStringLiteral("good"), "export function search() {} export function selfTest() { return {passed:true}; }");
        auto bad = source(QStringLiteral("bad"), "export function search() {} export function selfTest() { throw Error('SECRET_TOKEN=private'); }");
        auto network = source(QStringLiteral("network"), "export function search() {} export function selfTest() { return host.fetch('https://example.invalid').then(function() { return {passed:false}; }, function() { return {passed:true}; }); }");
        auto report = check({noCheck, bad, good, network}, ProviderDiagnostics::Mode::Offline);
        auto rows = report.value(QStringLiteral("results")).toArray();
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[0].toObject().value(QStringLiteral("status")).toString(), QStringLiteral("unsupported"));
        QCOMPARE(rows[1].toObject().value(QStringLiteral("status")).toString(), QStringLiteral("fail"));
        QCOMPARE(rows[2].toObject().value(QStringLiteral("status")).toString(), QStringLiteral("pass"));
        QCOMPARE(rows[3].toObject().value(QStringLiteral("status")).toString(), QStringLiteral("pass"));
        QVERIFY(!QJsonDocument(report).toJson().contains("SECRET"));
        for (const auto &value : rows) {
            const auto row = value.toObject();
            QVERIFY(row.contains(QStringLiteral("timestamp")) && row.contains(QStringLiteral("durationMs")) && row.contains(QStringLiteral("remediation")));
        }
    }
    void timeoutCancellationAndIndependentRunners() {
        auto pending = source(QStringLiteral("pending"), "export function search() {} export function selfTest() { return new Promise(function() {}); }");
        auto good = source(QStringLiteral("healthy"), "export function search() {} export function selfTest() { return {passed:true}; }");
        auto report = check({pending, good}, ProviderDiagnostics::Mode::Offline);
        const auto rows = report.value(QStringLiteral("results")).toArray();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].toObject().value(QStringLiteral("code")).toString(), QStringLiteral("check.timeout"));
        QCOMPARE(rows[1].toObject().value(QStringLiteral("status")).toString(), QStringLiteral("pass"));
        ProviderDiagnostics first, second;
        QSignalSpy a(&first, &ProviderDiagnostics::finished), b(&second, &ProviderDiagnostics::finished);
        first.run({pending, good}, ProviderDiagnostics::Mode::Offline);
        second.run({good}, ProviderDiagnostics::Mode::Offline);
        QTRY_COMPARE(b.count(), 1);
        first.cancel();
        QCOMPARE(a.count(), 1);
        const auto cancelled = a.first()[1].toJsonObject();
        QCOMPARE(cancelled.value(QStringLiteral("counts")).toObject().value(QStringLiteral("skipped")).toInt(), 2);
        first.run({good}, ProviderDiagnostics::Mode::Offline);
        QTRY_COMPARE(a.count(), 2);
        QCOMPARE(a.last()[1].toJsonObject().value(QStringLiteral("counts")).toObject().value(QStringLiteral("pass")).toInt(), 1);
    }
    void meaningfulNativeAndScriptExamples() {
        auto pkg = ProviderPackage::fromDirectory(QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/tvmaze"));
        pkg.setTrusted(true); pkg.setEnabled(true);
        ProviderDiagnostics::Source js{QStringLiteral("tvmaze"), QStringLiteral("javascript"), pkg.packageDir(), pkg.manifest().version, pkg, {}, true};
        ProviderDiagnostics::Source native{QStringLiteral("github"), QStringLiteral("native"), QStringLiteral(NATIVE_GITHUB), {}, {}, {}, true};
        auto report = check({native, js}, ProviderDiagnostics::Mode::Offline);
        QCOMPARE(report.value(QStringLiteral("results")).toArray().size(), 2);
        QVERIFY2(report.value(QStringLiteral("counts")).toObject().value(QStringLiteral("pass")).toInt() == 2, QJsonDocument(report).toJson().constData());
        const auto unsupported = check({js}, ProviderDiagnostics::Mode::Live);
        QCOMPARE(unsupported.value(QStringLiteral("counts")).toObject().value(QStringLiteral("unsupported")).toInt(), 1);
    }
    void cliJsonMissingProviderAndPassiveInventory() {
        QProcess process;
        process.start(QStringLiteral(CHECK_BINARY), {QStringLiteral("--provider"), QStringLiteral("does-not-exist")});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 1);
        const auto report = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        QCOMPARE(report.value(QStringLiteral("schema")).toString(), QStringLiteral("klookup.diagnostics.v1"));
        QCOMPARE(report.value(QStringLiteral("counts")).toObject().value(QStringLiteral("fail")).toInt(), 1);
        process.start(QStringLiteral(CHECK_BINARY), {QStringLiteral("--all")});
        QVERIFY(process.waitForFinished(10000));
        const auto inventory = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value(QStringLiteral("results")).toArray();
        QVERIFY(!inventory.isEmpty());
        bool github = false;
        for (const auto &value : inventory) if (value.toObject().value(QStringLiteral("id")).toString() == QLatin1String("github")) github = true;
        QVERIFY(github);
    }
    void settingsExposesIndividualAndAggregateControls() {
        auto example = source(QStringLiteral("ui"), "throw new Error('must not execute'); export function search() {}");
        SettingsDialog dialog({}, nullptr, nullptr, {{example.package, QStringLiteral("Ready")}});
        auto *one = dialog.findChild<QPushButton *>(QStringLiteral("checkProvider"));
        auto *all = dialog.findChild<QPushButton *>(QStringLiteral("checkAllProviders"));
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("diagnosticProviders"));
        auto *output = dialog.findChild<QPlainTextEdit *>(QStringLiteral("diagnosticOutput"));
        QVERIFY(one && all && list && output);
        QVERIFY(output->toPlainText().isEmpty());
        int selected = -1;
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->text().startsWith(QStringLiteral("ui ("))) selected = i;
        QVERIFY(selected >= 0); list->setCurrentRow(selected); one->click();
        QTRY_VERIFY(output->toPlainText().contains(QStringLiteral("package.preflight")));
        QVERIFY(!output->toPlainText().contains(QStringLiteral("must not execute")));
    }
};
int main(int argc, char **argv) {
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    TestProviderDiagnostics test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_providerdiagnostics.moc"
