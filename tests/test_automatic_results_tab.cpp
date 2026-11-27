// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/clipboardpolicy.h"
#include "../src/mainwindow.h"
#include "../src/recentactivity.h"
#include "../src/searchcontinuation.h"
#include "../src/searchtab.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QtTest>

class DeferredContinuationProvider : public SearchProvider, public SearchProviderContinuation
{
    Q_OBJECT
    Q_INTERFACES(SearchProvider SearchProviderContinuation)
public:
    explicit DeferredContinuationProvider(const QString &id, QObject *parent = nullptr)
        : SearchProvider(parent), m_id(id) {}
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
    void deliver(quint64 requestId, const QList<SearchResult> &results)
    {
        emit pageCompleted(requestId, {results, {}, {}});
    }

    QString lastQuery;
    quint64 lastRequestId = 0;
    bool canceled = false;

private:
    QString m_id;
};

class AutomaticResultsTabTest : public QObject
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

        // Switching to/from tabs, showing, or focusing NEVER promotes an auto tab.
        const int autoIdx = tabs.addTab(tab, tab->query());
        tabs.show();
        tabs.setCurrentIndex(autoIdx);
        tab->setFocus();
        tabs.setCurrentIndex(0);
        QCoreApplication::processEvents();
        QVERIFY(!tab->isPromotedFromAutomatic());
        QCOMPARE(promotedSpy.count(), 0);

        // Rendering initial results does not promote either.
        providerA->deliver(firstReq, {{QStringLiteral("r1"), QStringLiteral("Old Result"), QStringLiteral("old"), {}}});
        QCOMPARE(promotedSpy.count(), 0);

        // Resetting for next automatic query cancels old providers and drops stale responses.
        QPointer<DeferredContinuationProvider> oldProvider(providerA);
        tab->resetForAutomaticQuery(QStringLiteral("second auto query"));
        QVERIFY(oldProvider->canceled);
        const quint64 genBefore = tab->queryGeneration();
        oldProvider->deliver(firstReq, {{QStringLiteral("stale"), QStringLiteral("Stale Result"), {}, {}}});
        QCoreApplication::processEvents();

        auto *tree = tab->findChild<QTreeWidget *>(QStringLiteral("resultsTree"));
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 0); // stale response was ignored

        auto *providerB = new DeferredContinuationProvider(QStringLiteral("mock-b"));
        tab->startProvider(providerB);
        providerB->deliver(providerB->lastRequestId,
                           {{QStringLiteral("r2"), QStringLiteral("Fresh Result"), QStringLiteral("fresh"), {}}});
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tab->queryGeneration(), genBefore);
        QCOMPARE(promotedSpy.count(), 0);

        // Switching back to view the auto tab still does not promote; clicking Pin promotes once.
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

        // Clicking an item promotes the tab.
        emit tree->itemClicked(tree->topLevelItem(0)->child(0), 0);
        QVERIFY(clickTab.isPromotedFromAutomatic());

        // Wheel scroll promotes as well.
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

        // Background automatic updates reuse a single tab without stealing active tab focus.
        window.processClipboardText(QStringLiteral("auto first"), true);
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->currentIndex(), userTabIdx);
        QVERIFY(window.reusableAutoTab());
        QCOMPARE(window.reusableAutoTab()->query(), QStringLiteral("auto first"));
        // Default OnInteraction does NOT record history until promoted.
        QCOMPARE(recent->items().size(), 1); // only "user work tab"

        window.processClipboardText(QStringLiteral("auto second"), true);
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->currentIndex(), userTabIdx);
        QCOMPARE(window.reusableAutoTab()->query(), QStringLiteral("auto second"));
        QCOMPARE(recent->items().size(), 1);

        // Pinning promotes and records history cleanly; next auto query allocates a new auto tab.
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

        // Closing the reusable automatic tab clears the pointer cleanly.
        const int autoIdx = tabs->indexOf(window.reusableAutoTab());
        QVERIFY(autoIdx > 0);
        tabs->tabCloseRequested(autoIdx);
        QCoreApplication::processEvents();
        QVERIFY(!window.reusableAutoTab());
        QCOMPARE(tabs->count(), 3);
    }
};

QTEST_MAIN(AutomaticResultsTabTest)
#include "test_automatic_results_tab.moc"
