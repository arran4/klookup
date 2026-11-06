// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "isolatedguitest.h"
#include "displaypreferences.h"
#include "providerpackage.h"
#include "providerapiv1.h"
#include "searchtab.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QDialog>
#include <QMenu>
#include <QFile>
using namespace DisplayPreferences;
static QJsonArray fixture() {
    return QJsonDocument::fromJson(R"([
        {"version":"1.0.0","key":"org.klookup.fixture.spoilers","label":"Show spoilers","type":"boolean","default":false,"global":true},
        {"version":"1.0.0","key":"org.klookup.fixture.weights","label":"Tag weights","type":"ordered-list","choices":["core","defining","recurrent","incidental","unweighted"],"default":["core","defining"]},
        {"version":"1.0.0","key":"org.klookup.fixture.mode","label":"Mode","type":"enum","choices":["summary","complete"],"default":"summary"},
        {"version":"1.0.0","key":"org.klookup.fixture.limit","label":"Limit","type":"range","minimum":1,"maximum":20,"default":5,"requiresRefresh":true}
    ])").array();
}
static QList<Descriptor> specs() { QList<Descriptor> result; QString error; DisplayPreferences::descriptorsFromJson(fixture(), &result, &error); return result; }
class DisplayProvider : public SearchProvider {
    Q_OBJECT
public:
    DisplayProvider() { setDisplayOptions(specs()); }
    int requests = 0;
    QString id() const override { return QStringLiteral("display_fixture"); }
    QString name() const override { return id(); }
    void search(const QString &) override { ++requests; SearchResult result; result.id = QStringLiteral("one"); result.title = QStringLiteral("Fixture"); emit searchCompleted(id(), {result}); }
    SearchProvider *clone(QObject *) const override { return nullptr; }
};
class DisplayPreferencesTest : public QObject {
    Q_OBJECT
private slots:
    void init() {
        auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        for (const auto &group : config->groupList()) if (group.startsWith(QLatin1String("Display"))) config->deleteGroup(group);
        config->sync();
    }
    void precedenceIndependentViewsPersistenceResetAndSafeUndo() {
        const auto key = QStringLiteral("org.klookup.fixture.spoilers");
        Context a(QStringLiteral("a"), specs()), b(QStringLiteral("a"), specs()), c(QStringLiteral("b"), specs());
        QCOMPARE(a.effective(key).toBool(), false);
        QVERIFY(a.setTemporary(key, true)); QCOMPARE(b.effective(key).toBool(), false);
        QVERIFY(a.saveDefault(key, true)); QCOMPARE(c.effective(key).toBool(), true); QCOMPARE(a.origin(key), QStringLiteral("Global default"));
        QVERIFY(b.setTemporary(key, false)); QVERIFY(b.saveDefault(key, false)); QCOMPARE(a.effective(key).toBool(), false); QCOMPARE(c.effective(key).toBool(), true);
        Context restarted(QStringLiteral("a"), specs()); QCOMPARE(restarted.effective(key).toBool(), false);
        QVERIFY(a.setTemporary(key, true)); QVERIFY(a.saveDefault(key, true)); // Global promotion retains the selected view, without deleting the provider choice.
        QCOMPARE(a.effective(key).toBool(), true); QCOMPARE(b.effective(key).toBool(), false);
        a.resetTemporary(key); QCOMPARE(a.effective(key).toBool(), false);
        QVERIFY(a.setTemporary(key, true)); QVERIFY(a.saveDefault(key, false));
        QVERIFY(b.setTemporary(key, false)); QVERIFY(b.saveDefault(key, false));
        QVERIFY(!a.canUndo()); QVERIFY(!a.undo()); // Never overwrite a newer saved default from another view.
        QVERIFY(b.undo()); QCOMPARE(a.effective(key).toBool(), true);
        QVERIFY(a.clearDefault(key, false)); QCOMPARE(a.origin(key), QStringLiteral("Global default")); QVERIFY(a.undo());
        QVERIFY(!a.saveDefault(QStringLiteral("org.klookup.fixture.mode"), true)); // Provider-only options cannot be promoted globally.
    }
    void validationBoundsOrderedValuesAndUnavailableProvider() {
        Context context(QStringLiteral("missing-provider"), specs());
        QVERIFY(!context.setTemporary(QStringLiteral("org.klookup.fixture.spoilers"), QStringLiteral("true")));
        QVERIFY(!context.setTemporary(QStringLiteral("org.klookup.fixture.limit"), 21));
        QVERIFY(!context.setTemporary(QStringLiteral("org.klookup.fixture.limit"), 4.5));
        QVERIFY(!context.setTemporary(QStringLiteral("org.klookup.fixture.mode"), QStringLiteral("unknown")));
        const QStringList order{QStringLiteral("incidental"), QStringLiteral("core")};
        QVERIFY(context.setTemporary(QStringLiteral("org.klookup.fixture.weights"), order));
        QVERIFY(context.saveDefault(QStringLiteral("org.klookup.fixture.weights"), false));
        Context unavailable(QStringLiteral("missing-provider"), specs()); // No provider object or script execution is needed to restore options.
        QCOMPARE(unavailable.effective(QStringLiteral("org.klookup.fixture.weights")).toStringList(), order);
        QVERIFY(!context.setTemporary(QStringLiteral("org.klookup.fixture.weights"), QStringList{QStringLiteral("core"), QStringLiteral("core")}));
        for (const auto &badType : {QStringLiteral("string"), QStringLiteral("password"), QStringLiteral("code")}) {
            auto json = fixture(); auto object = json[0].toObject(); object.insert(QStringLiteral("type"), badType); json[0] = object;
            QList<Descriptor> result; QString error; QVERIFY(!descriptorsFromJson(json, &result, &error)); QVERIFY(!error.isEmpty());
        }
        auto json = fixture(); auto object = json[0].toObject(); object.insert(QStringLiteral("version"), QStringLiteral("2.0.0")); json[0] = object;
        QList<Descriptor> result; QString error; QVERIFY(!descriptorsFromJson(json, &result, &error));
        json = fixture(); json.append(json[0]); QVERIFY(!descriptorsFromJson(json, &result, &error));
        json = fixture(); object = json[0].toObject(); object.insert(QStringLiteral("execute"), QStringLiteral("host.fetch('https://example.invalid')")); json[0] = object; QVERIFY(!descriptorsFromJson(json, &result, &error));
    }
    void passiveManifestAndNativeAdapterParity() {
        QTemporaryDir root; QVERIFY(root.isValid());
        QFile manifest(root.filePath(QStringLiteral("provider.json"))); QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(QJsonDocument(QJsonObject{{QStringLiteral("id"), QStringLiteral("options_fixture")}, {QStringLiteral("name"), QStringLiteral("Options fixture")}, {QStringLiteral("displayOptions"), fixture()}}).toJson()); manifest.close();
        QFile script(root.filePath(QStringLiteral("main.mjs"))); QVERIFY(script.open(QIODevice::WriteOnly)); script.write("throw Error('Passive discovery must not execute');"); script.close();
        const auto package = ProviderPackage::fromDirectory(root.path()); QVERIFY2(package.isValid(), qPrintable(package.validationError()));
        DisplayProvider native;
        ProviderApiV1::LegacyProviderV1Adapter v1(&native); ProviderApiV1::ProviderV1ToLegacyAdapter back(&v1);
        QCOMPARE(package.manifest().displayOptions.size(), native.displayOptions().size());
        QCOMPARE(back.displayOptions()[0].key, package.manifest().displayOptions[0].key);
        QCOMPARE(back.displayOptions()[1].defaultValue.toStringList(), package.manifest().displayOptions[1].defaultValue.toStringList());
        QCOMPARE(native.requests, 0);
    }
    void contextualEditorDoesNotDispatchOrPersistUntilPromotion() {
        SearchTab tab(QStringLiteral("fixture")); auto *provider = new DisplayProvider; tab.startProvider(provider);
        auto *tree = tab.findChild<QTreeWidget *>(); auto *item = tree->topLevelItem(0)->child(0);
        std::unique_ptr<QMenu> menu(tab.createResultMenu(item)); auto *action = menu->findChild<QAction *>(QStringLiteral("viewDisplayOptions")); QVERIFY(action); action->trigger();
        auto *dialog = tab.findChild<QDialog *>(QStringLiteral("displayOptionsDialog")); QVERIFY(dialog);
        const auto key = QStringLiteral("org.klookup.fixture.spoilers");
        auto *box = dialog->findChild<QCheckBox *>(QStringLiteral("displayOption-") + key); QVERIFY(box); box->setChecked(true);
        QVERIFY(tab.displayContext(item)->effective(key).toBool());
        Context other(provider->id(), specs()); QCOMPARE(other.effective(key).toBool(), false);
        auto *range = dialog->findChild<QSpinBox *>(QStringLiteral("displayOption-org.klookup.fixture.limit")); QVERIFY(range); range->setValue(10);
        QCOMPARE(provider->requests, 1); // Changing any presentation control, including requiresRefresh, starts no new provider work.
        dialog->findChild<QPushButton *>(QStringLiteral("saveProvider-") + key)->click(); QCOMPARE(other.effective(key).toBool(), true);
        dialog->findChild<QPushButton *>(QStringLiteral("undoDisplayDefault"))->click(); QCOMPARE(other.effective(key).toBool(), false);
        QCOMPARE(provider->requests, 1);
        dialog->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};
int main(int argc, char **argv) { return runIsolatedGuiTest<DisplayPreferencesTest>(argc, argv); }
#include "test_displaypreferences.moc"
