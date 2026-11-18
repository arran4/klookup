// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "searchtab.h"
#include "searchwindow.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KGlobalAccel>
#include <KSharedConfig>
#include <QAction>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

class TestKeyboardNavigation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void testLocalPopOutShortcutAndDashboardDefaults()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());

        MainWindow main;
        auto *popOut = main.actionCollection()->action(QStringLiteral("pop_out_search_tab"));
        QVERIFY(popOut != nullptr);
        QVERIFY(!popOut->isEnabled());
        QCOMPARE(popOut->shortcutContext(), Qt::WindowShortcut);
        QVERIFY(main.actionCollection()->defaultShortcuts(popOut).contains(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return)));
        QVERIFY(main.actionCollection()->defaultShortcuts(popOut).contains(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Enter)));
        QVERIFY(!popOut->text().contains(QStringLiteral("new query window"), Qt::CaseInsensitive));

        // Retain #169 local shortcuts
        QVERIFY(main.actionCollection()->defaultShortcuts(main.actionCollection()->action(QStringLiteral("close_current_tab"))).contains(QKeySequence(Qt::CTRL | Qt::Key_W)));
        QVERIFY(main.actionCollection()->defaultShortcuts(main.actionCollection()->action(QStringLiteral("new_search"))).contains(QKeySequence(Qt::CTRL | Qt::Key_T)));
        QVERIFY(main.actionCollection()->defaultShortcuts(main.actionCollection()->action(QStringLiteral("focus_search"))).contains(QKeySequence(Qt::CTRL | Qt::Key_L)));
        QVERIFY(main.actionCollection()->defaultShortcuts(main.actionCollection()->action(QStringLiteral("next_tab"))).contains(QKeySequence(Qt::CTRL | Qt::Key_Tab)));
        QVERIFY(main.actionCollection()->defaultShortcuts(main.actionCollection()->action(QStringLiteral("previous_tab"))).contains(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab)));

        main.performSearch(QStringLiteral("shortcut query"));
        auto *tabs = qobject_cast<QTabWidget *>(main.centralWidget());
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), 2);
        QVERIFY(popOut->isEnabled());

        popOut->trigger();
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(main.searchWindows().size(), 1);
        QVERIFY(!popOut->isEnabled());
        main.searchWindows().first()->close();
    }

    void testGlobalShortcutsStayOptInWithDiscoverableHints()
    {
        MainWindow main;
        auto *show = main.actionCollection()->action(QStringLiteral("show_search"));
        auto *clipboard = main.actionCollection()->action(QStringLiteral("search_clipboard"));
        QVERIFY(show != nullptr);
        QVERIFY(clipboard != nullptr);

        // Global shortcuts must remain empty/opt-in by default
        QVERIFY(show->shortcuts().isEmpty());
        QVERIFY(clipboard->shortcuts().isEmpty());
        QVERIFY(KGlobalAccel::self()->shortcut(show).isEmpty());
        QVERIFY(KGlobalAccel::self()->shortcut(clipboard).isEmpty());

        // Discoverable suggested hints
        QCOMPARE(show->property("klookupSuggestedGlobalShortcut").toString(), QStringLiteral("Meta+Alt+K"));
        QCOMPARE(clipboard->property("klookupSuggestedGlobalShortcut").toString(), QStringLiteral("Meta+Alt+C"));
        QVERIFY(show->toolTip().contains(QStringLiteral("Meta+Alt+K")));
        QVERIFY(clipboard->toolTip().contains(QStringLiteral("Meta+Alt+C")));
        QVERIFY(!show->whatsThis().isEmpty());
        QVERIFY(!clipboard->whatsThis().isEmpty());
    }

    void testCustomUserShortcutOverridePreservedAcrossRestart()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());

        {
            MainWindow first;
            auto *focus = first.actionCollection()->action(QStringLiteral("focus_search"));
            QVERIFY(focus != nullptr);
            focus->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
            KConfigGroup shortcuts(KSharedConfig::openConfig(), QStringLiteral("Shortcuts"));
            first.actionCollection()->writeSettings(&shortcuts);
            KSharedConfig::openConfig()->sync();
        }

        {
            MainWindow second;
            auto *focus = second.actionCollection()->action(QStringLiteral("focus_search"));
            QVERIFY(focus != nullptr);
            QCOMPARE(focus->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_K));
        }
    }
};

QTEST_MAIN(TestKeyboardNavigation)
#include "test_keyboard_navigation.moc"
