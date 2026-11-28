// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/clipboardinput.h"
#include "../src/clipboardpolicy.h"
#include <KConfig>
#include <KConfigGroup>
#include <QCoreApplication>
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
                    QVERIFY(!group.hasKey("ClipboardCaptureMode"));
                    migrate(group);
                    QCOMPARE(read(group).capture, before.capture);
                    QCOMPARE(read(group).trigger, before.trigger);
                    group.writeEntry("MonitorClipboard", !monitor);
                    QCOMPARE(read(group).capture, before.capture);
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

class ClipboardInputTest : public QObject
{
    Q_OBJECT
private slots:
    void classification_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("kind");
        using Kind = ClipboardInput::Kind;
        const QList<QPair<QString, Kind>> cases = {
            {QString(), Kind::Empty},
            {QStringLiteral(" \t\r\n"), Kind::Empty},
            {QStringLiteral("https://example.org/path?q=hello#section"), Kind::Url},
            {QStringLiteral(" HTTP://EXAMPLE.COM \n"), Kind::Url},
            {QStringLiteral("https://"), Kind::Url},
            {QStringLiteral("file:///home/user/private"), Kind::Url},
            {QStringLiteral("mailto:person@example.org"), Kind::Url},
            {QStringLiteral("magnet:?xt=urn:btih:123"), Kind::Url},
            {QStringLiteral("tel:+123456789"), Kind::Url},
            {QStringLiteral("custom-app://object/1"), Kind::Url},
            {QStringLiteral("javascript:alert(1)"), Kind::Url},
            {QStringLiteral("data:text/plain,secret"), Kind::Url},
            {QStringLiteral("urn:isbn:9780140328721"), Kind::Url},
            {QStringLiteral("www.example.newtld"), Kind::WebAddress},
            {QStringLiteral("example.com"), Kind::WebAddress},
            {QStringLiteral("example.co.uk/path"), Kind::WebAddress},
            {QStringLiteral("例え.テスト/path"), Kind::WebAddress},
            {QStringLiteral("127.0.0.1:8000/path"), Kind::WebAddress},
            {QStringLiteral("localhost:8080"), Kind::WebAddress},
            {QStringLiteral("[::1]:8080/path"), Kind::WebAddress},
            {QStringLiteral("https://example.org/one\nhttps://example.org/two"), Kind::Unsupported},
            {QStringLiteral("person@example.org"), Kind::Unsupported},
            {QStringLiteral("/home/user/document"), Kind::Unsupported},
            {QStringLiteral("~/private"), Kind::Unsupported},
            {QStringLiteral("C:\\Users\\Private"), Kind::Unsupported},
            {QStringLiteral("./local/path"), Kind::Unsupported},
            {QStringLiteral("Search this https://example.org/page"), Kind::Unsupported},
            {QStringLiteral("Python 3.12"), Kind::SearchText},
            {QStringLiteral("1.2.3.4"), Kind::SearchText},
            {QStringLiteral("ISBN:978-0-140-32872-1"), Kind::SearchText},
            {QStringLiteral("9780140328721"), Kind::SearchText},
            {QStringLiteral("Dr.Smith"), Kind::SearchText},
            {QStringLiteral("QtCore.QString"), Kind::SearchText},
            {QStringLiteral("readme.md"), Kind::SearchText},
            {QStringLiteral("C++:templates"), Kind::SearchText},
            {QStringLiteral("夏目漱石"), Kind::SearchText},
            {QStringLiteral("  café lookup  "), Kind::SearchText},
            {QStringLiteral("Some prose\nhttps://example.org/reference\nMore prose"), Kind::Unsupported},
            {QStringLiteral("\n\nOne piece\n\n"), Kind::SearchText},
            {QStringLiteral("Email block\nFrom: user@example.com\nVisit https://example.org"), Kind::Unsupported},
            {QStringLiteral("Contact user@example.com for info"), Kind::Unsupported},
            {QStringLiteral("Check out www.example.com today"), Kind::Unsupported},
            {QStringLiteral("multi\nline\ntext"), Kind::Unsupported},
            {QString(ClipboardInput::MaximumLength, QLatin1Char('a')), Kind::SearchText},
            {QString(ClipboardInput::MaximumLength + 1, QLatin1Char('a')), Kind::Unsupported},
            {QStringLiteral("binary") + QChar(0) + QStringLiteral("data"), Kind::Unsupported},
            {QStringLiteral("control") + QChar(7), Kind::Unsupported}};
        for (int i = 0; i < cases.size(); ++i)
            QTest::newRow(qPrintable(QString::number(i))) << cases[i].first << int(cases[i].second);
    }

    void classification()
    {
        QFETCH(QString, text);
        QFETCH(int, kind);
        const auto result = ClipboardInput::classifyText(text);
        QCOMPARE(int(result.kind), kind);
        QCOMPARE(result.permitsAutomaticCapture(), result.kind == ClipboardInput::Kind::SearchText);
        if (result.permitsAutomaticCapture())
            QCOMPARE(result.text, text.trimmed());
        else
            QVERIFY(result.text.isEmpty());
    }

    void deliberateClipboardSearchIsSeparate()
    {
        QMimeData mime;
        mime.setText(QStringLiteral("https://example.org/explicit"));
        QVERIFY(!ClipboardInput::classifyMimeData(&mime).permitsAutomaticCapture());
        QCOMPARE(ClipboardInput::explicitQuery(&mime), QStringLiteral("https://example.org/explicit"));
        mime.setText(QStringLiteral("/an/intentional/path/query"));
        QCOMPARE(ClipboardInput::explicitQuery(&mime), QStringLiteral("/an/intentional/path/query"));
        mime.setText(QStringLiteral("\n\nOne Piece\n\n"));
        QCOMPARE(ClipboardInput::explicitQuery(&mime), QStringLiteral("One Piece"));
        mime.setText(QStringLiteral("line1\nline2"));
        QVERIFY(ClipboardInput::explicitQuery(&mime).isEmpty());
        mime.setText(QString(ClipboardInput::MaximumLength + 1, QLatin1Char('x')));
        QVERIFY(ClipboardInput::explicitQuery(&mime).isEmpty());
        mime.clear();
        mime.setHtml(QStringLiteral("<p>HTML only</p>"));
        QVERIFY(ClipboardInput::explicitQuery(&mime).isEmpty());
    }

    void mimeData()
    {
        QMimeData mime;
        mime.setHtml(QStringLiteral("<p>HTML only</p>"));
        QVERIFY(!ClipboardInput::classifyMimeData(&mime).permitsAutomaticCapture());
        mime.setText(QStringLiteral("ordinary query"));
        QVERIFY(ClipboardInput::classifyMimeData(&mime).permitsAutomaticCapture());
        mime.setUrls({QUrl(QStringLiteral("https://example.org"))});
        mime.setText(QStringLiteral("https://example.org"));
        QVERIFY(!ClipboardInput::classifyMimeData(&mime).permitsAutomaticCapture());
        mime.clear();
        mime.setData(QStringLiteral("image/png"), "binary");
        QVERIFY(!ClipboardInput::classifyMimeData(&mime).permitsAutomaticCapture());
        mime.setData(QStringLiteral("text/plain"), QByteArray("a\0b", 3));
        QVERIFY(!ClipboardInput::classifyMimeData(&mime).permitsAutomaticCapture());
        QVERIFY(!ClipboardInput::classifyMimeData(nullptr).permitsAutomaticCapture());
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ClipboardPolicyTest policyTest;
    ClipboardInputTest inputTest;
    return QTest::qExec(&policyTest, argc, argv) | QTest::qExec(&inputTest, argc, argv);
}

#include "test_clipboard_unit.moc"
