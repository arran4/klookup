#include "../src/clipboardpolicy.h"
#include <KConfig>
#include <KConfigGroup>
#include <QTemporaryDir>
#include <QtTest>

class ClipboardPolicyTest : public QObject
{
    Q_OBJECT
private slots:
    void captureAndTriggerAreIndependent()
    {
        using namespace ClipboardPolicy;
        for (int capture = 0; capture <= int(Capture::Running); ++capture) {
            for (bool visible : {false, true})
                for (bool focused : {false, true}) {
                    Policy policy{Capture(capture), Trigger::ClipboardChanged};
                    const bool expected = capture == int(Capture::Running) || (visible && capture == int(Capture::Visible)) ||
                                          (visible && focused && capture == int(Capture::Focused));
                    QCOMPARE(policy.permitsCapture(visible, focused), expected);
                }
        }
        for (int trigger = 0; trigger <= int(Trigger::GlobalShortcut); ++trigger) {
            Policy policy{Capture::Running, Trigger(trigger)};
            for (int event = 0; event <= int(Event::GlobalShortcut); ++event)
                QCOMPARE(policy.searchesOn(Event(event)), trigger == event + 1);
        }
    }
    void migrationAndPersistence()
    {
        using namespace ClipboardPolicy;
        QTemporaryDir root;
        QVERIFY(root.isValid());
        KConfig config(root.filePath(QStringLiteral("policyrc")), KConfig::SimpleConfig);
        for (bool monitor : {false, true})
            for (bool focused : {false, true})
                for (bool automatic : {false, true}) {
                    KConfigGroup group(&config, QStringLiteral("Policy"));
                    group.deleteGroup();
                    group.writeEntry("MonitorClipboard", monitor);
                    group.writeEntry("ClipboardOnlyWhenFocused", focused);
                    group.writeEntry("AutoSearchClipboard", automatic);
                    const auto before = read(group);
                    QCOMPARE(before.capture, monitor ? (focused ? Capture::Focused : Capture::Running) : Capture::Never);
                    QCOMPARE(before.trigger, monitor && automatic ? Trigger::ClipboardChanged : Trigger::Never);
                    QVERIFY(!group.hasKey("ClipboardCaptureMode")); // reading Settings is transactional
                    migrate(group);
                    QCOMPARE(read(group).capture, before.capture);
                    QCOMPARE(read(group).trigger, before.trigger);
                    group.writeEntry("MonitorClipboard", !monitor);
                    QCOMPARE(read(group).capture, before.capture); // explicit new policy wins
                }
        KConfigGroup group(&config, QStringLiteral("Saved"));
        write(group, {Capture::Visible, Trigger::TrayOpened});
        config.sync();
        KConfig restored(root.filePath(QStringLiteral("policyrc")), KConfig::SimpleConfig);
        KConfigGroup saved(&restored, QStringLiteral("Saved"));
        QCOMPARE(read(saved).capture, Capture::Visible);
        QCOMPARE(read(saved).trigger, Trigger::TrayOpened);
        saved.writeEntry("ClipboardCaptureMode", 999);
        saved.writeEntry("ClipboardSearchTrigger", -1);
        QCOMPARE(read(saved).capture, Capture::Never);
        QCOMPARE(read(saved).trigger, Trigger::Never);
    }
};
QTEST_GUILESS_MAIN(ClipboardPolicyTest)
#include "test_clipboardpolicy.moc"
