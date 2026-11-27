// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/clipboardpolicy.h"
#include "../src/mainwindow.h"
#include "../src/providerclassification.h"
#include "../src/providerpackage.h"
#include "../src/searchprovider.h"
#include "../src/searchtab.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

class MockExpandableProvider : public SearchProvider
{
    Q_OBJECT
public:
    MockExpandableProvider(const QString &id, const QString &name, const ProviderClassification &cls, QObject *parent = nullptr)
        : SearchProvider(parent), m_id(id), m_name(name)
    {
        setClassification(cls);
    }
    QString name() const override { return m_name; }
    QString id() const override { return m_id; }
    void search(const QString &query) override
    {
        ++searchCalls;
        emit searchCompleted(m_id, {{m_id + QStringLiteral("-1"), m_name + QStringLiteral(": ") + query, {}, {}, {}}});
    }
    SearchProvider *clone(QObject *parent = nullptr) const override
    {
        auto *copy = new MockExpandableProvider(m_id, m_name, classification(), parent);
        copy->setEnabled(isEnabled());
        return copy;
    }

    int searchCalls = 0;

private:
    QString m_id;
    QString m_name;
};

class LocalVirtualTagAndExpansionTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_configHome;

private slots:
    void initTestCase()
    {
        QVERIFY(m_configHome.isValid());
        qputenv("XDG_CONFIG_HOME", m_configHome.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanup()
    {
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("General"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("Providers"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->sync();
    }

    void manifestCannotSpoofScopeLocalAndCustomLocalDocsIsDenied()
    {
        const QJsonObject spoofed = QJsonDocument::fromJson(
            "{\"version\":\"1.0.0\",\"facets\":[\"scope:local\",\"topic:books\"]}").object();
        ProviderClassification cls;
        QString err;
        QVERIFY(!ProviderClassification::fromJson(spoofed, &cls, &err));
        QVERIFY(!err.isEmpty());

        // Custom-path scripted provider claiming ID "localdocs" must NEVER get scope:local.
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString fakePkgDir = root.filePath(QStringLiteral("localdocs"));
        QVERIFY(QDir().mkpath(fakePkgDir));
        QFile manifest(fakePkgDir + QStringLiteral("/provider.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write("{\"id\":\"localdocs\",\"name\":\"Fake Local\",\"version\":\"1.0.0\",\"runtime\":\"javascript\",\"entry\":\"main.mjs\"}");
        manifest.close();
        QFile entry(fakePkgDir + QStringLiteral("/main.mjs"));
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("function search() { return []; } module.exports = { search: search };");
        entry.close();

        MainWindow window;
        KConfigGroup pg(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Providers"));
        const auto pkg = ProviderPackage::fromDirectory(fakePkgDir, ProviderPackage::LocationType::Custom);
        QVERIFY(pkg.isValid());
        pkg.saveApproval(pg, true);

        window.loadPlugins({root.path()}, {});
        QVERIFY(!window.isVerifiedLocalProvider(QStringLiteral("localdocs")));
        if (auto *prov = window.provider(QStringLiteral("localdocs"))) {
            QVERIFY(!prov->classification().facets.contains(QString::fromLatin1(ProviderClassification::LocalVirtualFacet)));
        }
    }

    void defaultAutoLookupOnlyRunsVerifiedLocalAndNewOnlineRequiresAck()
    {
        using namespace ClipboardPolicy;
        MainWindow window;
        Policy policy;
        QCOMPARE(window.automaticLookupScopeForPolicy(policy).restricted, true);

        // Even with built-in plugins disabled/absent in test env, default scope must NOT include any online provider.
        const QueryScope defaultScope = window.automaticLookupScopeForPolicy(policy);
        for (SearchProvider *p : window.providers()) {
            if (!window.isVerifiedLocalProvider(p->id())) {
                QVERIFY2(!defaultScope.includes(p->id()), qPrintable(p->id()));
            }
        }

        // Opt-in auto-online snapshot: approved provider allowed, unacknowledged new provider blocked
        // even if its tag matches an allowed facet.
        Policy onlinePolicy;
        onlinePolicy.autoOnlineEnabled = true;
        onlinePolicy.autoOnlineAllowedFacets = {QStringLiteral("topic:manga")};
        onlinePolicy.autoAcknowledgedOnlineProviderIds = {QStringLiteral("known_manga")};

        QVERIFY(onlinePolicy.permitsAutoOnlineProvider(QStringLiteral("known_manga"), {QStringLiteral("topic:manga")}));
        QVERIFY(!onlinePolicy.permitsAutoOnlineProvider(QStringLiteral("brand_new_manga"), {QStringLiteral("topic:manga")}));
    }

    void additiveExpandToMenuRetainsLoadedResultsAndDispatchesOnlyAdded()
    {
        SearchTab tab(QStringLiteral("expansion query"));
        tab.setAutomaticResultsMode(true);

        ProviderClassification localCls;
        localCls.addHostVirtualFacet(QString::fromLatin1(ProviderClassification::LocalVirtualFacet));
        auto *localProv = new MockExpandableProvider(QStringLiteral("local_mock"), QStringLiteral("Local Mock"), localCls);

        ProviderClassification webCls;
        webCls.facets = {QStringLiteral("topic:books")};
        tab.setSourceChoices({
            {QStringLiteral("local_mock"), QStringLiteral("Local Mock"), QStringLiteral("Ready"), true, true, localCls},
            {QStringLiteral("books_mock"), QStringLiteral("Books Mock"), QStringLiteral("Ready"), true, true, webCls},
        });
        tab.applySourceEligibility({QStringLiteral("local_mock")});
        tab.startProvider(localProv);
        QCOMPARE(localProv->searchCalls, 1);

        auto *tree = tab.findChild<QTreeWidget *>(QStringLiteral("resultsTree"));
        QVERIFY(tree && tree->topLevelItemCount() == 1);

        QSignalSpy expandSpy(&tab, &SearchTab::searchSelectedSourcesRequested);
        auto *expandMenuBtn = tab.findChild<QToolButton *>(QStringLiteral("expandScopeMenuButton"));
        QVERIFY(expandMenuBtn && !expandMenuBtn->isHidden() && expandMenuBtn->menu());

        auto *expandBooksAction = expandMenuBtn->menu()->findChild<QAction *>(QStringLiteral("expandScopeFacet-topic:books"));
        QVERIFY(expandBooksAction);
        expandBooksAction->trigger();

        QCOMPARE(expandSpy.count(), 1);
        const QSet<QString> requested = expandSpy.first().first().value<QSet<QString>>();
        QVERIFY(requested.contains(QStringLiteral("local_mock")));
        QVERIFY(requested.contains(QStringLiteral("books_mock")));
        QVERIFY(tab.isPromotedFromAutomatic());

        // Simulate MainWindow::searchSelectedSources: already-queried local_mock is kept,
        // and only books_mock is started!
        tab.applySourceEligibility(requested);
        auto *booksProv = new MockExpandableProvider(QStringLiteral("books_mock"), QStringLiteral("Books Mock"), webCls);
        tab.startProvider(booksProv);
        QCOMPARE(localProv->searchCalls, 1); // existing provider was NOT re-queried
        QCOMPARE(booksProv->searchCalls, 1);
        QCOMPARE(tree->topLevelItemCount(), 2); // retained local + added books
    }
};

QTEST_MAIN(LocalVirtualTagAndExpansionTest)
#include "test_local_virtual_tag_and_expansion.moc"
