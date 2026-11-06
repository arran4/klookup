// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "isolatedguitest.h"
#include "providereligibility.h"
#include "mainwindow.h"
#include "searchtab.h"
#include "searchcontinuation.h"
#include "settingsdialog.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QAction>
#include <QCheckBox>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QToolButton>
#include <QTimer>
#include <cstring>
class EligibilityReply : public QNetworkReply {
public:
    EligibilityReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] { setFinished(true); emit readyRead(); emit finished(); });
    }
    void abort() override { setFinished(true); }
    qint64 bytesAvailable() const override { return 2 - offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override {
        const auto count = qMin<qint64>(max, 2 - offset); if (!count) return -1;
        std::memcpy(data, "[]" + offset, count); offset += count; return count;
    }
    qint64 offset = 0;
};
class EligibilityNetwork : public QNetworkAccessManager {
public:
    EligibilityNetwork(int *requests, QObject *parent) : QNetworkAccessManager(parent), requests(requests) {}
    int *requests;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override { ++*requests; return new EligibilityReply(request, this); }
};
class EligibilityProvider : public SearchProvider {
public:
    QString id() const override { return QStringLiteral("cheapshark"); }
    QString name() const override { return QStringLiteral("Fixture games"); }
    void search(const QString &) override { ++requests; }
    SearchProvider *clone(QObject *) const override { return nullptr; }
    int requests = 0;
};
class EligibilityContinuation : public SearchProvider, public SearchProviderContinuation {
    Q_OBJECT
    Q_INTERFACES(SearchProvider SearchProviderContinuation)
public:
    int calls = 0;
    QString id() const override { return QStringLiteral("continuation_fixture"); }
    QString name() const override { return id(); }
    void search(const QString &) override {}
    SearchProvider *clone(QObject *) const override { return nullptr; }
    void requestPage(const QString &, const QString &, quint64 request) override {
        ++calls; SearchPage page; page.nextToken = QStringLiteral("next"); emit pageCompleted(request, page);
    }
    void cancelRequests() override {}
};
class SearchEligibilityTest : public QObject {
    Q_OBJECT
private slots:
    void precedence_data() {
        QTest::addColumn<int>("provider"); QTest::addColumn<int>("games"); QTest::addColumn<int>("currency");
        QTest::addColumn<int>("temporary"); QTest::addColumn<bool>("expected");
        QTest::newRow("default-off") << -1 << -1 << -1 << -1 << false;
        QTest::newRow("group-enable") << -1 << 1 << -1 << -1 << true;
        QTest::newRow("group-disable") << -1 << 0 << -1 << -1 << false;
        QTest::newRow("group-conflict-deny") << -1 << 1 << 0 << -1 << false;
        QTest::newRow("explicit-on-wins") << 1 << 1 << 0 << -1 << true;
        QTest::newRow("explicit-off-wins") << 0 << 1 << 1 << -1 << false;
        QTest::newRow("temporary-default-opt-in") << -1 << -1 << -1 << 1 << true;
        QTest::newRow("temporary-group-opt-in") << -1 << 0 << -1 << 1 << true;
        QTest::newRow("temporary-cannot-override-user-off") << 0 << 1 << -1 << 1 << false;
        QTest::newRow("temporary-opt-out") << 1 << 1 << -1 << 0 << false;
    }
    void precedence() {
        QFETCH(int, provider); QFETCH(int, games); QFETCH(int, currency); QFETCH(int, temporary); QFETCH(bool, expected);
        auto optional = [](int value) { return value < 0 ? std::optional<bool>() : std::optional<bool>(bool(value)); };
        QMap<QString, bool> groups;
        if (games >= 0) groups.insert(QStringLiteral("topic:games"), bool(games));
        if (currency >= 0) groups.insert(QStringLiteral("currency:USD"), bool(currency));
        const auto decision = ProviderEligibility::evaluate(QStringLiteral("cheapshark"), ProviderClassification::builtin(QStringLiteral("cheapshark")), optional(provider), groups, optional(temporary));
        QCOMPARE(decision.enabled, expected); QVERIFY(!decision.reason.isEmpty());
    }
    void savedOffStopsFutureRequestsInExistingTabs() {
        KConfigGroup settings(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Providers"));
        settings.writeEntry(QStringLiteral("continuation_fixture_Enabled"), true); settings.sync();
        SearchTab tab(QStringLiteral("fixture"));
        tab.setSourceChoices({{QStringLiteral("continuation_fixture"), QStringLiteral("Fixture"), {}, true, true}});
        auto *provider = new EligibilityContinuation; tab.startProvider(provider);
        QCOMPARE(provider->calls, 1);
        settings.writeEntry(QStringLiteral("continuation_fixture_Enabled"), false); settings.sync();
        tab.loadMore(provider->id()); QCOMPARE(provider->calls, 1);
        // Even an explicit temporary inclusion cannot defeat the saved user-off choice.
        tab.applySourceEligibility({provider->id()}); tab.loadMore(provider->id()); QCOMPARE(provider->calls, 1);
    }
    void settingsGroupsOverridesResetAndNoDispatch() {
        auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        KConfigGroup providers(config, QStringLiteral("Providers")), groups(config, QStringLiteral("ProviderGroups"));
        providers.deleteGroup(); groups.deleteGroup(); config->sync();
        EligibilityProvider provider;
        SettingsDialog dialog({&provider});
        auto *policies = dialog.findChild<QListWidget *>(QStringLiteral("sourceGroupPolicies")); QVERIFY(policies);
        QListWidgetItem *games = nullptr, *currency = nullptr;
        for (int i = 0; i < policies->count(); ++i) {
            auto *item = policies->item(i);
            if (item->data(Qt::UserRole).toString() == QLatin1String("topic:games")) games = item;
            if (item->data(Qt::UserRole).toString() == QLatin1String("currency:USD")) currency = item;
        }
        QVERIFY(games && currency); QCOMPARE(games->checkState(), Qt::PartiallyChecked);
        auto *native = dialog.findChild<QListWidget *>(QStringLiteral("nativeProviders")); QVERIFY(native);
        games->setCheckState(Qt::Checked);
        QCOMPARE(native->item(0)->checkState(), Qt::Checked);
        QCOMPARE(provider.requests, 0);
        QVERIFY(QMetaObject::invokeMethod(&dialog, "saveSettings", Qt::DirectConnection));
        QVERIFY(!providers.hasKey(QStringLiteral("cheapshark_Enabled"))); QVERIFY(provider.isEnabled());
        native->item(0)->setCheckState(Qt::Unchecked);
        QVERIFY(QMetaObject::invokeMethod(&dialog, "saveSettings", Qt::DirectConnection));
        QVERIFY(providers.hasKey(QStringLiteral("cheapshark_Enabled"))); QVERIFY(!provider.isEnabled());
        currency->setCheckState(Qt::Unchecked);
        auto *effective = dialog.findChild<QListWidget *>(QStringLiteral("effectiveSourcePolicies")); QVERIFY(effective); effective->setCurrentRow(0);
        dialog.findChild<QPushButton *>(QStringLiteral("resetSourceOverride"))->click();
        QVERIFY(effective->item(0)->text().contains(QStringLiteral("conflicting enabled groups")));
        QVERIFY(QMetaObject::invokeMethod(&dialog, "saveSettings", Qt::DirectConnection));
        QVERIFY(!providers.hasKey(QStringLiteral("cheapshark_Enabled"))); QVERIFY(!provider.isEnabled());
        SettingsDialog restarted({&provider});
        auto *restored = restarted.findChild<QListWidget *>(QStringLiteral("sourceGroupPolicies")); QVERIFY(restored);
        bool persisted = false;
        for (int i = 0; i < restored->count(); ++i) if (restored->item(i)->data(Qt::UserRole).toString() == QLatin1String("currency:USD")) {
            persisted = true; QCOMPARE(restored->item(i)->checkState(), Qt::Unchecked);
        }
        QVERIFY(persisted); QCOMPARE(provider.requests, 0);
    }
    void temporaryChoicesNeedExplicitSearchAndRespectApprovalAndUserOff() {
        auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        KConfigGroup settings(config, QStringLiteral("Providers")), groups(config, QStringLiteral("ProviderGroups"));
        settings.deleteGroup(); groups.deleteGroup();
        for (const auto &id : {"tvmaze", "lyrics_ovh", "openlibrary", "anilist", "wikipedia", "github", "duckduckgo", "mangaupdates", "mangabaka", "mangadex", "spotify", "myanimelist", "omdb", "themoviedb"})
            settings.writeEntry(QString::fromLatin1(id) + QStringLiteral("_Enabled"), false);
        settings.sync();
        int requests = 0;
        MainWindow window;
        window.setNetworkAccessManagerFactory([&requests](QObject *parent) { return new EligibilityNetwork(&requests, parent); });
        const QStringList paths{QStringLiteral(PROJECT_SOURCE_DIR "/providers.d")};
        window.loadPlugins(paths, {});
        window.setPackageTrusted(QStringLiteral("cheapshark"), false); window.loadPlugins(paths, {});
        window.performSearch(QStringLiteral("unapproved"));
        auto *tabs = qobject_cast<QTabWidget *>(window.centralWidget()); QVERIFY(tabs);
        auto *unapproved = qobject_cast<SearchTab *>(tabs->currentWidget()); QVERIFY(unapproved);
        auto *denied = unapproved->findChild<QAction *>(QStringLiteral("sourceChoice-cheapshark")); QVERIFY(denied && !denied->isEnabled());
        denied->setChecked(true); unapproved->findChild<QPushButton *>(QStringLiteral("applySourceSelection"))->click();
        QCOMPARE(requests, 0);
        window.setPackageTrusted(QStringLiteral("cheapshark"), true); window.loadPlugins(paths, {});
        window.performSearch(QStringLiteral("temporary"));
        auto *tab = qobject_cast<SearchTab *>(tabs->currentWidget()); QVERIFY(tab);
        auto *choice = tab->findChild<QAction *>(QStringLiteral("sourceChoice-cheapshark")); QVERIFY(choice && choice->isEnabled());
        QVERIFY(!choice->isChecked()); choice->setChecked(true);
        QCoreApplication::processEvents(); QCOMPARE(requests, 0); // Toggling is never a search.
        tab->findChild<QPushButton *>(QStringLiteral("applySourceSelection"))->click(); QTRY_COMPARE(requests, 2);
        QVERIFY(tab->hasProvider(QStringLiteral("cheapshark")));
        QVERIFY(!settings.hasKey(QStringLiteral("cheapshark_Enabled"))); // Temporary stays local.
        window.performSearch(QStringLiteral("another tab"));
        auto *other = qobject_cast<SearchTab *>(tabs->currentWidget()); QVERIFY(other);
        QVERIFY(!other->findChild<QAction *>(QStringLiteral("sourceChoice-cheapshark"))->isChecked());
        QCOMPARE(requests, 2);
        settings.writeEntry(QStringLiteral("cheapshark_Enabled"), false); settings.sync();
        choice->setChecked(false); choice->setChecked(true);
        tab->findChild<QPushButton *>(QStringLiteral("applySourceSelection"))->click();
        QCoreApplication::processEvents(); QCOMPARE(requests, 2); // Current explicit off wins over a stale tab's choice.
        window.performSearch(QStringLiteral("explicit off"));
        auto *off = qobject_cast<SearchTab *>(tabs->currentWidget()); QVERIFY(off);
        QVERIFY(!off->findChild<QAction *>(QStringLiteral("sourceChoice-cheapshark"))->isEnabled());
    }
};
int main(int argc, char **argv) { return runIsolatedGuiTest<SearchEligibilityTest>(argc, argv); }
#include "test_searcheligibility.moc"
