#include "../src/searchtab.h"
#include "../src/sourcelink.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QMimeData>
#include <QMouseEvent>
#include <QtTest>

class SourceUrlReceiver : public QObject
{
    Q_OBJECT
public:
    QUrl received;
public slots:
    void receive(const QUrl &url)
    {
        received = url;
    }
};

class TestSourceLink : public SourceLink
{
public:
    QList<QUrl> dragged;
    QString text;
    int drags = 0;

protected:
    void executeDrag(QMimeData *mime) override
    {
        ++drags;
        if (mime) {
            dragged = mime->urls();
            text = mime->text();
            delete mime;
        }
    }
};

class SourceLinkTest : public QObject
{
    Q_OBJECT
    SourceUrlReceiver receiver;
    static SearchResult result(const QString &url)
    {
        SearchResult result;
        result.title = QStringLiteral("Source title");
        result.url = url;
        return result;
    }
private slots:
    void init()
    {
        receiver.received = QUrl();
        QDesktopServices::setUrlHandler(QStringLiteral("https"), &receiver, "receive");
    }
    void cleanup()
    {
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
    }
    void mimeAndPolicy()
    {
        TestSourceLink link;
        const auto source = result(QStringLiteral("https://example.org/source?q=one%20two#chapter"));
        link.setResult(source);
        QScopedPointer<QMimeData> mime(link.mimeData());
        QVERIFY(mime);
        QCOMPARE(mime->urls(), QList<QUrl>{QUrl(source.url)});
        QCOMPARE(mime->text(), source.url);
        QVERIFY(mime->hasFormat(QStringLiteral("text/uri-list")));
        for (const auto &invalid : {QString(), QStringLiteral("file:///etc/passwd"), QStringLiteral("javascript:alert(1)"),
                                    QStringLiteral("https://user:token@example.org/"), QStringLiteral("https://")}) {
            link.setResult(result(invalid));
            QVERIFY(link.isHidden());
            QVERIFY(!link.isEnabled());
            QVERIFY(!link.mimeData());
        }
    }
    void clickAndDragThreshold()
    {
        TestSourceLink link;
        const QUrl url(QStringLiteral("https://example.org/source"));
        link.setResult(result(url.toString()));
        link.resize(240, 40);
        link.show();
        QTest::mouseClick(&link, Qt::LeftButton);
        QCOMPARE(receiver.received, url);
        receiver.received = QUrl();
        const QPoint origin(20, 20);
        QTest::mousePress(&link, Qt::LeftButton, {}, origin);
        auto move = [&](int distance) {
            const QPoint point = origin + QPoint(distance, 0);
            QMouseEvent event(QEvent::MouseMove, QPointF(point), QPointF(link.mapToGlobal(point)), Qt::NoButton, Qt::LeftButton,
                              Qt::NoModifier);
            QCoreApplication::sendEvent(&link, &event);
        };
        move(QApplication::startDragDistance() - 1);
        QCOMPARE(link.drags, 0);
        move(QApplication::startDragDistance() + 1);
        QCOMPARE(link.drags, 1);
        QTest::mouseRelease(&link, Qt::LeftButton, {}, origin);
        QVERIFY(receiver.received.isEmpty());
        QCOMPARE(link.dragged, QList<QUrl>{url});
        QCOMPARE(link.text, url.toString(QUrl::FullyEncoded));
        QTest::mouseClick(&link, Qt::LeftButton);
        QCOMPARE(receiver.received, url); // drag does not break subsequent clicks
    }
    void pressRetainsOriginalSource()
    {
        TestSourceLink link;
        const QUrl original(QStringLiteral("https://example.org/original"));
        link.setResult(result(original.toString()));
        link.resize(240, 40);
        link.show();
        const QPoint origin(20, 20);
        QTest::mousePress(&link, Qt::LeftButton, {}, origin);
        link.setResult(result(QStringLiteral("https://example.org/updated")));
        const QPoint point = origin + QPoint(QApplication::startDragDistance() + 1, 0);
        QMouseEvent move(QEvent::MouseMove, QPointF(point), QPointF(link.mapToGlobal(point)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&link, &move);
        QCOMPARE(link.dragged, QList<QUrl>{original});
        QTest::mouseRelease(&link, Qt::LeftButton, {}, origin);
        QVERIFY(receiver.received.isEmpty());
    }
    void tabSelectionAndCopy()
    {
        SearchTab tab(QStringLiteral("query"));
        const auto source = result(QStringLiteral("https://example.org/selected"));
        tab.addResults(QStringLiteral("Provider"), {source});
        auto *tree = tab.findChild<QTreeWidget *>();
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        auto *link = tab.findChild<SourceLink *>();
        QVERIFY(link);
        QVERIFY(!link->isHidden());
        tab.show();
        tab.activateWindow();
        QCoreApplication::processEvents();
        link->setFocus();
        QTRY_VERIFY(link->hasFocus());
        QTest::keyClick(link, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(), source.url);
        tree->setCurrentItem(tree->topLevelItem(0));
        QVERIFY(link->isHidden());
        QVERIFY(!link->mimeData());
    }
};
QTEST_MAIN(SourceLinkTest)
#include "test_sourcelink.moc"
