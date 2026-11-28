// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/clipboardpolicy.h"
#include "../src/mainwindow.h"
#include "../src/providerclassification.h"
#include "../src/providerpackage.h"
#include "../src/recentactivity.h"
#include "../src/searchcontinuation.h"
#include "../src/searchprovider.h"
#include "../src/searchtab.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QtTest>

namespace
{

class DeferredContinuationProvider : public SearchProvider, public SearchProviderContinuation
{
    Q_OBJECT
    Q_INTERFACES(SearchProvider SearchProviderContinuation)
public:
    explicit DeferredContinuationProvider(const QString &id, QObject *parent = nullptr)
        : SearchProvider(parent)
        , m_id(id)
    {
    }
    QString name() const override { return m_id; }
    QString id() const override { return m_id; }
    void search(const QString &) override {}
    void requestPage(const QString &query, const QString &, quint64 requestId) override
    {
        lastQuery = query;
        lastRequestId = requestId;
    }
    void cancelRequests() override { canceled = true; }
    SearchProvider *clone(QObject *parent = nullptr) const override
    {
        auto *copy = new DeferredContinuationProvider(m_id, parent);
        copy->setEnabled(isEnabled());
        return copy;
    }
    void deliver(quint64 requestId, const QList<SearchResult> &results) { emit pageCompleted(requestId, {results, {}, {}}); }

    QString lastQuery;
    quint64 lastRequestId = 0;
    bool canceled = false;

private:
    QString m_id;
};

class MockExpandableProvider : public SearchProvider
{
    Q_OBJECT
public:
    MockExpandableProvider(const QString &id, const QString &name, const ProviderClassification &cls, QObject *parent = nullptr)
        : SearchProvider(parent)
        , m_id(id)
        , m_name(name)
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

} // namespace

class AutomaticResultsTabTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void cleanup()
    {
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("General"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("RecentActivity"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->sync();
    }

    void nonInteractionEventsDoNotPromoteAndStaleResponsesAreDropped()
    {
        QTabWidget tabs;
        tabs.addTab(new QWidget, QStringLiteral("Manual"));
        auto *tab = new SearchTab(QStringLiteral("first auto query"));
        tab->setAutomaticResultsMode(true);
        QSignalSpy promotedSpy(tab, &SearchTab::promotedFromAutomatic);

        auto *providerA = new DeferredContinuationProvider(QStringLiteral("mock-a"));
        tab->startProvider(providerA);
        const quint64 firstReq = providerA->lastRequestId;
        QCOMPARE(providerA->lastQuery, QStringLiteral("first auto query"));

        const int autoIdx = tabs.addTab(tab, tab->query());
        tabs.show();
        tabs.setCurrentIndex(autoIdx);
        tab->setFocus();
        tabs.setCurrentIndex(0);
        QCoreApplication::processEvents();
        QVERIFY(!tab->isPromotedFromAutomatic());
        QCOMPARE(promotedSpy.count(), 0);

        providerA->deliver(firstReq, {{QStringLiteral("r1"), QStringLiteral("Old Result"), QStringLiteral("old"), {}}});
        QCOMPARE(promotedSpy.count(), 0);

        QPointer<DeferredContinuationProvider> oldProvider(providerA);
        tab->resetForAutomaticQuery(QStringLiteral("second auto query"));
        QVERIFY(oldProvider->canceled);
        const quint64 genBefore = tab->queryGeneration();
        oldProvider->deliver(firstReq, {{QStringLiteral("stale"), QStringLiteral("Stale Result"), {}, {}}});
        QCoreApplication::processEvents();

        auto *tree = tab->findChild<QTreeWidget *>(QStringLiteral("resultsTree"));
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 0);

        auto *providerB = new DeferredContinuationProvider(QStringLiteral("mock-b"));
        tab->startProvider(providerB);
        providerB->deliver(providerB->lastRequestId, {{QStringLiteral("r2"), QStringLiteral("Fresh Result"), QStringLiteral("fresh"), {}}});
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tab->queryGeneration(), genBefore);
        QCOMPARE(promotedSpy.count(), 0);

        tabs.setCurrentIndex(autoIdx);
        QCoreApplication::processEvents();
        QVERIFY(!tab->isPromotedFromAutomatic());
        auto *pin = tab->findChild<QPushButton *>(QStringLiteral("pinAutoTabButton"));
        QVERIFY(pin && !pin->isHidden());
        pin->click();
        QVERIFY(tab->isPromotedFromAutomatic());
        QVERIFY(pin->isHidden());
        QCOMPARE(promotedSpy.count(), 1);
    }

    void clickAndScrollPromoteAndKeepEmptyExpansionControls()
    {
        SearchTab clickTab(QStringLiteral("click test"));
        clickTab.setAutomaticResultsMode(true);
        clickTab.setSourceChoices({{QStringLiteral("mock-click"), QStringLiteral("Mock"), QStringLiteral("Ready"), true, true, {}}});
        QVERIFY(clickTab.findChild<QPushButton *>(QStringLiteral("applySourceSelection"))->isVisible() ||
                clickTab.findChild<QWidget *>(QStringLiteral("sourceSelection")));

        auto *provider = new DeferredContinuationProvider(QStringLiteral("mock-click"));
        clickTab.startProvider(provider);
        provider->deliver(provider->lastRequestId, {{QStringLiteral("r1"), QStringLiteral("Alpha Item"), QStringLiteral("desc"), {}}});
        auto *tree = clickTab.findChild<QTreeWidget *>(QStringLiteral("resultsTree"));
        QVERIFY(tree && tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() >= 1);
        QVERIFY(!clickTab.isPromotedFromAutomatic());

        emit tree->itemClicked(tree->topLevelItem(0)->child(0), 0);
        QVERIFY(clickTab.isPromotedFromAutomatic());

        SearchTab scrollTab(QStringLiteral("scroll test"));
        scrollTab.setAutomaticResultsMode(true);
        auto *scrollTree = scrollTab.findChild<QTreeWidget *>(QStringLiteral("resultsTree"));
        QVERIFY(scrollTree);
        QWheelEvent wheel(QPointF(5, 5), QPointF(5, 5), QPoint(0, -10), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(scrollTree->viewport(), &wheel);
        QVERIFY(scrollTab.isPromotedFromAutomatic());
    }

    void mainWindowReusesBackgroundTabPromotesAndControlsHistory()
    {
        using namespace ClipboardPolicy;
        MainWindow window;
        KConfigGroup gen(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("General"));
        Policy policy;
        policy.autoHistoryMode = AutoHistoryMode::OnInteraction;
        write(gen, policy);
        gen.sync();

        auto *tabs = window.findChild<QTabWidget *>();
        auto *recent = window.findChild<RecentActivity *>();
        QVERIFY(tabs && recent);
        recent->clear();

        window.performSearch(QStringLiteral("user work tab"));
        const int userTabIdx = tabs->currentIndex();
        QCOMPARE(tabs->count(), 2);

        window.processClipboardText(QStringLiteral("auto first"), true);
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->currentIndex(), userTabIdx);
        QVERIFY(window.reusableAutoTab());
        QCOMPARE(window.reusableAutoTab()->query(), QStringLiteral("auto first"));
        QCOMPARE(recent->items().size(), 1);

        window.processClipboardText(QStringLiteral("auto second"), true);
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->currentIndex(), userTabIdx);
        QCOMPARE(window.reusableAutoTab()->query(), QStringLiteral("auto second"));
        QCOMPARE(recent->items().size(), 1);

        SearchTab *promoted = window.reusableAutoTab();
        auto *pin = promoted->findChild<QPushButton *>(QStringLiteral("pinAutoTabButton"));
        QVERIFY(pin);
        pin->click();
        QVERIFY(!window.reusableAutoTab());
        QVERIFY(promoted->isPromotedFromAutomatic());
        QCOMPARE(recent->items().size(), 2);
        QCOMPARE(recent->items().first().query, QStringLiteral("auto second"));

        window.processClipboardText(QStringLiteral("auto third"), true);
        QCOMPARE(tabs->count(), 4);
        QVERIFY(window.reusableAutoTab());
        QCOMPARE(window.reusableAutoTab()->query(), QStringLiteral("auto third"));

        const int autoIdx = tabs->indexOf(window.reusableAutoTab());
        QVERIFY(autoIdx > 0);
        tabs->tabCloseRequested(autoIdx);
        QCoreApplication::processEvents();
        QVERIFY(!window.reusableAutoTab());
        QCOMPARE(tabs->count(), 3);
    }
};

class LocalVirtualTagAndExpansionTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

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

        const QueryScope defaultScope = window.automaticLookupScopeForPolicy(policy);
        for (SearchProvider *p : window.providers()) {
            if (!window.isVerifiedLocalProvider(p->id())) {
                QVERIFY2(!defaultScope.includes(p->id()), qPrintable(p->id()));
            }
        }

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

        tab.applySourceEligibility(requested);
        auto *booksProv = new MockExpandableProvider(QStringLiteral("books_mock"), QStringLiteral("Books Mock"), webCls);
        tab.startProvider(booksProv);
        QCOMPARE(localProv->searchCalls, 1);
        QCOMPARE(booksProv->searchCalls, 1);
        QCOMPARE(tree->topLevelItemCount(), 2);
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir configHome;
    if (!configHome.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QApplication app(argc, argv);

    AutomaticResultsTabTest autoTabTest;
    LocalVirtualTagAndExpansionTest localScopeTest;
    return QTest::qExec(&autoTabTest, argc, argv) | QTest::qExec(&localScopeTest, argc, argv);
}

#include "test_automatic_lookup_tab.moc"
