// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providerdiagnostics.h"
#include "searchprovider.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTextStream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    auto package = ProviderPackage::fromDirectory(QString::fromLocal8Bit(argv[1]));
    package.setTrusted(true); package.setEnabled(true); // Explicit test-owned source, never saved to user settings.
    ProviderDiagnostics diagnostics;
    QObject::connect(&diagnostics, &ProviderDiagnostics::finished, &app, [&app](quint64, const QJsonObject &report) {
        QTextStream(stdout) << QJsonDocument(report).toJson();
        app.exit(report.value(QStringLiteral("counts")).toObject().value(QStringLiteral("pass")).toInt() == 1 ? 0 : 1);
    });
    diagnostics.run({{package.id(), QStringLiteral("javascript"), package.packageDir(), package.manifest().version, package, {}, true}}, ProviderDiagnostics::Mode::Offline);
    return app.exec();
}
