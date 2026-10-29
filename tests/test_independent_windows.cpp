// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QSignalSpy>
#include <QPushButton>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTextBrowser>

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
    }

    void cancelRequests() override {
        m_searchInProgress = false;
        m_cancelled = true;
    }

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
        auto *newWindowBtn = tab.findChild<QPushButton *>(QStringLiteral("openSearchInNewWindow"));
        QVERIFY(newWindowBtn != nullptr);

        QSignalSpy spy(&tab, &SearchTab::openSearchInNewWindowRequested);
        newWindowBtn->click();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("tab query"));
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
};

QTEST_MAIN(TestIndependentWindows)
#include "test_independent_windows.moc"
