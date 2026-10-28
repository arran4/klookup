#include "../src/globalshortcuts.h"
#include <QtTest>

class FakeGlobalShortcuts : public GlobalShortcuts
{
public:
    using GlobalShortcuts::GlobalShortcuts;
    bool available = false;
    QStringList attempts;
    QString failure;

protected:
    bool serviceAvailable() const override
    {
        return available;
    }
    bool registerAction(QAction *action) override
    {
        attempts.append(action->objectName());
        return action->objectName() != failure;
    }
};

class GlobalShortcutsTest : public QObject
{
    Q_OBJECT
private slots:
    void unavailableFailedAndReconnect()
    {
        QAction show;
        show.setObjectName(QStringLiteral("show_search"));
        QAction clipboard;
        clipboard.setObjectName(QStringLiteral("search_clipboard"));
        FakeGlobalShortcuts shortcuts({&show, &clipboard});
        shortcuts.refresh();
        QCOMPARE(shortcuts.status(), GlobalShortcuts::Status::Unavailable);
        QVERIFY(shortcuts.attempts.isEmpty());
        shortcuts.available = true;
        shortcuts.failure = clipboard.objectName();
        shortcuts.refresh();
        QCOMPARE(shortcuts.status(), GlobalShortcuts::Status::Failed);
        QCOMPARE(shortcuts.attempts, QStringList({show.objectName(), clipboard.objectName()}));
        QVERIFY(show.isEnabled() && clipboard.isEnabled());
        shortcuts.failure.clear();
        shortcuts.refresh();
        QCOMPARE(shortcuts.status(), GlobalShortcuts::Status::Registered);
        shortcuts.available = false;
        shortcuts.refresh();
        QCOMPARE(shortcuts.status(), GlobalShortcuts::Status::Unavailable);
        shortcuts.available = true;
        shortcuts.refresh();
        QCOMPARE(shortcuts.status(), GlobalShortcuts::Status::Registered);
    }
    void destroyedActionsAreIgnored()
    {
        auto *action = new QAction;
        action->setObjectName(QStringLiteral("temporary"));
        FakeGlobalShortcuts shortcuts({action});
        delete action;
        shortcuts.available = true;
        shortcuts.refresh();
        QVERIFY(shortcuts.attempts.isEmpty());
    }
    void headlessRegistrationIsPassive()
    {
        QAction action;
        action.setObjectName(QStringLiteral("show_search"));
        GlobalShortcuts shortcuts({&action});
        shortcuts.refresh();
        if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
            QCOMPARE(shortcuts.status(), GlobalShortcuts::Status::Unavailable);
        QVERIFY(action.isEnabled());
        QVERIFY(action.shortcuts().isEmpty());
    }
};
QTEST_MAIN(GlobalShortcutsTest)
#include "test_globalshortcuts.moc"
