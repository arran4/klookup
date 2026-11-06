// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providerclassification.h"
#include "providerpackage.h"
#include "providerapiv1.h"
#include "searchprovider.h"
#include "mainwindow.h"
#include "settingsdialog.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

class MetadataProvider : public SearchProvider {
    Q_OBJECT
public:
    using SearchProvider::SearchProvider;
    QString id() const override { return QStringLiteral("cheapshark"); }
    QString name() const override { return QStringLiteral("Fixture"); }
    void search(const QString &) override { ++searches; }
    SearchProvider *clone(QObject *parent) const override { return new MetadataProvider(parent); }
    int searches = 0;
};
class ClassificationTest : public QObject {
    Q_OBJECT
    QTemporaryDir files;
    QJsonObject descriptor(const QJsonArray &facets = {}) const {
        return {{QStringLiteral("version"), QStringLiteral("1.0.0")}, {QStringLiteral("facets"), facets}};
    }
    ProviderPackage package(const QJsonValue &classification, bool include = true) {
        const auto path = files.filePath(QStringLiteral("custom")); QDir().mkpath(path);
        QJsonObject manifest{{QStringLiteral("id"), QStringLiteral("custom")}, {QStringLiteral("name"), QStringLiteral("Custom")}};
        if (include) manifest.insert(QStringLiteral("classification"), classification);
        QFile json(path + QStringLiteral("/provider.json"));
        if (!json.open(QIODevice::WriteOnly)) return {};
        json.write(QJsonDocument(manifest).toJson()); json.close();
        QFile script(path + QStringLiteral("/main.mjs"));
        if (!script.open(QIODevice::WriteOnly)) return {};
        script.write("throw Error('Metadata discovery must never execute this entry'); export function search() { return host.fetch('https://example.invalid'); }"); script.close();
        return ProviderPackage::fromDirectory(path);
    }
private slots:
    void missingUnknownDuplicatesAndMultipleFacets() {
        auto legacy = package({}, false); QVERIFY(legacy.isValid());
        QVERIFY(legacy.manifest().classification.facets.isEmpty());
        auto json = descriptor({QStringLiteral("topic:anime"), QStringLiteral("topic:manga"), QStringLiteral("audience:general"),
            QStringLiteral("currency:USD"), QStringLiteral("currency:AUD"), QStringLiteral("region:au"),
            QStringLiteral("region:nz"), QStringLiteral("topic:science"), QStringLiteral("topic:anime")});
        json.insert(QStringLiteral("requirements"), QJsonArray{QStringLiteral("requirement:login"), QStringLiteral("requirement:terms"), QStringLiteral("requirement:login")});
        auto pkg = package(json); QVERIFY2(pkg.isValid(), qPrintable(pkg.validationError()));
        QCOMPARE(pkg.manifest().classification.facets.size(), 8);
        QCOMPARE(pkg.manifest().classification.requirements.size(), 2);
        QVERIFY(pkg.manifest().classification.description().contains(QStringLiteral("USD pricing")));
        QVERIFY(pkg.manifest().classification.label(QStringLiteral("topic:science")).contains(QStringLiteral("science")));
        QVERIFY(pkg.manifest().classification.label(QStringLiteral("region:au")).contains(QStringLiteral("Australia")));
    }
    void invalidDescriptors_data() {
        QTest::addColumn<QJsonValue>("json");
        QTest::newRow("null") << QJsonValue();
        QTest::newRow("array") << QJsonValue(QJsonArray{});
        QTest::newRow("version-absent") << QJsonValue(QJsonObject{});
        auto version = descriptor(); version.insert(QStringLiteral("version"), QStringLiteral("2.0.0"));
        QTest::newRow("unknown-version") << QJsonValue(version);
        for (const auto &bad : QStringList{QStringLiteral("story:spoiler"), QStringLiteral("topic:<script>"), QStringLiteral("topic:Anime"),
                QStringLiteral("region:zz"), QStringLiteral("currency:usd"), QStringLiteral("topic:")})
            QTest::newRow(qPrintable(bad)) << QJsonValue(descriptor({bad}));
        QJsonArray excessive; for (int i = 0; i < 33; ++i) excessive.append(QStringLiteral("topic:anime"));
        QTest::newRow("count-bound") << QJsonValue(descriptor(excessive));
        QTest::newRow("length-bound") << QJsonValue(descriptor({QStringLiteral("topic:") + QString(80, QLatin1Char('a'))}));
        auto unknown = descriptor(); unknown.insert(QStringLiteral("code"), QStringLiteral("host.fetch('https://example.invalid')"));
        QTest::newRow("executable-field") << QJsonValue(unknown);
        auto wrong = descriptor(); wrong.insert(QStringLiteral("facets"), QStringLiteral("topic:anime"));
        QTest::newRow("wrong-type") << QJsonValue(wrong);
        wrong = descriptor(); wrong.insert(QStringLiteral("requirements"), QJsonArray{QStringLiteral("topic:games")});
        QTest::newRow("requirements-distinct") << QJsonValue(wrong);
        wrong = descriptor({QStringLiteral("requirement:login")});
        QTest::newRow("facets-distinct") << QJsonValue(wrong);
        wrong = descriptor(); wrong.insert(QStringLiteral("labels"), QJsonObject{{QStringLiteral("topic:absent"), QJsonObject{{QStringLiteral("en"), QStringLiteral("Absent")}}}});
        QTest::newRow("orphan-label") << QJsonValue(wrong);
    }
    void invalidDescriptors() {
        QFETCH(QJsonValue, json);
        auto pkg = package(json); QVERIFY(!pkg.isValid()); QVERIFY(!pkg.validationError().isEmpty());
        ProviderClassification preserved = ProviderClassification::builtin(QStringLiteral("cheapshark"));
        QString error; QVERIFY(!ProviderClassification::fromJson(json, &preserved, &error));
        QCOMPARE(preserved.facets, ProviderClassification::builtin(QStringLiteral("cheapshark")).facets);
    }
    void localizedLabelsAndSafeDiscovery() {
        auto json = descriptor({QStringLiteral("topic:custom")});
        json.insert(QStringLiteral("labels"), QJsonObject{{QStringLiteral("topic:custom"), QJsonObject{
            {QStringLiteral("en"), QStringLiteral("Custom")}, {QStringLiteral("fr"), QStringLiteral("Personnalisé")},
            {QStringLiteral("fr-CA"), QStringLiteral("Personnalisé au Canada")}}}});
        auto pkg = package(json); QVERIFY(pkg.isValid());
        const auto c = pkg.manifest().classification;
        QCOMPARE(c.label(QStringLiteral("topic:custom"), QLocale(QStringLiteral("fr_CA"))), QStringLiteral("Personnalisé au Canada"));
        QCOMPARE(c.label(QStringLiteral("topic:custom"), QLocale(QStringLiteral("fr_FR"))), QStringLiteral("Personnalisé"));
        QCOMPARE(c.label(QStringLiteral("topic:custom"), QLocale(QStringLiteral("ja_JP"))), QStringLiteral("Custom"));
        MainWindow window;
        int managers = 0;
        window.setNetworkAccessManagerFactory([&managers](QObject *parent) { ++managers; return new QNetworkAccessManager(parent); });
        window.loadPlugins({files.path()}, {});
        QVERIFY(!window.provider(QStringLiteral("custom"))); // unapproved script is never instantiated
        bool found = false;
        for (const auto &record : window.packages()) if (record.package.id() == QLatin1String("custom")) {
            found = true; QCOMPARE(record.package.manifest().classification.facets, c.facets);
        }
        QVERIFY(found); QCOMPARE(managers, 0);
    }
    void nativeScriptAndAdapterParity() {
        MetadataProvider native;
        const auto pkg = ProviderPackage::fromDirectory(QStringLiteral(PROJECT_SOURCE_DIR "/providers.d/cheapshark"));
        QVERIFY(pkg.isValid());
        QCOMPARE(native.classification().facets, pkg.manifest().classification.facets);
        QVERIFY(!native.classification().facets.contains(QStringLiteral("region:us")));
        ProviderApiV1::LegacyProviderV1Adapter v1(&native);
        QCOMPARE(v1.classification().facets, native.classification().facets);
        ProviderApiV1::ProviderV1ToLegacyAdapter back(&v1);
        QCOMPARE(back.classification().facets, native.classification().facets);
        SettingsDialog settings({&native});
        auto *list = settings.findChild<QListWidget *>(QStringLiteral("nativeProviders")); QVERIFY(list);
        list->setCurrentRow(0);
        auto *label = settings.findChild<QLabel *>(QStringLiteral("providerClassification")); QVERIFY(label);
        QVERIFY(label->text().contains(QStringLiteral("USD pricing")));
        QCOMPARE(native.searches, 0);
        // A source's topics must never be populated from a story's arbitrary tags.
        QVERIFY(!ProviderClassification::builtin(QStringLiteral("mangabaka")).facets.contains(QStringLiteral("topic:romance")));
    }
};
int main(int argc, char **argv) {
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME", root.path().toUtf8());
    qputenv("XDG_DATA_HOME", root.filePath(QStringLiteral("data")).toUtf8());
    qputenv("XDG_DATA_DIRS", root.filePath(QStringLiteral("system")).toUtf8());
    QApplication app(argc, argv); ClassificationTest test; return QTest::qExec(&test, argc, argv);
}
#include "test_providerclassification.moc"
