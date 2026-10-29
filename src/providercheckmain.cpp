// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providerdiagnostics.h"
#include "providercheck.h"
#include <QPluginLoader>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("klookup-provider-check"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Passive provider inventory by default. --offline executes approved deterministic checks. --live explicitly opts into provider-declared remote probes, without credentials."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("all"), QStringLiteral("Check all installed providers (default)")});
    parser.addOption({QStringLiteral("provider"), QStringLiteral("Check one provider ID"), QStringLiteral("id")});
    parser.addOption({QStringLiteral("offline"), QStringLiteral("Execute approved offline checks with host network disabled")});
    parser.addOption({QStringLiteral("live"), QStringLiteral("Opt in to explicitly exported live probes")});
    parser.addOption({QStringLiteral("package-path"), QStringLiteral("Additional package discovery directory"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("native-path"), QStringLiteral("Additional native discovery directory"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("native-self-test"), QStringLiteral("Internal isolated native offline check"), QStringLiteral("module")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("native-self-test"))) {
        QPluginLoader loader(parser.value(QStringLiteral("native-self-test")));
        QObject *plugin = loader.instance();
        if (!plugin) return 2;
        auto *check = qobject_cast<ProviderCheckV1 *>(plugin);
        QJsonObject result{{QStringLiteral("supported"), bool(check)}};
        if (check) result.insert(QStringLiteral("passed"), check->selfTestV1().value(QStringLiteral("passed")).toBool());
        QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact);
        return 0;
    }
    if (parser.isSet(QStringLiteral("offline")) && parser.isSet(QStringLiteral("live"))) parser.showHelp(2);
    auto sources = ProviderDiagnostics::discover(parser.values(QStringLiteral("package-path")), parser.values(QStringLiteral("native-path")));
    if (parser.isSet(QStringLiteral("provider"))) {
        const auto id = parser.value(QStringLiteral("provider"));
        for (auto it = sources.begin(); it != sources.end();) { if (it->id != id) it = sources.erase(it); else ++it; }
        if (sources.isEmpty()) sources.append({id, QStringLiteral("javascript"), {}, {}, {}, {}, true});
    }
    ProviderDiagnostics runner;
    QObject::connect(&runner, &ProviderDiagnostics::finished, &app, [&app](quint64, const QJsonObject &report) {
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Indented);
        app.exit(report.value(QStringLiteral("counts")).toObject().value(QStringLiteral("fail")).toInt() ? 1 : 0);
    });
    runner.run(sources, parser.isSet(QStringLiteral("live")) ? ProviderDiagnostics::Mode::Live : parser.isSet(QStringLiteral("offline")) ? ProviderDiagnostics::Mode::Offline : ProviderDiagnostics::Mode::Preflight);
    return app.exec();
}
