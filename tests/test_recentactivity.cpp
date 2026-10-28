#include "../src/mainwindow.h"
#include "../src/recentactivity.h"
#include "../src/searchdashboard.h"
#include "../src/searchtab.h"
#include "../src/settingsdialog.h"
#include <KConfigGroup>
#include <QAbstractButton>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

class ClosingWindow : public MainWindow
{
public:
    using MainWindow::queryClose;
};
class RecentActivityTest : public QObject
{
    Q_OBJECT
    QTemporaryDir configHome;
    QList<QUrl> opened;
public slots:
    void openSource(const QUrl &url)
    {
        opened.append(url);
    }
private slots:
    void initTestCase()
    {
        QVERIFY(configHome.isValid());
        qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    }
    void init()
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        for (const auto &group : config->groupList())
            config->deleteGroup(group);
        config->sync();
    }
    void privacyPersistenceAndRetention()
    {
        const auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        RecentActivity activity(config);
        activity.recordSearch(QStringLiteral("manual query"));
        activity.recordCapture(QStringLiteral("dummy-private-clipboard"));
        activity.recordSearch(QStringLiteral("dummy-private-automatic"), true);
        QCOMPARE(activity.items().size(), 1);
        KConfigGroup policy(config, QStringLiteral("General"));
        policy.writeEntry(QStringLiteral("RecordClipboardActivity"), true);
        activity.reloadPolicy();
        activity.recordCapture(QStringLiteral("dummy-private-clipboard"));
        activity.recordSearch(QStringLiteral("dummy-private-automatic"), true);
        QCOMPARE(activity.items().size(), 3);
        config->sync();
        QFile file(configHome.path() + QStringLiteral("/klookuprc"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("dummy-private"));
        RecentActivity restored(config);
        QCOMPARE(restored.items().size(), 1); // session-only clipboard entries
        policy.writeEntry(QStringLiteral("PersistClipboardActivity"), true);
        activity.reloadPolicy();
        RecentActivity persisted(config);
        QCOMPARE(persisted.items().size(), 3);
        persisted.expire(QDateTime::currentDateTimeUtc().addDays(1).addSecs(1));
        QCOMPARE(persisted.items().size(), 1);
        persisted.expire(QDateTime::currentDateTimeUtc().addDays(31));
        QVERIFY(persisted.items().isEmpty());
        QVERIFY(!KConfigGroup(config, QStringLiteral("RecentActivity")).hasKey(QStringLiteral("Items")));
    }
    void deduplicationBoundsAndSafeRevisitData()
    {
        const auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        RecentActivity activity(config);
        for (int i = 0; i < 80; ++i)
            activity.recordSearch(QStringLiteral("query %1").arg(i));
        QCOMPARE(activity.items().size(), 50);
        const auto key = activity.items().first().key;
        activity.recordSearch(QStringLiteral("query 79"));
        QCOMPARE(activity.items().first().key, key);
        QCOMPARE(activity.items().size(), 50);
        activity.recordSearch(QStringLiteral("https://user:pass@example.org/private"));
        activity.recordSearch(QStringLiteral("https://example.org/?access_token=dummy-secret"));
        activity.recordSearch(QStringLiteral("https://example.org/#access_token=dummy-secret"));
        QCOMPARE(activity.items().size(), 50);
        SearchResult result;
        result.id = QStringLiteral("id");
        result.title = QStringLiteral("<b>Untrusted title</b>");
        result.url = QStringLiteral("https://example.org/source");
        result.description = QStringLiteral("Readable snippet");
        activity.recordViewed(QStringLiteral("provider-id"), result);
        QCOMPARE(activity.items().first().kind, RecentItem::Kind::Viewed);
        QCOMPARE(activity.items().first().source, QStringLiteral("provider-id"));
        activity.recordViewed(QStringLiteral("provider-id"), result);
        QCOMPARE(activity.items().size(), 50);
        const auto viewedKey = activity.items().first().key;
        result.url = QStringLiteral("https://example.org/?api_key=dummy-secret");
        activity.recordViewed(QStringLiteral("provider-id"), result);
        QCOMPARE(activity.items().first().key, viewedKey);
        activity.remove(viewedKey);
        QCOMPARE(activity.items().size(), 49);
        const QString exactQuery = QStringLiteral("title:\"two  spaces\"");
        activity.recordSearch(exactQuery);
        QCOMPARE(activity.items().first().query, exactQuery);
        RecentActivity restored(config);
        QCOMPARE(restored.items().first().query, exactQuery);
    }
    void malformedStoredEntriesAreIgnored()
    {
        const auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        RecentActivity activity(config);
        activity.recordSearch(QStringLiteral("valid query"));
        KConfigGroup group(config, QStringLiteral("RecentActivity"));
        const auto valid = group.readEntry(QStringLiteral("Items"), QStringList()).first();
        const auto original = QJsonDocument::fromJson(valid.toUtf8()).object();
        QStringList saved{QStringLiteral("not JSON")};
        for (const auto &field : {QStringLiteral("kind"), QStringLiteral("timestamp"), QStringLiteral("url"), QStringLiteral("query")}) {
            auto object = original;
            if (field == QStringLiteral("kind"))
                object[field] = 99;
            if (field == QStringLiteral("timestamp"))
                object[field] = QDateTime::currentDateTimeUtc().addDays(10).toString(Qt::ISODateWithMs);
            if (field == QStringLiteral("url"))
                object[field] = QStringLiteral("file:///private");
            if (field == QStringLiteral("query"))
                object[field] = QString(3000, QLatin1Char('x'));
            saved.append(QString::fromUtf8(QJsonDocument(object).toJson()));
        }
        saved.append(valid);
        group.writeEntry(QStringLiteral("Items"), saved);
        RecentActivity restored(config);
        QCOMPARE(restored.items().size(), 1);
        QCOMPARE(restored.items().first().query, QStringLiteral("valid query"));
    }
    void dashboardHideRemoveClearAndKeyboard()
    {
        const auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        RecentActivity activity(config);
        SearchDashboard dashboard;
        dashboard.loadHistory({QStringLiteral("autocomplete stays")});
        dashboard.setRecentActivity(&activity);
        dashboard.show();
        auto *list = dashboard.findChild<QListWidget *>(QStringLiteral("recentActivityList"));
        QVERIFY(list->isHidden());
        for (int i = 0; i < 15; ++i)
            activity.recordSearch(QStringLiteral("query %1").arg(i));
        QCOMPARE(list->count(), 10);
        QVERIFY(list->item(0)->text().contains(QStringLiteral("Searched query")));
        QSignalSpy activated(&dashboard, &SearchDashboard::recentActivated);
        list->setCurrentRow(0);
        dashboard.activateWindow();
        list->setFocus();
        QTRY_VERIFY(list->hasFocus());
        QTest::keyClick(list, Qt::Key_Return);
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().value<RecentItem>().query, QStringLiteral("query 14"));
        QTest::keyClick(list, Qt::Key_Delete);
        QCOMPARE(activity.items().size(), 14);
        KConfigGroup policy(config, QStringLiteral("General"));
        policy.writeEntry(QStringLiteral("ShowRecentActivity"), false);
        activity.reloadPolicy();
        QVERIFY(dashboard.findChild<QWidget *>(QStringLiteral("recentActivitySection"))->isHidden());
        RecentActivity hiddenRestored(config);
        QCOMPARE(hiddenRestored.items().size(), 14);
        policy.writeEntry(QStringLiteral("ShowRecentActivity"), true);
        activity.reloadPolicy();
        QCOMPARE(list->count(), 10);
        QTimer::singleShot(0, &dashboard, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->button(QMessageBox::No)->click();
        });
        dashboard.findChild<QPushButton *>(QStringLiteral("clearRecentActivity"))->click();
        QCOMPARE(activity.items().size(), 14);
        QTimer::singleShot(0, &dashboard, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Yes)->click();
        });
        dashboard.findChild<QPushButton *>(QStringLiteral("clearRecentActivity"))->click();
        QVERIFY(activity.items().isEmpty());
        QCOMPARE(dashboard.saveHistory(), QStringList{QStringLiteral("autocomplete stays")});
        RecentActivity cleared(config);
        QVERIFY(cleared.items().isEmpty());
    }
    void settingsApplyCancelAndPolicyPurge()
    {
        const auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        SettingsDialog cancelled({});
        cancelled.findChild<QCheckBox *>(QStringLiteral("showRecentActivity"))->setChecked(false);
        cancelled.reject();
        QVERIFY(KConfigGroup(config, QStringLiteral("General")).readEntry(QStringLiteral("ShowRecentActivity"), true));
        RecentActivity activity(config);
        SettingsDialog applied({});
        connect(&applied, &SettingsDialog::settingsApplied, &activity, &RecentActivity::reloadPolicy);
        applied.findChild<QCheckBox *>(QStringLiteral("showRecentActivity"))->setChecked(false);
        applied.findChild<QCheckBox *>(QStringLiteral("recordClipboardActivity"))->setChecked(true);
        applied.findChild<QCheckBox *>(QStringLiteral("persistClipboardActivity"))->setChecked(true);
        applied.button(QDialogButtonBox::Apply)->click();
        QVERIFY(!activity.shown());
        activity.recordCapture(QStringLiteral("dummy-private-clipboard"));
        QCOMPARE(activity.items().size(), 1);
        applied.findChild<QCheckBox *>(QStringLiteral("recordClipboardActivity"))->setChecked(false);
        applied.button(QDialogButtonBox::Apply)->click();
        QVERIFY(activity.items().isEmpty());
        RecentActivity reopened(config);
        QVERIFY(reopened.items().isEmpty());
    }
    void viewedResultEventsAndRevisitSurviveTabDeletion()
    {
        ClosingWindow window;
        window.performSearch(QStringLiteral("lookup"));
        auto *tabs = window.findChild<QTabWidget *>();
        auto *tab = qobject_cast<SearchTab *>(tabs->currentWidget());
        QVERIFY(tab);
        SearchResult result;
        result.id = QStringLiteral("stable-id");
        result.title = QStringLiteral("Viewed title");
        result.description = QStringLiteral("<img src='file:///private'> untrusted tooltip");
        result.url = QStringLiteral("https://example.org/source");
        tab->addResults(QStringLiteral("Friendly name"), {result}, QStringLiteral("stable-provider"));
        auto *tree = tab->findChild<QTreeWidget *>();
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        auto *activity = window.findChild<RecentActivity *>();
        QCOMPARE(activity->items().first().kind, RecentItem::Kind::Viewed);
        QCOMPARE(activity->items().first().source, QStringLiteral("stable-provider"));
        QVERIFY(!window.isVisible()); // recording a viewed event never raises the window
        tabs->removeTab(tabs->indexOf(tab));
        delete tab;
        auto *dashboard = window.findChild<SearchDashboard *>();
        auto *list = dashboard->findChild<QListWidget *>(QStringLiteral("recentActivityList"));
        QVERIFY(!list->item(0)->toolTip().contains(QStringLiteral("<img")));
        QVERIFY(list->item(0)->toolTip().contains(QStringLiteral("&lt;img")));
        list->setCurrentRow(0);
        opened.clear();
        QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "openSource");
        dashboard->findChild<QPushButton *>(QStringLiteral("openRecentItem"))->click();
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
        QCOMPARE(opened, QList<QUrl>{QUrl(result.url)});
    }
    void automaticQueriesCannotBypassPrivacyThroughSessionTabs()
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
        KConfigGroup(config, QStringLiteral("General")).writeEntry(QStringLiteral("MinimizeToTray"), false);
        {
            ClosingWindow window;
            window.processClipboardText(QStringLiteral("dummy-private-automatic-query"), true);
            window.performSearch(QStringLiteral("deliberate query"));
            auto *activity = window.findChild<RecentActivity *>();
            QCOMPARE(activity->items().size(), 1);
            auto *dashboard = window.findChild<SearchDashboard *>();
            QVERIFY(dashboard->saveHistory().isEmpty()); // existing autocomplete semantics
            QVERIFY(window.queryClose());
        }
        config->sync();
        QFile file(configHome.path() + QStringLiteral("/klookuprc"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("dummy-private"));
        {
            ClosingWindow restored;
            auto *activity = restored.findChild<RecentActivity *>();
            QCOMPARE(activity->items().size(), 1); // restore doesn't create a new event
            activity->clear();
            QVERIFY(restored.queryClose());
        }
        ClosingWindow afterClear;
        QVERIFY(afterClear.findChild<RecentActivity *>()->items().isEmpty());
    }
};
QTEST_MAIN(RecentActivityTest)
#include "test_recentactivity.moc"
