#include "../src/clipboardinput.h"
#include <QMimeData>
#include <QtTest>

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
            {QStringLiteral("Search this https://example.org/page"), Kind::SearchText},
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
            {QStringLiteral("Some prose\nhttps://example.org/reference\nMore prose"), Kind::SearchText},
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
            QVERIFY(result.text.isEmpty()); // rejected input cannot reach capture/history
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
QTEST_GUILESS_MAIN(ClipboardInputTest)
#include "test_clipboardinput.moc"
