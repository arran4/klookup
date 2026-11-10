#include "isolatedguitest.h"
// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QSignalSpy>
#include <QPushButton>
#include <QAction>
#include <QScrollBar>
#include <QTabWidget>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QSplitter>
#include <QMouseEvent>
#include <KConfigGroup>
#include <KSharedConfig>

#include "mainwindow.h"
#include "searchdashboard.h"
#include "searchtab.h"
#include "searchwindow.h"
#include "resultdetailwindow.h"
#include "searchcontinuation.h"

class MockContinuationProvider : public SearchProvider, public SearchProviderContinuation
{
    Q_OBJECT
    Q_INTERFACES(SearchProvider SearchProviderContinuation)
public:
    explicit MockContinuationProvider(QObject *parent = nullptr) : SearchProvider(parent) {}

    QString id() const override { return QStringLiteral("mock_continuation"); }
    QString name() const override { return QStringLiteral("Mock Continuation"); }

    SearchProvider *clone(QObject *parent = nullptr) const override {
        return new MockContinuationProvider(parent);
    }

    void search(const QString &query) override {
        Q_UNUSED(query);
        m_searchInProgress = true;
    }

    void requestPage(const QString &query, const QString &token, quint64 requestId) override {
        Q_UNUSED(query);
        Q_UNUSED(token);
        Q_UNUSED(requestId);
        m_searchInProgress = true;
        ++m_pageRequests;
    }

    void cancelRequests() override {
        m_searchInProgress = false;
        m_cancelled = true;
    }

    int m_pageRequests = 0;
    bool m_searchInProgress = false;
    bool m_cancelled = false;
};

class TestIndependentWindows : public QObject
{
    Q_OBJECT

private slots:
    void testOpenSearchInNewWindow()
    {
        MainWindow mw;
        QCOMPARE(mw.searchWindows().size(), 0);

        SearchWindow *win1 = mw.openSearchInNewWindow(QStringLiteral("first query"));
        QVERIFY(win1 != nullptr);
        QCOMPARE(win1->query(), QStringLiteral("first query"));
        QCOMPARE(mw.searchWindows().size(), 1);

        SearchWindow *win2 = mw.openSearchInNewWindow(QStringLiteral("second query"));
        QVERIFY(win2 != nullptr);
        QCOMPARE(win2->query(), QStringLiteral("second query"));
        QCOMPARE(mw.searchWindows().size(), 2);

        // Closing win1 should not affect win2 or mw
        win1->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(mw.searchWindows().size(), 1);
        QCOMPARE(mw.searchWindows().first(), win2);

        win2->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(mw.searchWindows().size(), 0);
    }

    void testDetachedResultDetailWindow()
    {
        MainWindow mw;
        QCOMPARE(mw.detailWindows().size(), 0);

        SearchResult res;
        res.id = QStringLiteral("item-42");
        res.title = QStringLiteral("The Answer");
        res.type = QStringLiteral("Philosophy");
        res.description = QStringLiteral("42 is the meaning of life.");
        res.url = QStringLiteral("https://example.org/42");

        ResultMetadata meta;
        MetadataNode sec;
        sec.label = QStringLiteral("General Info");
        sec.values.append({QStringLiteral("Universal"), QUrl(), MetadataValue::Kind::Text});
        meta.sections.append(sec);

        ResultDetailWindow *detail = mw.openResultDetailsInNewWindow(res, QStringLiteral("MockProvider"), meta, QStringLiteral("<b>Rich 42</b>"));
        QVERIFY(detail != nullptr);
        QCOMPARE(mw.detailWindows().size(), 1);
        QCOMPARE(detail->result().id, QStringLiteral("item-42"));
        QCOMPARE(detail->result().title, QStringLiteral("The Answer"));
        QCOMPARE(detail->providerName(), QStringLiteral("MockProvider"));
        QCOMPARE(detail->metadata().sections.size(), 1);

        // Detail window has its own copy of data and can close cleanly
        detail->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(mw.detailWindows().size(), 0);
    }

    void testSearchTabDetachedDetailsPreservesDataWhenTabClosed()
    {
        auto *tab = new SearchTab(QStringLiteral("test query"));
        SearchResult res;
        res.id = QStringLiteral("test-1");
        res.title = QStringLiteral("Test Result");
        res.description = QStringLiteral("Test description");
        res.url = QStringLiteral("https://example.org/test");
        tab->addResults(QStringLiteral("MockProvider"), {res});

        auto *tree = tab->findChild<QTreeWidget *>();
        QVERIFY(tree != nullptr);
        auto *group = tree->topLevelItem(0);
        QVERIFY(group != nullptr && group->childCount() > 0);
        auto *item = group->child(0);

        QSignalSpy spy(tab, &SearchTab::detailWindowCreated);
        ResultDetailWindow *detail = tab->openResultDetailsInNewWindow(item);
        QVERIFY(detail != nullptr);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().value<ResultDetailWindow *>(), detail);

        // Deleting the tab must not crash or invalidate detail window
        delete tab;
        tab = nullptr;

        QCOMPARE(detail->result().id, QStringLiteral("test-1"));
        QCOMPARE(detail->result().title, QStringLiteral("Test Result"));

        detail->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void testClosingDuringActiveRequests()
    {
        auto *mock = new MockContinuationProvider();
        QList<SearchProvider *> providers = {mock};

        auto *win = new SearchWindow(QStringLiteral("async query"), providers);
        win->show();
        QVERIFY(win->searchTab() != nullptr);

        // Close window while provider is active
        win->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        delete mock;
    }

    void testDashboardSearchInNewWindowButton()
    {
        SearchDashboard dashboard;
        auto *lineEdit = dashboard.findChild<QLineEdit *>();
        auto *newWindowBtn = dashboard.findChild<QPushButton *>(QStringLiteral("searchNewWindow"));
        QVERIFY(lineEdit != nullptr);
        QVERIFY(newWindowBtn != nullptr);

        QSignalSpy spy(&dashboard, &SearchDashboard::searchInNewWindowRequested);
        lineEdit->setText(QStringLiteral("open in window"));
        newWindowBtn->click();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("open in window"));
    }

    void testSearchTabOpenInNewWindowButton()
    {
        SearchTab tab(QStringLiteral("tab query"));
        auto *newWindowBtn = tab.findChild<QPushButton *>(QStringLiteral("popOutSearch"));
        QVERIFY(newWindowBtn != nullptr);

        QSignalSpy spy(&tab, &SearchTab::popOutRequested);
        newWindowBtn->click();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(newWindowBtn->text(), QStringLiteral("Pop Out to New Window"));
    }

    void testPopOutReturnAndPendingOwnership()
    {
        MainWindow main;
        main.performSearch(QStringLiteral("first")); main.performSearch(QStringLiteral("second"));
        auto *tabs = qobject_cast<QTabWidget *>(main.centralWidget()); QVERIFY(tabs);
        QCOMPARE(tabs->count(), 3);
        auto *tab = qobject_cast<SearchTab *>(tabs->widget(1)); QVERIFY(tab);
        auto *provider = new MockContinuationProvider(tab); tab->startProvider(provider);
        QVERIFY(provider->m_searchInProgress);
        auto *tree = tab->findChild<QTreeWidget *>(QStringLiteral("resultsTree")); QVERIFY(tree);
        SearchResult result; result.id = QStringLiteral("loaded"); result.title = QStringLiteral("Loaded detail");
        tab->addResults(QStringLiteral("Fixture"), {result});
        auto *item = tree->topLevelItem(tree->topLevelItemCount() - 1)->child(0);
        tree->setCurrentItem(item);
        auto *browser = tab->findChild<QTextBrowser *>(); const auto content = browser->toPlainText();
        auto *window = main.popOutSearchTab(tab); QVERIFY(window);
        QCOMPARE(tabs->count(), 2); QCOMPARE(tabs->tabText(1), QStringLiteral("second"));
        QCOMPARE(window->searchTab(), tab); QCOMPARE(provider->parent(), tab);
        QVERIFY(provider->m_searchInProgress && !provider->m_cancelled);
        QCOMPARE(provider->m_pageRequests, 1);
        QCOMPARE(tree->currentItem(), item); QCOMPARE(browser->toPlainText(), content);
        QVERIFY(!tab->findChild<QPushButton *>(QStringLiteral("popOutSearch"))->isVisible());
        // Results arriving while popped out target the same live tab/provider state.
        SearchPage page; SearchResult late; late.id = QStringLiteral("late"); late.title = QStringLiteral("Late result"); page.results = {late};
        // This mock uses legacy completion rather than a tracked page request.
        emit provider->searchCompleted(provider->id(), {late});
        QVERIFY(tree->topLevelItem(0)->childCount() > 0);
        auto *action = window->findChild<QAction *>(QStringLiteral("returnSearchToMainWindow")); QVERIFY(action);
        QPointer<SearchWindow> closing(window); action->trigger();
        QCOMPARE(tabs->count(), 3); QCOMPARE(tabs->currentWidget(), tab);
        QCOMPARE(tree->currentItem(), item); QCOMPARE(provider->parent(), tab);
        QVERIFY(!provider->m_cancelled);
        QCOMPARE(provider->m_pageRequests, 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(!closing);
        // Closing a popped-out window permanently closes that search, leaving the other tab.
        window = main.popOutSearchTab(tab); QVERIFY(window);
        QPointer<SearchTab> deleted(tab); QPointer<MockContinuationProvider> cancelled(provider);
        window->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!deleted && !cancelled); QCOMPARE(tabs->count(), 2);
        QVERIFY(!main.popOutSearchTab(nullptr));
        QVERIFY(!main.returnSearchToMain(nullptr));
    }

    void testSearchTabOpenDetailsButton()
    {
        SearchTab tab(QStringLiteral("query"));
        tab.resize(800, 600);
        tab.show();

        SearchResult res;
        res.id = QStringLiteral("res-id");
        res.title = QStringLiteral("Result Title");
        tab.addResults(QStringLiteral("Provider"), {res});

        auto *detailsBtn = tab.findChild<QPushButton *>(QStringLiteral("openDetailsInNewWindowButton"));
        QVERIFY(detailsBtn != nullptr);
        QVERIFY(!detailsBtn->isVisible());

        auto *tree = tab.findChild<QTreeWidget *>();
        auto *group = tree->topLevelItem(0);
        tree->setCurrentItem(group->child(0));

        QVERIFY(detailsBtn->isVisible());

        QSignalSpy spy(&tab, &SearchTab::detailWindowCreated);
        detailsBtn->click();

        QCOMPARE(spy.count(), 1);
        ResultDetailWindow *win = spy.first().first().value<ResultDetailWindow *>();
        QVERIFY(win != nullptr);
        QCOMPARE(win->result().id, QStringLiteral("res-id"));

        win->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void testSearchWindowResizablePanesAndPersistence()
    {
        KConfigGroup layoutGroup(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
        layoutGroup.writeEntry(QStringLiteral("SearchTabSplitter"), QList<int>{310, 490});
        layoutGroup.sync();

        MainWindow mw;
        SearchWindow *win = mw.openSearchInNewWindow(QStringLiteral("window query"));
        QVERIFY(win != nullptr);
        win->resize(800, 600);
        win->show();

        auto *tab = win->searchTab();
        QVERIFY(tab != nullptr);
        auto *splitter = tab->mainSplitter();
        QVERIFY(splitter != nullptr);

        QVERIFY(qAbs(splitter->sizes()[0] - 310) <= 5);
        QVERIFY(qAbs(splitter->sizes()[1] - 490) <= 5);

        splitter->setSizes({360, 440});
        emit tab->mainSplitter()->splitterMoved(360, 1);

        SearchTab mainTab(QStringLiteral("new tab"));
        mainTab.resize(800, 600);
        mainTab.show();
        QCOMPARE(mainTab.mainSplitter()->sizes(), splitter->sizes());

        win->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void testStaleDetailWindowDoesNotOverwriteNewerLayout()
    {
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
        SearchResult result; result.title = QStringLiteral("Fixture");
        ResultMetadata metadata;
        MetadataNode section; section.label = QStringLiteral("Facts");
        section.values.append({QStringLiteral("Fixture value"), QUrl(), MetadataValue::Kind::Text});
        metadata.sections.append(section);
        auto *old = new ResultDetailWindow(result, QStringLiteral("Fixture"), metadata);
        auto *current = new ResultDetailWindow(result, QStringLiteral("Fixture"), metadata);
        old->show(); current->show();
        QTest::keyClick(old->splitter()->handle(1), Qt::Key_Down);
        QTest::keyClick(current->splitter()->handle(1), Qt::Key_Up);
        const auto latest = group.readEntry(QStringLiteral("DetailWindowSplitter"), QList<int>());
        QVERIFY(!latest.isEmpty());
        old->resize(900, 900);
        old->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(group.readEntry(QStringLiteral("DetailWindowSplitter"), QList<int>()), latest);
        current->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(group.readEntry(QStringLiteral("DetailWindowSplitter"), QList<int>()), latest);
    }

    void testResultDetailWindowSplitterAndPersistence()
    {
        KConfigGroup layoutGroup(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
        layoutGroup.deleteGroup();
        layoutGroup.sync();

        MainWindow mw;
        SearchResult res;
        res.id = QStringLiteral("detail-id");
        res.title = QStringLiteral("Detail Window Title");
        res.description = QStringLiteral("Detailed synopsis for test.");

        ResultMetadata meta;
        MetadataNode node;
        node.label = QStringLiteral("Info");
        node.values.append({QStringLiteral("Value"), QUrl(), MetadataValue::Kind::Text});
        meta.sections.append(node);

        ResultDetailWindow *win1 = mw.openResultDetailsInNewWindow(res, QStringLiteral("MockProvider"), meta, QStringLiteral("<b>Content</b>"));
        QVERIFY(win1 != nullptr);
        win1->resize(650, 500);
        win1->show();

        auto *splitter = win1->splitter();
        QVERIFY(splitter != nullptr);
        QCOMPARE(splitter->orientation(), Qt::Vertical);

        splitter->setSizes({280, 220});
        emit splitter->splitterMoved(splitter->sizes()[0], 1);
        win1->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        const auto saved = layoutGroup.readEntry(QStringLiteral("DetailWindowSplitter"), QList<int>());
        QCOMPARE(saved.size(), 2);
        QVERIFY(saved[0] > 0);
        QVERIFY(saved[1] > 0);

        ResultDetailWindow *win2 = mw.openResultDetailsInNewWindow(res, QStringLiteral("MockProvider"), meta, QStringLiteral("<b>Content</b>"));
        QVERIFY(win2 != nullptr);
        win2->resize(650, 500);
        win2->show();

        QCOMPARE(win2->splitter()->sizes(), saved);

        auto *handle = win2->splitter()->handle(1);
        QVERIFY(handle != nullptr);
        QTest::mouseDClick(handle, Qt::LeftButton);

        const auto resetSizes = win2->splitter()->sizes();
        QCOMPARE(resetSizes.size(), 2);
        QVERIFY(resetSizes[0] > 0);
        QVERIFY(resetSizes[1] > 0);

        win2->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

int main(int argc, char **argv) { return runIsolatedGuiTest<TestIndependentWindows>(argc, argv); }
#include "test_independent_windows.moc"
