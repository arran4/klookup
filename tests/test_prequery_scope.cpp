// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "searchdashboard.h"
#include "searchprovider.h"
#include "searchtab.h"
#include "searchwindow.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

namespace
{

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

} // namespace

class TestPrequeryScope : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

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

        // Select General (wikipedia) + Software (github) -> inclusive union of 2 sources
        auto *generalBtn = dashboard.findChild<QToolButton *>(QStringLiteral("scopeTag_audience:general"));
        auto *softwareBtn = dashboard.findChild<QToolButton *>(QStringLiteral("scopeTag_topic:software"));
        QVERIFY(generalBtn != nullptr);
        QVERIFY(softwareBtn != nullptr);

        generalBtn->click();
        QCOMPARE(dashboard.selectedSourceIds(), (QSet<QString>{QStringLiteral("wikipedia")}));
        QVERIFY(preview->text().contains(QStringLiteral("1 of 3")));

        softwareBtn->click();
        QCOMPARE(dashboard.selectedSourceIds(), (QSet<QString>{QStringLiteral("wikipedia"), QStringLiteral("github")}));
        QVERIFY(preview->text().contains(QStringLiteral("2 of 3")));

        // Trigger search -> emits scoped selection, then resets for next query
        QSignalSpy scopedSpy(&dashboard, &SearchDashboard::searchScopedRequested);
        auto *bar = dashboard.findChild<QLineEdit *>();
        QVERIFY(bar != nullptr);
        bar->setText(QStringLiteral("qt6"));
        QTest::keyClick(bar, Qt::Key_Return);
        QCOMPARE(scopedSpy.count(), 1);
        QCOMPARE(scopedSpy.at(0).at(1).value<QSet<QString>>(),
                 (QSet<QString>{QStringLiteral("wikipedia"), QStringLiteral("github")}));
        QVERIFY(dashboard.selectedSourceIds().isEmpty());
    }

    void testScopedDispatchInTabAndNewWindowAndIncrementalExpansion()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());

        int wikiCalls = 0;
        int githubCalls = 0;
        int mangaCalls = 0;

        auto *wiki = new CountingScopeProvider(QStringLiteral("wikipedia"), QStringLiteral("Wikipedia"),
                                               ProviderClassification::builtin(QStringLiteral("wikipedia")), &wikiCalls);
        auto *github = new CountingScopeProvider(QStringLiteral("github"), QStringLiteral("GitHub"),
                                                 ProviderClassification::builtin(QStringLiteral("github")), &githubCalls);
        auto *manga = new CountingScopeProvider(QStringLiteral("mangadex"), QStringLiteral("MangaDex"),
                                                ProviderClassification::builtin(QStringLiteral("mangadex")), &mangaCalls);

        // New-window search with only Manga selected -> zero Wikipedia/GitHub calls
        SearchWindow win(QStringLiteral("manga query"), {wiki, github, manga}, nullptr, nullptr,
                         {QStringLiteral("mangadex")});
        QCOMPARE(wikiCalls, 0);
        QCOMPARE(githubCalls, 0);
        QCOMPARE(mangaCalls, 1);

        // Incremental expansion on SearchTab: adding Wikipedia only queries Wikipedia once, not MangaDex again
        auto *tab = win.searchTab();
        QVERIFY(tab != nullptr);
        QVERIFY(tab->hasProvider(QStringLiteral("mangadex")));
        QVERIFY(!tab->hasProvider(QStringLiteral("wikipedia")));
        tab->applySourceEligibility({QStringLiteral("mangadex"), QStringLiteral("wikipedia")});
        tab->startProvider(wiki->clone(tab));
        QCOMPARE(wikiCalls, 1);
        QCOMPARE(mangaCalls, 1);
        QCOMPARE(githubCalls, 0);

        delete wiki;
        delete github;
        delete manga;
    }
};

QTEST_MAIN(TestPrequeryScope)
#include "test_prequery_scope.moc"
