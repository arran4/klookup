#include "../src/resultactions.h"
#include "../src/searchtab.h"
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QMenu>
#include <QMimeData>
#include <QtTest>

class UrlReceiver : public QObject
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

class ResultActionsTest : public QObject
{
    Q_OBJECT
private slots:
    void targetSelectionAndClipboard()
    {
        SearchTab tab(QStringLiteral("query"));
        SearchResult first;
        first.id = QStringLiteral("one");
        first.title = QStringLiteral("First");
        first.url = QStringLiteral("https://example.org/one");
        SearchResult second;
        second.id = QStringLiteral("two");
        second.title = QStringLiteral("Second");
        second.url = QStringLiteral("https://example.org/two?q=encoded%20value");
        tab.addResults(QStringLiteral("Provider"), {first, second});
        auto *tree = tab.findChild<QTreeWidget *>();
        auto *group = tree->topLevelItem(0);
        tree->setCurrentItem(group->child(0));
        auto *menu = tab.createResultMenu(group->child(1));
        QVERIFY(menu);
        QCOMPARE(tree->currentItem(), group->child(1));
        QVERIFY(!menu->findChild<QAction *>(QStringLiteral("refresh_details"))->isEnabled());
        auto *copy = menu->findChild<QAction *>(QStringLiteral("copy_url"));
        QVERIFY(copy->isEnabled());
        copy->trigger();
        QCOMPARE(QGuiApplication::clipboard()->text(), second.url);
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasFormat(QString::fromLatin1(SourceUrlClipboardMime)));
        menu->findChild<QAction *>(QStringLiteral("copy_title"))->trigger();
        QCOMPARE(QGuiApplication::clipboard()->text(), second.title);
        QVERIFY(!tab.createResultMenu(group));
        QVERIFY(!tab.createResultMenu(nullptr));
    }
    void openCallbackAndInvalidUrls()
    {
        UrlReceiver receiver;
        QDesktopServices::setUrlHandler(QStringLiteral("https"), &receiver, "receive");
        SearchResult result;
        result.title = QStringLiteral("Title");
        result.url = QStringLiteral("https://example.org/source");
        ResultActions commands(result, true);
        QSignalSpy refresh(&commands, &ResultActions::refreshRequested);
        commands.openUrl->trigger();
        QCOMPARE(receiver.received, QUrl(result.url));
        commands.refresh->trigger();
        QCOMPARE(refresh.count(), 1);
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
        for (const auto &url : {QString(), QStringLiteral("file:///etc/passwd"), QStringLiteral("javascript:alert(1)"),
                                QStringLiteral("https://user:secret@example.org/"), QStringLiteral("https://")}) {
            result.url = url;
            ResultActions invalid(result, false);
            QVERIFY(!invalid.copyUrl->isEnabled());
            QVERIFY(!invalid.openUrl->isEnabled());
            QVERIFY(!invalid.refresh->isEnabled());
            QVERIFY(invalid.copyTitle->isEnabled());
        }
    }
    void mouseAndKeyboardContextEventsTargetRows()
    {
        SearchTab tab(QStringLiteral("query"));
        SearchResult first;
        first.title = QStringLiteral("First");
        SearchResult second;
        second.title = QStringLiteral("Second");
        tab.addResults(QStringLiteral("Provider"), {first, second});
        tab.resize(800, 600);
        tab.show();
        auto *tree = tab.findChild<QTreeWidget *>();
        auto *group = tree->topLevelItem(0);
        tree->setCurrentItem(group->child(0));
        const QPoint position = tree->visualItemRect(group->child(1)).center();
        QContextMenuEvent mouse(QContextMenuEvent::Mouse, position, tree->viewport()->mapToGlobal(position));
        QCoreApplication::sendEvent(tree->viewport(), &mouse);
        QCOMPARE(tree->currentItem(), group->child(1));
        auto *menu = tab.findChild<QMenu *>(QStringLiteral("resultContextMenu"));
        QVERIFY(menu && menu->isVisible());
        menu->hide();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->setCurrentItem(group->child(0));
        QContextMenuEvent keyboard(QContextMenuEvent::Keyboard, QPoint(-1, -1));
        QCoreApplication::sendEvent(tree, &keyboard);
        menu = tab.findChild<QMenu *>(QStringLiteral("resultContextMenu"));
        QVERIFY(menu && menu->isVisible());
        menu->findChild<QAction *>(QStringLiteral("copy_title"))->trigger();
        QCOMPARE(QGuiApplication::clipboard()->text(), first.title);
        menu->hide();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!tab.createResultMenu(group));
        tab.setError(QStringLiteral("Other"), QStringLiteral("error"));
        QVERIFY(!tab.createResultMenu(tree->topLevelItem(1)->child(0)));
    }
};
QTEST_MAIN(ResultActionsTest)
#include "test_resultactions.moc"
