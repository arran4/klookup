// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchtab.h"
#include "sourcelink.h"

#include <QTest>
#include <QTextBrowser>
#include <QTreeWidget>

class TestMangaBakaListTitles : public QObject
{
    Q_OBJECT

private slots:
    void testSameTitleDifferentFormatsAndIdentity()
    {
        SearchTab tab(QStringLiteral("Example Series"));
        auto *tree = tab.findChild<QTreeWidget *>(QStringLiteral("resultsTree"));
        auto *details = tab.findChild<QTextBrowser *>(QStringLiteral("detailsView"));
        QVERIFY(tree != nullptr);
        QVERIFY(details != nullptr);

        SearchResult manga;
        manga.id = QStringLiteral("101");
        manga.title = QStringLiteral("Example Series");
        manga.description = QStringLiteral("Manga adaptation description.");
        manga.url = QStringLiteral("https://mangabaka.dev/101");
        manga.type = QStringLiteral("Manga");

        SearchResult novel;
        novel.id = QStringLiteral("102");
        novel.title = QStringLiteral("Example Series");
        novel.description = QStringLiteral("Original novel description.");
        novel.url = QStringLiteral("https://mangabaka.dev/102");
        novel.type = QStringLiteral("Novel");

        SearchResult missingType;
        missingType.id = QStringLiteral("103");
        missingType.title = QStringLiteral("Example Series");

        SearchResult unknownType;
        unknownType.id = QStringLiteral("104");
        unknownType.title = QStringLiteral("Example Series");
        unknownType.type = QStringLiteral("Webtoon");

        tab.addResults(QStringLiteral("MangaBaka"), {manga, novel, missingType, unknownType}, QStringLiteral("mangabaka"));

        SearchResult wiki;
        wiki.id = QStringLiteral("201");
        wiki.title = QStringLiteral("Example Series");
        wiki.type = QStringLiteral("Article");
        tab.addResults(QStringLiteral("Wikipedia"), {wiki}, QStringLiteral("wikipedia"));

        QCOMPARE(tree->topLevelItemCount(), 2);
        auto *mbGroup = tree->topLevelItem(0);
        QCOMPARE(mbGroup->childCount(), 4);
        QCOMPARE(mbGroup->child(0)->text(0), QStringLiteral("Example Series (Manga)"));
        QCOMPARE(mbGroup->child(1)->text(0), QStringLiteral("Example Series (Novel)"));
        QCOMPARE(mbGroup->child(2)->text(0), QStringLiteral("Example Series"));
        QCOMPARE(mbGroup->child(3)->text(0), QStringLiteral("Example Series (Webtoon)"));

        auto *wikiGroup = tree->topLevelItem(1);
        QCOMPARE(wikiGroup->childCount(), 1);
        QCOMPARE(wikiGroup->child(0)->text(0), QStringLiteral("Example Series"));

        // Clicking each entry opens its corresponding distinct record
        tree->setCurrentItem(mbGroup->child(0));
        QMetaObject::invokeMethod(&tab, "onResultClicked", Q_ARG(QTreeWidgetItem *, mbGroup->child(0)), Q_ARG(int, 0));
        QVERIFY(details->toPlainText().contains(QStringLiteral("Manga adaptation description.")));

        tree->setCurrentItem(mbGroup->child(1));
        QMetaObject::invokeMethod(&tab, "onResultClicked", Q_ARG(QTreeWidgetItem *, mbGroup->child(1)), Q_ARG(int, 0));
        QVERIFY(details->toPlainText().contains(QStringLiteral("Original novel description.")));
    }
};

QTEST_MAIN(TestMangaBakaListTitles)
#include "test_mangabaka_list_titles.moc"
