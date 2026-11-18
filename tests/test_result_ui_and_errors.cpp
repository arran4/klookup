// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchcontinuation.h"
#include "searchtab.h"
#include "settingsdialog.h"

#include <KColorScheme>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>

namespace
{

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

class TestResultUiAndErrors : public QObject
{
    Q_OBJECT

private slots:
    void testCompactAffordancesAndPagination()
    {
        SearchTab tab(QStringLiteral("test query"));
        auto *popOut = tab.findChild<QPushButton *>(QStringLiteral("popOutSearch"));
        QVERIFY(popOut != nullptr);
        QVERIFY(popOut->isFlat());
        QVERIFY(!popOut->accessibleName().isEmpty());

        auto *prov = new MockUiProvider(QStringLiteral("alpha"), QStringLiteral("Alpha"));
        tab.startProvider(prov);
        QCOMPARE(prov->tokens.size(), 1);

        prov->emitSuccess({{QStringLiteral("1"), QStringLiteral("First"), {}, {}}}, QStringLiteral("page2"));
        auto *loadMoreBtn = tab.findChild<QPushButton *>(QStringLiteral("loadMore_alpha"));
        QVERIFY(loadMoreBtn != nullptr);
        QVERIFY(loadMoreBtn->isFlat());
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

        // 1. Retryable error with 60ms backoff
        retryProv->emitFailure(QStringLiteral("Temporary timeout"), 60);
        auto *retryLabel = tab.findChild<QLabel *>(QStringLiteral("errorLabel_retryable"));
        auto *retryBtn = tab.findChild<QPushButton *>(QStringLiteral("loadMore_retryable"));
        QVERIFY(retryLabel != nullptr);
        QVERIFY(retryBtn != nullptr);
        QVERIFY(!retryBtn->isEnabled());
        const QColor expectedNeg = KColorScheme(QPalette::Active, KColorScheme::View).foreground(KColorScheme::NegativeText).color();
        QCOMPARE(retryLabel->palette().color(QPalette::WindowText), expectedNeg);

        QTRY_VERIFY_WITH_TIMEOUT(retryBtn->isEnabled(), 1000);
        retryBtn->click();
        QCOMPARE(retryProv->tokens.size(), 2);

        // 2. Settings/auth error: shows Go to Settings, no dead Retry button
        authProv->emitFailure(QStringLiteral("401 Unauthorized: API key missing"), 0);
        QVERIFY(tab.findChild<QLabel *>(QStringLiteral("errorLabel_authfail")) != nullptr);
        QVERIFY(tab.findChild<QPushButton *>(QStringLiteral("loadMore_authfail")) == nullptr);
        auto *settingsBtn = tab.findChild<QPushButton *>(QStringLiteral("settingsAction_authfail"));
        QVERIFY(settingsBtn != nullptr);
        settingsBtn->click();
        QCOMPARE(settingsSpy.count(), 1);
        QCOMPARE(settingsSpy.at(0).at(0).toString(), QStringLiteral("authfail"));

        // Verify SettingsDialog::focusProvider selects the matching provider
        SettingsDialog dlg(QList<SearchProvider *>{retryProv, authProv, termProv});
        QVERIFY(dlg.focusProvider(QStringLiteral("authfail"), QStringLiteral("providers")));

        // 3. Terminal error (retryMs = -1): non-interactive label only
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

        // Beta gets 1 result, then fails pagination
        pB->emitSuccess({{QStringLiteral("b1"), QStringLiteral("Beta One"), {}, {}}}, QStringLiteral("b_next"));
        auto *bLoadMore = tab.findChild<QPushButton *>(QStringLiteral("loadMore_b"));
        QVERIFY(bLoadMore != nullptr);
        bLoadMore->click();
        pB->emitFailure(QStringLiteral("Rate limited"), 10);

        // Beta moves below healthy Alpha and Gamma, preserving its loaded result
        QCOMPARE(topLevelNames(tree), (QStringList{QStringLiteral("Alpha"), QStringLiteral("Gamma"), QStringLiteral("Beta")}));
        QCOMPARE(tree->topLevelItem(2)->childCount(), 2); // Beta One + error status row

        // Alpha also fails -> healthy Gamma first, then Alpha (order 0) before Beta (order 1)
        pA->emitFailure(QStringLiteral("Network down"), 10);
        QCOMPARE(topLevelNames(tree), (QStringList{QStringLiteral("Gamma"), QStringLiteral("Alpha"), QStringLiteral("Beta")}));

        // Beta recovers on retry -> Beta (healthy, order 1) and Gamma (healthy, order 2) above Alpha (failed)
        QTRY_VERIFY_WITH_TIMEOUT(tab.findChild<QPushButton *>(QStringLiteral("loadMore_b")) &&
                                     tab.findChild<QPushButton *>(QStringLiteral("loadMore_b"))->isEnabled(),
                                 1000);
        tab.findChild<QPushButton *>(QStringLiteral("loadMore_b"))->click();
        pB->emitSuccess({{QStringLiteral("b2"), QStringLiteral("Beta Two"), {}, {}}}, {});
        QCOMPARE(topLevelNames(tree), (QStringList{QStringLiteral("Beta"), QStringLiteral("Gamma"), QStringLiteral("Alpha")}));
    }
};

QTEST_MAIN(TestResultUiAndErrors)
#include "test_result_ui_and_errors.moc"
