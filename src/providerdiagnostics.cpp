// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providerdiagnostics.h"
#include "providercheck.h"
#include "jsprovider.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QPluginLoader>
#include <QProcess>
#include <QTemporaryDir>
#include <QSet>
#include <QTimer>

ProviderDiagnostics::ProviderDiagnostics(QObject *parent) : QObject(parent) {}
ProviderDiagnostics::~ProviderDiagnostics() { cancel(); }

QList<ProviderDiagnostics::Source> ProviderDiagnostics::discover(const QStringList &packages, const QStringList &nativePaths)
{
    QList<Source> sources;
    QSet<QString> ids;
    const KConfigGroup settings(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Providers"));
    for (auto package : ProviderPackageDiscovery::discoverPackages(packages)) {
        package.setTrusted(package.approved(settings));
        package.setEnabled(settings.readEntry(package.id() + QStringLiteral("_Enabled"), true));
        sources.append({package.id(), QStringLiteral("javascript"), package.packageDir(), package.manifest().version, package, {}, package.isEnabled()});
        ids.insert(package.id());
    }
    // Preserve previously seen missing packages in a passive CLI inventory too.
    for (const auto &key : settings.keyList()) {
        if (!key.endsWith(QStringLiteral("_LastPath"))) continue;
        const auto id = key.chopped(9);
        if (ids.contains(id)) continue;
        const auto path = settings.readEntry(key, QString());
        sources.append({id, QStringLiteral("javascript"), path, {}, ProviderPackage::fromDirectory(path), {}, settings.readEntry(id + QStringLiteral("_Enabled"), true)});
        ids.insert(id);
    }
    QStringList paths = nativePaths;
    paths << QCoreApplication::applicationDirPath() + QStringLiteral("/providers")
          << QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/plugins/klookup")
          << QCoreApplication::applicationDirPath() + QStringLiteral("/../lib64/plugins/klookup");
    for (const auto &path : QCoreApplication::libraryPaths()) paths << path + QStringLiteral("/klookup/providers") << path + QStringLiteral("/klookup");
    QSet<QString> files;
    for (const auto &path : paths) {
        const QDir directory(path);
        for (const auto &file : directory.entryInfoList(QDir::Files)) {
            if (!QLibrary::isLibrary(file.fileName()) || files.contains(file.canonicalFilePath())) continue;
            files.insert(file.canonicalFilePath());
            QPluginLoader loader(file.absoluteFilePath());
            if (loader.metaData().value(QStringLiteral("IID")).toString() != QLatin1String("org.kde.klookup.SearchProvider")) continue;
            auto id = file.completeBaseName();
            if (id.startsWith(QStringLiteral("lib"))) id.remove(0, 3);
            if (id.startsWith(QStringLiteral("provider_"))) id.remove(0, 9);
            if (id == QLatin1String("tvmaze") || id == QLatin1String("lyrics_ovh") || id == QLatin1String("openlibrary") || id == QLatin1String("anilist") || ids.contains(id)) continue;
            ids.insert(id);
            sources.append({id, QStringLiteral("native"), file.absoluteFilePath(), {}, {}, {}, settings.readEntry(id + QStringLiteral("_Enabled"), true)});
        }
    }
    const auto builtin = QString::fromLatin1(KLOOKUP_BUILTIN_NATIVE_IDS).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (auto id : builtin) {
        id.remove(0, 9); // CMake target prefix provider_
        if (!ids.contains(id)) sources.append({id, QStringLiteral("native"), QStringLiteral("<missing builtin module>"), {}, {}, {}, settings.readEntry(id + QStringLiteral("_Enabled"), true)});
    }
    return sources;
}

QList<ProviderDiagnostics::Source> ProviderDiagnostics::forSettings(const QList<SearchProvider *> &providers, const QList<ProviderPackageStatus> &packages)
{
    auto sources = discover();
    for (auto *provider : providers) {
        bool found = false;
        for (auto &source : sources) if (source.id == provider->id()) { source.native = provider; if (source.path == QLatin1String("<missing builtin module>")) source.path.clear(); source.enabled = provider->isEnabled(); found = true; }
        if (!found) sources.append({provider->id(), QStringLiteral("native"), {}, {}, {}, provider, provider->isEnabled()});
    }
    for (const auto &record : packages) {
        bool found = false;
        for (auto &source : sources) if (source.id == record.package.id()) { source.package = record.package; source.path = record.package.packageDir(); source.version = record.package.manifest().version; source.type = QStringLiteral("javascript"); source.enabled = record.package.isEnabled(); found = true; }
        if (!found) sources.append({record.package.id(), QStringLiteral("javascript"), record.package.packageDir(), record.package.manifest().version, record.package, {}, record.package.isEnabled()});
    }
    return sources;
}

quint64 ProviderDiagnostics::run(const QList<Source> &sources, Mode mode)
{
    cancel();
    ++m_run; m_queue = sources; m_results.clear(); m_mode = mode; m_running = true;
    QTimer::singleShot(0, this, [this, generation = m_run] { if (generation == m_run) next(); });
    return m_run;
}
void ProviderDiagnostics::cancel()
{
    if (m_script) { delete m_script.data(); m_script = nullptr; }
    if (m_process) { m_process->disconnect(this); m_process->kill(); m_process->waitForFinished(500); delete m_process; m_process = nullptr; }
    if (m_nativeDeadline) { delete m_nativeDeadline; m_nativeDeadline = nullptr; }
    if (m_running) {
        const auto pending = m_queue;
        m_queue.clear(); m_running = false;
        if (m_hasCurrent) report(QStringLiteral("skipped"), QStringLiteral("check.cancelled"), QStringLiteral("Check cancelled"));
        for (const auto &source : pending) {
            m_current = source; m_elapsed.start();
            report(QStringLiteral("skipped"), QStringLiteral("check.cancelled"), QStringLiteral("Check cancelled before execution"));
        }
        emit finished(m_run, aggregate(m_results));
    }
}
void ProviderDiagnostics::next()
{
    if (!m_running) return;
    if (m_script) { m_script->deleteLater(); m_script = nullptr; }
    if (m_process) { m_process->disconnect(this); m_process->kill(); m_process->waitForFinished(500); delete m_process; m_process = nullptr; }
    if (m_nativeDeadline) { delete m_nativeDeadline; m_nativeDeadline = nullptr; }
    if (m_queue.isEmpty()) { m_running = false; emit finished(m_run, aggregate(m_results)); return; }
    m_current = m_queue.takeFirst(); m_hasCurrent = true; m_elapsed.start();
    if (!m_current.enabled) { report(QStringLiteral("skipped"), QStringLiteral("provider.disabled"), QStringLiteral("Provider is disabled"), QStringLiteral("Enable it in Settings before an active check")); return; }
    if (m_current.type == QLatin1String("javascript")) {
        const auto &pkg = m_current.package;
        if (!pkg.isValid()) { report(QStringLiteral("fail"), QStringLiteral("package.invalid"), QStringLiteral("Package missing or invalid"), QStringLiteral("Reinstall the package and retry discovery")); return; }
        const auto current = ProviderPackage::fromDirectory(pkg.packageDir(), pkg.locationType());
        if (!current.isValid() || current.approvalFingerprint() != pkg.approvalFingerprint()) { report(QStringLiteral("fail"), QStringLiteral("package.changed"), QStringLiteral("Package contents changed"), QStringLiteral("Rediscover and review approval")); return; }
        if (!pkg.isTrusted()) { report(QStringLiteral("warn"), QStringLiteral("package.unapproved"), QStringLiteral("Approval required; script was not executed"), QStringLiteral("Review requested permissions in Settings")); return; }
        if (pkg.runtime() != QLatin1String("javascript")) { report(QStringLiteral("unsupported"), QStringLiteral("runtime.unsupported"), QStringLiteral("No installed runtime adapter")); return; }
        QTemporaryDir snapshot;
        QString syntaxError;
        if (!pkg.writeVerifiedSnapshot(snapshot.path(), &syntaxError)) { report(QStringLiteral("fail"), QStringLiteral("package.syntax"), QStringLiteral("Package module syntax or imports are invalid")); return; }
        if (m_mode == Mode::Preflight) { report(QStringLiteral("pass"), QStringLiteral("package.preflight"), QStringLiteral("Package integrity, manifest and approval verified; implementation not executed")); return; }
        auto *host = new ProviderHost::ProviderHostService(pkg);
        host->setDiagnosticPolicy(m_mode == Mode::Live);
        m_script = new JsProvider(pkg, this, host); host->setParent(m_script);
        if (!m_script->isLoaded()) { report(QStringLiteral("fail"), QStringLiteral("runtime.load"), QStringLiteral("Approved module could not load"), QStringLiteral("Reinstall or report the package version")); return; }
        if (!m_script->hasCheck(m_mode == Mode::Live)) { report(QStringLiteral("unsupported"), QStringLiteral("check.unsupported"), QStringLiteral("Provider does not export this optional check")); return; }
        const auto generation = m_run;
        connect(m_script, &JsProvider::checkReady, this, [this, generation](quint64, const QJsonObject &value) {
            if (generation != m_run || !m_running) return;
            const bool passed = value.value(QStringLiteral("passed")).isBool() && value.value(QStringLiteral("passed")).toBool();
            report(passed ? QStringLiteral("pass") : QStringLiteral("fail"), passed ? QStringLiteral("check.passed") : QStringLiteral("check.failed"),
                   passed ? QStringLiteral("Provider-specific check passed") : QStringLiteral("Provider-specific check failed"), passed ? QString() : QStringLiteral("Reinstall or report the provider version"));
        });
        connect(m_script, &JsProvider::checkFailed, this, [this, generation](quint64, ProviderApiV1::ErrorCode code) {
            if (generation != m_run || !m_running) return;
            report(QStringLiteral("fail"), code == ProviderApiV1::ErrorCode::Timeout ? QStringLiteral("check.timeout") : QStringLiteral("check.execution"), QStringLiteral("Provider check failed or exceeded its budget"), QStringLiteral("Retry this provider individually"));
        });
        m_script->setOperationTimeoutMs(1000);
        m_script->check(m_mode == Mode::Live);
        return;
    }
    if (!m_current.path.isEmpty() && !QFileInfo::exists(m_current.path)) { report(QStringLiteral("fail"), QStringLiteral("native.missing"), QStringLiteral("Native module is missing")); return; }
    if (m_mode == Mode::Preflight) { report(QStringLiteral("pass"), QStringLiteral("native.preflight"), QStringLiteral("Native module metadata available; implementation not executed")); return; }
    if (m_mode == Mode::Live) { report(QStringLiteral("unsupported"), QStringLiteral("live.unsupported"), QStringLiteral("Native provider has no optional live probe")); return; }
    if (m_current.path.isEmpty()) { report(QStringLiteral("unsupported"), QStringLiteral("native.no-path"), QStringLiteral("No module path available for an isolated native check")); return; }
    QString executable = QCoreApplication::applicationDirPath() + QStringLiteral("/klookup-provider-check");
    if (!QFileInfo::exists(executable)) executable = QCoreApplication::applicationDirPath() + QStringLiteral("/../src/klookup-provider-check");
    if (!QFileInfo::exists(executable)) { report(QStringLiteral("fail"), QStringLiteral("dependency.check-tool"), QStringLiteral("Diagnostic helper is unavailable"), QStringLiteral("Reinstall the diagnostic executable")); return; }
    m_process = new QProcess(this);
    m_nativeDeadline = new QTimer(this); m_nativeDeadline->setSingleShot(true);
    connect(m_nativeDeadline, &QTimer::timeout, this, [this] {
        if (!m_process) return;
        m_process->disconnect(this); m_process->kill();
        report(QStringLiteral("fail"), QStringLiteral("check.timeout"), QStringLiteral("Native check exceeded its deadline"));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_process) m_process->disconnect(this);
        report(QStringLiteral("fail"), QStringLiteral("native.process"), QStringLiteral("Native check process failed"));
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        if (m_process && m_process->bytesAvailable() > 65536) {
            m_process->disconnect(this); m_process->kill();
            report(QStringLiteral("fail"), QStringLiteral("check.output-limit"), QStringLiteral("Native check exceeded output limit"));
        }
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        m_nativeDeadline->stop();
        const auto result = QJsonDocument::fromJson(m_process->readAllStandardOutput()).object();
        if (exitStatus != QProcess::NormalExit || exitCode != 0 || !result.value(QStringLiteral("supported")).isBool()) {
            report(QStringLiteral("fail"), QStringLiteral("native.process"), QStringLiteral("Native check process failed")); return;
        }
        if (!result.value(QStringLiteral("supported")).toBool()) { report(QStringLiteral("unsupported"), QStringLiteral("check.unsupported"), QStringLiteral("Native plugin has no optional ProviderCheck/1.0 interface")); return; }
        const bool passed = result.value(QStringLiteral("passed")).toBool();
        report(passed ? QStringLiteral("pass") : QStringLiteral("fail"), passed ? QStringLiteral("check.passed") : QStringLiteral("check.failed"), QStringLiteral("Native provider-specific offline fixture check completed"));
    });
    m_process->setStandardErrorFile(QProcess::nullDevice());
    m_process->start(executable, {QStringLiteral("--native-self-test"), m_current.path});
    m_nativeDeadline->start(1500);

}
void ProviderDiagnostics::report(QString status, QString code, QString message, QString remedy)
{
    QJsonObject row{{QStringLiteral("schema"), QStringLiteral("klookup.diagnostics.v1")}, {QStringLiteral("id"), m_current.id},
        {QStringLiteral("type"), m_current.type}, {QStringLiteral("version"), m_current.version},
        {QStringLiteral("status"), status}, {QStringLiteral("code"), code}, {QStringLiteral("message"), message},
        {QStringLiteral("remediation"), remedy}, {QStringLiteral("durationMs"), double(m_elapsed.elapsed())},
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("mode"), m_mode == Mode::Preflight ? QStringLiteral("preflight") : m_mode == Mode::Offline ? QStringLiteral("offline") : QStringLiteral("live")},
        {QStringLiteral("capabilities"), ProviderApiV1::toJson(m_current.package.manifest().capabilities)},
        {QStringLiteral("dependencies"), QJsonObject{{QStringLiteral("qt"), QString::fromLatin1(qVersion())}, {QStringLiteral("protocol"), QString::fromLatin1(ProviderApiV1::ProtocolVersion)}}}};
    m_hasCurrent = false;
    m_results.append(row); emit resultReady(m_run, row);
    QTimer::singleShot(0, this, [this, generation = m_run] { if (generation == m_run) next(); });
}
QJsonObject ProviderDiagnostics::aggregate(const QList<QJsonObject> &reports)
{
    QJsonArray rows; QJsonObject counts;
    for (const auto &status : {"pass", "warn", "fail", "skipped", "unsupported"}) counts.insert(QString::fromLatin1(status), 0);
    for (const auto &row : reports) { rows.append(row); const auto status = row.value(QStringLiteral("status")).toString(); counts.insert(status, counts.value(status).toInt() + 1); }
    QJsonArray shared;
    shared.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("discovery")}, {QStringLiteral("status"), QStringLiteral("pass")}, {QStringLiteral("code"), QStringLiteral("inventory.complete")}});
    for (const auto &component : {"ipc", "auth-broker", "network-broker", "renderer"})
        shared.append(QJsonObject{{QStringLiteral("id"), QString::fromLatin1(component)}, {QStringLiteral("status"), QStringLiteral("unsupported")},
            {QStringLiteral("code"), QStringLiteral("shared.no-probe")}, {QStringLiteral("message"), QStringLiteral("No automatic active shared-service probe; provider failures remain isolated")}});
    return {{QStringLiteral("schema"), QStringLiteral("klookup.diagnostics.v1")}, {QStringLiteral("results"), rows}, {QStringLiteral("counts"), counts}, {QStringLiteral("shared"), shared}};
}
