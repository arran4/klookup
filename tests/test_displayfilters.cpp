// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "isolatedguitest.h"
#include "searchtab.h"
#include "searchcontinuation.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QToolButton>
#include <QPushButton>
#include <QTextBrowser>
class FilterReply : public QNetworkReply {
public:
    FilterReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent) { setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly); setFinished(true); }
    void abort() override {}
protected:
    qint64 readData(char *, qint64) override { return -1; }
};
class FilterNetwork : public QNetworkAccessManager {
public:
    int requests = 0;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override { ++requests; return new FilterReply(request, this); }
};
class FilterProvider : public SearchProvider, public SearchProviderContinuation {
    Q_OBJECT
    Q_INTERFACES(SearchProvider SearchProviderContinuation)
public:
    FilterProvider(const QString &id, QStringList tags, FilterNetwork *network) : sourceId(id), tags(tags), network(network) {
        ProviderClassification classification; classification.facets = tags;
        setClassification(classification);
    }
    QString sourceId; QStringList tags; FilterNetwork *network; quint64 pending = 0;
    QString id() const override { return sourceId; }
    QString name() const override { return sourceId; }
    SearchProvider *clone(QObject *) const override { return nullptr; }
    void search(const QString &) override {}
    void requestPage(const QString &, const QString &, quint64 id) override {
        network->get(QNetworkRequest(QUrl(QStringLiteral("https://example.invalid/page")))); pending = id;
    }
    void finish(const QString &id) {
        SearchResult result; result.id = id; result.title = id;
        SearchPage page; page.results = {result}; page.nextToken = QStringLiteral("next");
        emit pageCompleted(pending, page);
    }
    bool supportsDetails() const override { return true; }
    void requestDetails(const QString &id, const QString &, quint64 request) override {
        network->get(QNetworkRequest(QUrl(QStringLiteral("https://example.invalid/details"))));
        SearchDetails details; details.result.id = id; details.result.title = id; details.result.description = QStringLiteral("Cached detail");
        emit detailsCompleted(request, details);
    }
    void cancelRequests() override {}
};
class DisplayFiltersTest : public QObject {
    Q_OBJECT
private slots:
    void chipsPreserveDetailsContinuationAndMakeZeroNewRequests() {
        FilterNetwork network;
        SearchTab tab(QStringLiteral("fixture")); tab.resize(900, 600); tab.show();
        auto *games = new FilterProvider(QStringLiteral("games"), {QStringLiteral("topic:games"), QStringLiteral("currency:USD")}, &network);
        auto *anime = new FilterProvider(QStringLiteral("anime"), {QStringLiteral("topic:anime"), QStringLiteral("topic:tv")}, &network);
        auto *unknown = new FilterProvider(QStringLiteral("unknown"), {}, &network);
        tab.startProvider(games); tab.startProvider(anime); tab.startProvider(unknown);
        QCOMPARE(network.requests, 3);
        games->finish(QStringLiteral("g1")); anime->finish(QStringLiteral("a1")); unknown->finish(QStringLiteral("u1"));
        auto *tree = tab.findChild<QTreeWidget *>(); QVERIFY(tree);
        auto *g = tree->topLevelItem(0), *a = tree->topLevelItem(1), *u = tree->topLevelItem(2);
        auto *selected = g->child(0); tree->setCurrentItem(selected);
        auto *details = tab.findChild<QTextBrowser *>(); QVERIFY(details->toPlainText().contains(QStringLiteral("Cached detail")));
        const auto cached = details->toPlainText();
        const int before = network.requests;
        auto chip = [&tab](const QString &id) { return tab.findChild<QToolButton *>(QStringLiteral("displayFacet-") + id); };
        QVERIFY(chip(QStringLiteral("topic:games"))); QVERIFY(chip(QStringLiteral("currency:USD")));
        QTest::keyClick(chip(QStringLiteral("topic:anime")), Qt::Key_Space);
        QVERIFY(g->isHidden() && !a->isHidden() && u->isHidden());
        chip(QStringLiteral("topic:games"))->click(); // OR within topic
        QVERIFY(!g->isHidden() && !a->isHidden());
        chip(QStringLiteral("currency:USD"))->click(); // AND across facets
        QVERIFY(!g->isHidden() && a->isHidden());
        QCOMPARE(tree->currentItem(), selected); QCOMPARE(details->toPlainText(), cached);
        chip(QStringLiteral("display:unclassified"))->click();
        QVERIFY(!g->isHidden() && !u->isHidden());
        // Asynchronous results in a hidden source remain cached and hidden.
        SearchResult late; late.id = QStringLiteral("late"); late.title = QStringLiteral("Late");
        emit anime->searchCompleted(anime->id(), {late});
        QCoreApplication::processEvents();
        QCOMPARE(network.requests, before); // Every chip/keyboard/reset toggle must be network-free.
        auto *reset = tab.findChild<QPushButton *>(QStringLiteral("resetDisplayFilters")); QVERIFY(reset); reset->click();
        QVERIFY(!g->isHidden() && !a->isHidden() && !u->isHidden());
        QCOMPARE(network.requests, before); QCOMPARE(tree->currentItem(), selected);
        QVERIFY(!chip(QStringLiteral("topic:anime"))->isChecked());
        tab.loadMore(games->id()); QCOMPARE(network.requests, before + 1);
        games->finish(QStringLiteral("g2"));
        QCOMPARE(g->child(0), selected); QVERIFY(g->childCount() >= 2);
        QCOMPARE(details->toPlainText(), cached);
    }
    void missingMetadataAndLateGroups() {
        SearchTab tab(QStringLiteral("fixture"));
        SearchResult result; result.id = QStringLiteral("one"); result.title = QStringLiteral("One");
        tab.addResults(QStringLiteral("Games"), {result}, QStringLiteral("cheapshark"));
        auto *games = tab.findChild<QToolButton *>(QStringLiteral("displayFacet-topic:games")); QVERIFY(games); games->click();
        tab.addResults(QStringLiteral("Unclassified"), {result});
        auto *tree = tab.findChild<QTreeWidget *>();
        QVERIFY(!tree->topLevelItem(0)->isHidden()); QVERIFY(tree->topLevelItem(1)->isHidden());
        tab.findChild<QPushButton *>(QStringLiteral("resetDisplayFilters"))->click();
        QVERIFY(!tree->topLevelItem(1)->isHidden());
    }
};
int main(int argc, char **argv) { return runIsolatedGuiTest<DisplayFiltersTest>(argc, argv); }
#include "test_displayfilters.moc"
