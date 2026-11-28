// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "isolatedguitest.h"
#include "mainwindow.h"
#include "searchcontinuation.h"
#include "searchdashboard.h"
#include "searchprovider.h"
#include "searchtab.h"
#include "searchwindow.h"
#include "settingsdialog.h"
#include "testhelpers.h"

#include <KColorScheme>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>

namespace
{

class FilterProvider final : public SearchProvider, public SearchProviderContinuation
{
    Q_OBJECT
    Q_INTERFACES(SearchProvider SearchProviderContinuation)
public:
    FilterProvider(const QString &id, QStringList tags, TestCountingNetworkManager *network)
        : sourceId(id)
        , tags(tags)
        , network(network)
    {
        ProviderClassification classification;
        classification.facets = tags;
        setClassification(classification);
    }
    QString sourceId;
    QStringList tags;
    TestCountingNetworkManager *network;
    quint64 pending = 0;

    QString id() const override { return sourceId; }
    QString name() const override { return sourceId; }
    SearchProvider *clone(QObject *) const override { return nullptr; }
    void search(const QString &) override {}
    void requestPage(const QString &, const QString &, quint64 id) override
    {
        network->get(QNetworkRequest(QUrl(QStringLiteral("https://example.invalid/page"))));
        pending = id;
    }
    void finish(const QString &id)
    {
        SearchResult result;
        result.id = id;
        result.title = id;
        SearchPage page;
        page.results = {result};
        page.nextToken = QStringLiteral("next");
        emit pageCompleted(pending, page);
    }
    bool supportsDetails() const override { return true; }
    void requestDetails(const QString &id, const QString &, quint64 request) override
    {
        network->get(QNetworkRequest(QUrl(QStringLiteral("https://example.invalid/details"))));
        SearchDetails details;
        details.result.id = id;
        details.result.title = id;
        details.result.description = QStringLiteral("Cached detail");
        emit detailsCompleted(request, details);
    }
    void cancelRequests() override {}
};

class CountingScopeProvider final : public SearchProvider
{
    Q_OBJECT
public:
    CountingScopeProvider(QString id, QString name, ProviderClassification cls, int *counter, QObject *parent = nullptr)
        : SearchProvider(parent)
        , m_id(std::move(id))
        , m_name(std::move(name))
        , m_cls(std::move(cls))
        , m_counter(counter)
    {
        setEnabled(true);
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    void search(const QString &) override
    {
        if (m_counter)
            ++(*m_counter);
        emit searchCompleted(m_id, {});
    }
    SearchProvider *clone(QObject *parent = nullptr) const override
    {
        auto *copy = new CountingScopeProvider(m_id, m_name, m_cls, m_counter, parent);
        copy->setEnabled(isEnabled());
        return copy;
    }

private:
    QString m_id;
    QString m_name;
    ProviderClassification m_cls;
    int *m_counter = nullptr;
};

class MockUiProvider final : public SearchProvider, public SearchProviderContinuation
{
    Q_OBJECT
    Q_INTERFACES(SearchProviderContinuation)
public:
    MockUiProvider(QString id, QString name, QObject *parent = nullptr)
        : SearchProvider(parent)
        , m_id(std::move(id))
        , m_name(std::move(name))
    {
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    void search(const QString &) override {}
    SearchProvider *clone(QObject *parent = nullptr) const override { return new MockUiProvider(m_id, m_name, parent); }
    void cancelRequests() override {}
    void requestPage(const QString &, const QString &token, quint64 requestId) override
    {
        tokens.append(token);
        lastRequestId = requestId;
    }

    void emitSuccess(const QList<SearchResult> &results, const QString &nextToken = {})
    {
        SearchPage page;
        page.results = results;
        page.nextToken = nextToken;
        emit pageCompleted(lastRequestId, page);
    }

    void emitFailure(const QString &error, int retryMs)
    {
        emit requestFailed(lastRequestId, error, retryMs);
    }

    QStringList tokens;
    quint64 lastRequestId = 0;

private:
    QString m_id;
    QString m_name;
};

QStringList topLevelNames(QTreeWidget *tree)
{
    QStringList names;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        names.append(tree->topLevelItem(i)->text(0));
    return names;
}

} // namespace

class DisplayFiltersTest : public QObject
{
    Q_OBJECT
private slots:
    void chipsPreserveDetailsContinuationAndMakeZeroNewRequests()
    {
        TestCountingNetworkManager network;
        SearchTab tab(QStringLiteral("fixture"));
        tab.resize(900, 600);
        tab.show();
        auto *games = new FilterProvider(QStringLiteral("games"), {QStringLiteral("topic:games"), QStringLiteral("currency:USD")}, &network);
        auto *anime = new FilterProvider(QStringLiteral("anime"), {QStringLiteral("topic:anime"), QStringLiteral("topic:tv")}, &network);
        auto *unknown = new FilterProvider(QStringLiteral("unknown"), {}, &network);
        tab.startProvider(games);
        tab.startProvider(anime);
        tab.startProvider(unknown);
        QCOMPARE(network.requests, 3);
        games->finish(QStringLiteral("g1"));
        anime->finish(QStringLiteral("a1"));
        unknown->finish(QStringLiteral("u1"));
        auto *tree = tab.findChild<QTreeWidget *>();
        QVERIFY(tree);
        auto *g = tree->topLevelItem(0), *a = tree->topLevelItem(1), *u = tree->topLevelItem(2);
        auto *selected = g->child(0);
        tree->setCurrentItem(selected);
        auto *details = tab.findChild<QTextBrowser *>();
        QVERIFY(details->toPlainText().contains(QStringLiteral("Cached detail")));
        const auto cached = details->toPlainText();
        const int before = network.requests;
        auto chip = [&tab](const QString &id) { return tab.findChild<QToolButton *>(QStringLiteral("displayFacet-") + id); };
        QVERIFY(chip(QStringLiteral("topic:games")));
        QVERIFY(chip(QStringLiteral("currency:USD")));
        QTest::keyClick(chip(QStringLiteral("topic:anime")), Qt::Key_Space);
        QVERIFY(g->isHidden() && !a->isHidden() && u->isHidden());
        chip(QStringLiteral("topic:games"))->click();
        QVERIFY(!g->isHidden() && !a->isHidden());
        chip(QStringLiteral("currency:USD"))->click();
        QVERIFY(!g->isHidden() && !a->isHidden());
        QCOMPARE(tree->currentItem(), selected);
        QCOMPARE(details->toPlainText(), cached);
        chip(QStringLiteral("display:unclassified"))->click();
        QVERIFY(!g->isHidden() && !a->isHidden() && !u->isHidden());
        SearchResult late;
        late.id = QStringLiteral("late");
        late.title = QStringLiteral("Late");
        emit anime->searchCompleted(anime->id(), {late});
        QCoreApplication::processEvents();
        QCOMPARE(network.requests, before);
        auto *reset = tab.findChild<QPushButton *>(QStringLiteral("resetDisplayFilters"));
        QVERIFY(reset);
        reset->click();
        QVERIFY(!g->isHidden() && !a->isHidden() && !u->isHidden());
        QCOMPARE(network.requests, before);
        QCOMPARE(tree->currentItem(), selected);
        QVERIFY(!chip(QStringLiteral("topic:anime"))->isChecked());
        tab.loadMore(games->id());
        QCOMPARE(network.requests, before + 1);
        games->finish(QStringLiteral("g2"));
        QCOMPARE(g->child(0), selected);
        QVERIFY(g->childCount() >= 2);
        QCOMPARE(details->toPlainText(), cached);
    }

    void missingMetadataAndLateGroups()
    {
        SearchTab tab(QStringLiteral("fixture"));
        SearchResult result;
        result.id = QStringLiteral("one");
        result.title = QStringLiteral("One");
        tab.addResults(QStringLiteral("Games"), {result}, QStringLiteral("cheapshark"));
        auto *games = tab.findChild<QToolButton *>(QStringLiteral("displayFacet-topic:games"));
        QVERIFY(games);
        games->click();
        tab.addResults(QStringLiteral("Unclassified"), {result});
        auto *tree = tab.findChild<QTreeWidget *>();
        QVERIFY(!tree->topLevelItem(0)->isHidden());
        QVERIFY(tree->topLevelItem(1)->isHidden());
        tab.findChild<QPushButton *>(QStringLiteral("resetDisplayFilters"))->click();
        QVERIFY(!tree->topLevelItem(1)->isHidden());
    }

    void inclusiveOrAcrossAudienceAndTopicAndWrapping()
    {
        TestCountingNetworkManager network;
        SearchTab tab(QStringLiteral("fixture"));
        tab.resize(320, 600);
        tab.show();

        auto *wiki = new FilterProvider(QStringLiteral("wikipedia"), {QStringLiteral("audience:general"), QStringLiteral("topic:reference")}, &network);
        auto *github = new FilterProvider(QStringLiteral("github"), {QStringLiteral("audience:technical"), QStringLiteral("topic:software")}, &network);
        auto *manga = new FilterProvider(QStringLiteral("mangadex"), {QStringLiteral("topic:manga")}, &network);
        tab.startProvider(wiki);
        tab.startProvider(github);
        tab.startProvider(manga);
        wiki->finish(QStringLiteral("w1"));
        github->finish(QStringLiteral("gh1"));
        manga->finish(QStringLiteral("m1"));
        QCoreApplication::processEvents();

        auto *tree = tab.findChild<QTreeWidget *>();
        QVERIFY(tree);
        auto *wItem = tree->topLevelItem(0);
        auto *gItem = tree->topLevelItem(1);
        auto *mItem = tree->topLevelItem(2);

        auto chip = [&tab](const QString &id) { return tab.findChild<QToolButton *>(QStringLiteral("displayFacet-") + id); };
        auto *generalChip = chip(QStringLiteral("audience:general"));
        auto *softwareChip = chip(QStringLiteral("topic:software"));
        auto *mangaChip = chip(QStringLiteral("topic:manga"));
        QVERIFY(generalChip && softwareChip && mangaChip);

        generalChip->click();
        QVERIFY(!wItem->isHidden() && gItem->isHidden() && mItem->isHidden());
        softwareChip->click();
        QVERIFY(!wItem->isHidden() && !gItem->isHidden() && mItem->isHidden());

        auto *reset = tab.findChild<QPushButton *>(QStringLiteral("resetDisplayFilters"));
        QVERIFY(reset);
        QCOMPARE(reset->text(), QStringLiteral("×"));
        const int requestsBefore = network.requests;
        reset->click();
        QCOMPARE(network.requests, requestsBefore);
        QVERIFY(!wItem->isHidden() && !gItem->isHidden() && !mItem->isHidden());

        bool wrapped = false;
        const int firstY = generalChip->y();
        for (auto *btn : {softwareChip, mangaChip, chip(QStringLiteral("topic:reference")), chip(QStringLiteral("audience:technical"))}) {
            if (btn && btn->y() > firstY) {
                wrapped = true;
                break;
            }
        }
        QVERIFY(wrapped);
    }
};

class TestPrequeryScope : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void testDashboardTagUnionPreviewAndReset()
    {
        SearchDashboard dashboard;
        dashboard.setAvailableScopes({
            {QStringLiteral("wikipedia"), ProviderClassification::builtin(QStringLiteral("wikipedia"))},
            {QStringLiteral("github"), ProviderClassification::builtin(QStringLiteral("github"))},
            {QStringLiteral("mangadex"), ProviderClassification::builtin(QStringLiteral("mangadex"))},
        });

        QVERIFY(dashboard.selectedSourceIds().isEmpty());
        auto *preview = dashboard.findChild<QLabel *>(QStringLiteral("preQueryScopePreview"));
        QVERIFY(preview != nullptr);
        QVERIFY(preview->text().contains(QStringLiteral("all 3 enabled sources")));

        auto *generalBtn = dashboard.findChild<QToolButton *>(QStringLiteral("scopeTag_audience:general"));
        auto *softwareBtn = dashboard.findChild<QToolButton *>(QStringLiteral("scopeTag_topic:software"));
        QVERIFY(generalBtn != nullptr && softwareBtn != nullptr);

        generalBtn->click();
        QCOMPARE(dashboard.selectedSourceIds(), (QSet<QString>{QStringLiteral("wikipedia")}));
        QVERIFY(preview->text().contains(QStringLiteral("1 of 3")));

        softwareBtn->click();
        QCOMPARE(dashboard.selectedSourceIds(), (QSet<QString>{QStringLiteral("wikipedia"), QStringLiteral("github")}));
        QVERIFY(preview->text().contains(QStringLiteral("2 of 3")));

        QSignalSpy scopedSpy(&dashboard, &SearchDashboard::searchScopedRequested);
        auto *bar = dashboard.findChild<QLineEdit *>();
        QVERIFY(bar != nullptr);
        bar->setText(QStringLiteral("qt6"));
        QTest::keyClick(bar, Qt::Key_Return);
        QCOMPARE(scopedSpy.count(), 1);
        const auto emittedScope = scopedSpy.at(0).at(1).value<QueryScope>();
        QVERIFY(emittedScope.restricted);
        QCOMPARE(emittedScope.sourceIds, (QSet<QString>{QStringLiteral("wikipedia"), QStringLiteral("github")}));
        QCOMPARE(dashboard.selectedScope(), QueryScope::all());
        QVERIFY(dashboard.selectedSourceIds().isEmpty());
    }

    void testExplicitEmptyScopeMeansZeroProviders()
    {
        int wikiCalls = 0;
        auto *wiki = new CountingScopeProvider(QStringLiteral("wikipedia"), QStringLiteral("Wikipedia"),
                                               ProviderClassification::builtin(QStringLiteral("wikipedia")), &wikiCalls);
        SearchWindow emptyScopeWin(QStringLiteral("zero query"), {wiki}, nullptr, nullptr, QueryScope::only({}));
        QCOMPARE(wikiCalls, 0);
        QVERIFY(!emptyScopeWin.searchTab()->hasProvider(QStringLiteral("wikipedia")));
        delete wiki;
    }

    void testScopedDispatchInTabAndNewWindowAndUiFacetExpansion()
    {
        int wikiCalls = 0, githubCalls = 0, mangaCalls = 0;
        auto *wiki = new CountingScopeProvider(QStringLiteral("wikipedia"), QStringLiteral("Wikipedia"),
                                               ProviderClassification::builtin(QStringLiteral("wikipedia")), &wikiCalls);
        auto *github = new CountingScopeProvider(QStringLiteral("github"), QStringLiteral("GitHub"),
                                                 ProviderClassification::builtin(QStringLiteral("github")), &githubCalls);
        auto *manga = new CountingScopeProvider(QStringLiteral("mangadex"), QStringLiteral("MangaDex"),
                                                ProviderClassification::builtin(QStringLiteral("mangadex")), &mangaCalls);

        SearchWindow win(QStringLiteral("manga query"), {wiki, github, manga}, nullptr, nullptr,
                         QueryScope::only({QStringLiteral("mangadex")}));
        QCOMPARE(wikiCalls, 0);
        QCOMPARE(githubCalls, 0);
        QCOMPARE(mangaCalls, 1);

        auto *tab = win.searchTab();
        QVERIFY(tab != nullptr);
        tab->setSourceChoices({
            {QStringLiteral("mangadex"), QStringLiteral("MangaDex"), QStringLiteral("Enabled"), true, true, ProviderClassification::builtin(QStringLiteral("mangadex"))},
            {QStringLiteral("github"), QStringLiteral("GitHub"), QStringLiteral("Available"), false, true, ProviderClassification::builtin(QStringLiteral("github"))},
            {QStringLiteral("wikipedia"), QStringLiteral("Wikipedia"), QStringLiteral("Disabled"), false, false, ProviderClassification::builtin(QStringLiteral("wikipedia"))},
        });
        QObject::connect(tab, &SearchTab::searchSelectedSourcesRequested, tab, [&](const QSet<QString> &selected) {
            tab->applySourceEligibility(selected);
            if (selected.contains(QStringLiteral("github")) && !tab->hasProvider(QStringLiteral("github")))
                tab->startProvider(github->clone(tab));
        });

        auto *softwareChip = tab->findChild<QToolButton *>(QStringLiteral("displayFacet-topic:software"));
        auto *expandBtn = tab->findChild<QPushButton *>(QStringLiteral("expandScopeSources"));
        QVERIFY(softwareChip != nullptr && expandBtn != nullptr);
        softwareChip->click();
        QCOMPARE(githubCalls, 0);
        QCOMPARE(wikiCalls, 0);
        QVERIFY(!expandBtn->isHidden());
        QVERIFY(expandBtn->text().contains(QStringLiteral("1")));

        expandBtn->click();
        QCOMPARE(githubCalls, 1);
        QCOMPARE(mangaCalls, 1);
        QCOMPARE(wikiCalls, 0);
        QVERIFY(expandBtn->isHidden());

        auto *clearBtn = tab->findChild<QPushButton *>(QStringLiteral("resetDisplayFilters"));
        QVERIFY(clearBtn != nullptr);
        clearBtn->click();
        QCOMPARE(githubCalls, 1);
        QCOMPARE(mangaCalls, 1);
        QCOMPARE(wikiCalls, 0);

        delete wiki;
        delete github;
        delete manga;
    }
};

class TestResultUiAndErrors : public QObject
{
    Q_OBJECT
private slots:
    void testCompactAffordancesAndPagination()
    {
        SearchTab tab(QStringLiteral("test query"));
        auto *popOut = tab.findChild<QPushButton *>(QStringLiteral("popOutSearch"));
        QVERIFY(popOut != nullptr && popOut->isFlat() && !popOut->accessibleName().isEmpty());

        auto *prov = new MockUiProvider(QStringLiteral("alpha"), QStringLiteral("Alpha"));
        tab.startProvider(prov);
        QCOMPARE(prov->tokens.size(), 1);

        prov->emitSuccess({{QStringLiteral("1"), QStringLiteral("First"), {}, {}}}, QStringLiteral("page2"));
        auto *loadMoreBtn = tab.findChild<QPushButton *>(QStringLiteral("loadMore_alpha"));
        QVERIFY(loadMoreBtn != nullptr && loadMoreBtn->isFlat());
        QVERIFY(loadMoreBtn->text().contains(QStringLiteral("…")));
        QVERIFY(loadMoreBtn->accessibleName().contains(QStringLiteral("Alpha")));

        loadMoreBtn->click();
        QCOMPARE(prov->tokens.size(), 2);
        QCOMPARE(prov->tokens.last(), QStringLiteral("page2"));
    }

    void testErrorClassificationAndSettingsFocus()
    {
        SearchTab tab(QStringLiteral("test query"));
        QSignalSpy settingsSpy(&tab, &SearchTab::settingsRequested);

        auto *retryProv = new MockUiProvider(QStringLiteral("retryable"), QStringLiteral("Retryable"));
        auto *authProv = new MockUiProvider(QStringLiteral("authfail"), QStringLiteral("AuthFail"));
        auto *termProv = new MockUiProvider(QStringLiteral("terminal"), QStringLiteral("Terminal"));

        tab.startProvider(retryProv);
        tab.startProvider(authProv);
        tab.startProvider(termProv);

        retryProv->emitFailure(QStringLiteral("Temporary timeout"), 60);
        auto *retryLabel = tab.findChild<QLabel *>(QStringLiteral("errorLabel_retryable"));
        auto *retryBtn = tab.findChild<QPushButton *>(QStringLiteral("loadMore_retryable"));
        QVERIFY(retryLabel != nullptr && retryBtn != nullptr && !retryBtn->isEnabled());
        const QColor expectedNeg = KColorScheme(QPalette::Active, KColorScheme::View).foreground(KColorScheme::NegativeText).color();
        QCOMPARE(retryLabel->palette().color(QPalette::WindowText), expectedNeg);

        QTRY_VERIFY_WITH_TIMEOUT(retryBtn->isEnabled(), 1000);
        retryBtn->click();
        QCOMPARE(retryProv->tokens.size(), 2);

        authProv->emitFailure(QStringLiteral("401 Unauthorized: API key missing"), 0);
        QVERIFY(tab.findChild<QLabel *>(QStringLiteral("errorLabel_authfail")) != nullptr);
        QVERIFY(tab.findChild<QPushButton *>(QStringLiteral("loadMore_authfail")) == nullptr);
        auto *settingsBtn = tab.findChild<QPushButton *>(QStringLiteral("settingsAction_authfail"));
        QVERIFY(settingsBtn != nullptr);
        settingsBtn->click();
        QCOMPARE(settingsSpy.count(), 1);
        QCOMPARE(settingsSpy.at(0).at(0).toString(), QStringLiteral("authfail"));

        SettingsDialog dlg(QList<SearchProvider *>{retryProv, authProv, termProv});
        QVERIFY(dlg.focusProvider(QStringLiteral("authfail"), QStringLiteral("providers")));

        termProv->emitFailure(QStringLiteral("Invalid request"), -1);
        QVERIFY(tab.findChild<QLabel *>(QStringLiteral("errorLabel_terminal")) != nullptr);
        QVERIFY(tab.findChild<QPushButton *>(QStringLiteral("loadMore_terminal")) == nullptr);
        QVERIFY(tab.findChild<QPushButton *>(QStringLiteral("settingsAction_terminal")) == nullptr);
    }

    void testFailedProvidersSortBelowHealthyWithStableOrder()
    {
        SearchTab tab(QStringLiteral("order query"));
        auto *tree = tab.findChild<QTreeWidget *>(QStringLiteral("resultsTree"));
        QVERIFY(tree != nullptr);

        auto *pA = new MockUiProvider(QStringLiteral("a"), QStringLiteral("Alpha"));
        auto *pB = new MockUiProvider(QStringLiteral("b"), QStringLiteral("Beta"));
        auto *pC = new MockUiProvider(QStringLiteral("c"), QStringLiteral("Gamma"));
        tab.startProvider(pA);
        tab.startProvider(pB);
        tab.startProvider(pC);

        QCOMPARE(topLevelNames(tree), (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gamma")}));

        pB->emitSuccess({{QStringLiteral("b1"), QStringLiteral("Beta One"), {}, {}}}, QStringLiteral("b_next"));
        auto *bLoadMore = tab.findChild<QPushButton *>(QStringLiteral("loadMore_b"));
        QVERIFY(bLoadMore != nullptr);
        bLoadMore->click();
        pB->emitFailure(QStringLiteral("Rate limited"), 10);

        QCOMPARE(topLevelNames(tree), (QStringList{QStringLiteral("Alpha"), QStringLiteral("Gamma"), QStringLiteral("Beta")}));
        QCOMPARE(tree->topLevelItem(2)->childCount(), 2);

        pA->emitFailure(QStringLiteral("Network down"), 10);
        QCOMPARE(topLevelNames(tree), (QStringList{QStringLiteral("Gamma"), QStringLiteral("Alpha"), QStringLiteral("Beta")}));

        QTRY_VERIFY_WITH_TIMEOUT(tab.findChild<QPushButton *>(QStringLiteral("loadMore_b")) &&
                                     tab.findChild<QPushButton *>(QStringLiteral("loadMore_b"))->isEnabled(),
                                 1000);
        tab.findChild<QPushButton *>(QStringLiteral("loadMore_b"))->click();
        pB->emitSuccess({{QStringLiteral("b2"), QStringLiteral("Beta Two"), {}, {}}}, {});
        QCOMPARE(topLevelNames(tree), (QStringList{QStringLiteral("Beta"), QStringLiteral("Gamma"), QStringLiteral("Alpha")}));
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir root;
    if (!root.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", root.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", root.filePath(QStringLiteral("config-dirs")).toUtf8());
    qputenv("XDG_DATA_HOME", root.filePath(QStringLiteral("data")).toUtf8());
    qputenv("XDG_DATA_DIRS", root.filePath(QStringLiteral("data-dirs")).toUtf8());
    QApplication app(argc, argv);

    DisplayFiltersTest filters;
    TestPrequeryScope prequery;
    TestResultUiAndErrors resultUi;
    return QTest::qExec(&filters, argc, argv) | QTest::qExec(&prequery, argc, argv) | QTest::qExec(&resultUi, argc, argv);
}

#include "test_searchtab_scope_and_ui.moc"
