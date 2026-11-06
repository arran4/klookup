// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "isolatedguitest.h"
#include "languagepreferences.h"
#include "searchtab.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QDialog>
class LanguagesTest : public QObject {
    Q_OBJECT
private slots:
    void canonicalCodes_data() {
        QTest::addColumn<QString>("input"); QTest::addColumn<QString>("expected");
        QTest::newRow("english") << QStringLiteral("EN_us") << QStringLiteral("en-US");
        QTest::newRow("script") << QStringLiteral("ZH_hANT_tw") << QStringLiteral("zh-Hant-TW");
        QTest::newRow("iso639-3") << QStringLiteral("jpn") << QStringLiteral("ja");
        QTest::newRow("deprecated") << QStringLiteral("iw-IL") << QStringLiteral("he-IL");
        QTest::newRow("variant") << QStringLiteral("de-CH-1901") << QStringLiteral("de-CH-1901");
        QTest::newRow("extension") << QStringLiteral("en-US-u-ca-gregory") << QStringLiteral("en-US-u-ca-gregory");
        QTest::newRow("private-use") << QStringLiteral("en-x-test") << QStringLiteral("en-x-test");
        for (const auto &invalid : QStringList{QString(), QStringLiteral("ja--JP"), QStringLiteral("<script>"), QStringLiteral("English text"), QStringLiteral("en-US-u"), QStringLiteral("en-123456789"), QString(100, QLatin1Char('a'))})
            QTest::newRow(qPrintable(QStringLiteral("invalid-") + invalid)) << invalid << QString();
    }
    void canonicalCodes() { QFETCH(QString, input); QFETCH(QString, expected); QCOMPARE(Languages::canonicalTag(input), expected); }
    void mappingsMatchingFallbackAndNoGuessedOriginal() {
        QCOMPARE(Languages::providerCode(QStringLiteral("openlibrary"), QStringLiteral("/languages/eng")), QStringLiteral("en"));
        QVERIFY(Languages::providerCode(QStringLiteral("other"), QStringLiteral("/languages/eng")).isEmpty());
        QCOMPARE(Languages::defaults(QLocale(QStringLiteral("en_AU"))), QStringList{QStringLiteral("en")});
        QCOMPARE(Languages::normalized({QStringLiteral("en"), QStringLiteral("eng"), QStringLiteral("JA"), QStringLiteral("jpn")}), (QStringList{QStringLiteral("en"), QStringLiteral("ja")}));
        const QList<Languages::Title> titles{{QStringLiteral("en-US"), QStringLiteral("US title")}, {QStringLiteral("en-GB"), QStringLiteral("UK title")},
            {QStringLiteral("ja"), QStringLiteral("原題"), true}, {QStringLiteral("ar"), QStringLiteral("العنوان")}, {QStringLiteral("zh-Hans"), QStringLiteral("简体")}};
        QCOMPARE(Languages::select(titles, {QStringLiteral("en-GB")}, QStringLiteral("ja")).preferred.text, QStringLiteral("UK title"));
        QCOMPARE(Languages::select(titles, {QStringLiteral("fr")}, QStringLiteral("ja")).preferred.text, QStringLiteral("原題"));
        QCOMPARE(Languages::select(titles, {QStringLiteral("zh-Hant")}, QStringLiteral("ja")).preferred.text, QStringLiteral("原題"));
        QCOMPARE(Languages::select(titles, {QStringLiteral("ar")}).preferred.text, QStringLiteral("العنوان"));
        QVERIFY(Languages::select({{QStringLiteral("en"), QStringLiteral("Only known title")}}, {}).original.text.isEmpty());
        auto serialized = Languages::toData(titles); auto restored = Languages::fromData(serialized);
        QCOMPARE(Languages::select(restored, {QStringLiteral("en-GB")}, QStringLiteral("ja")).preferred.text, QStringLiteral("UK title"));
    }
    void orderedWidgetPersistenceAndReset() {
        KConfigGroup settings(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Languages")); settings.deleteGroup(); settings.sync();
        LanguagePreferencesWidget widget;
        auto *list = widget.findChild<QListWidget *>(QStringLiteral("preferredLanguages")); QVERIFY(list);
        auto *entry = widget.findChild<QLineEdit *>(QStringLiteral("languageCode")); QVERIFY(entry);
        entry->setText(QStringLiteral("ja")); widget.findChild<QPushButton *>(QStringLiteral("addPreferredLanguage"))->click();
        list->setCurrentRow(list->count() - 1); widget.findChild<QPushButton *>(QStringLiteral("languageUp"))->click(); widget.saveSettings();
        QCOMPARE(Languages::read(settings).first(), QStringLiteral("ja"));
        LanguagePreferencesWidget restarted;
        auto *restored = restarted.findChild<QListWidget *>(QStringLiteral("preferredLanguages"));
        QCOMPARE(restored->item(0)->data(Qt::UserRole).toString(), QStringLiteral("ja"));
        restarted.findChild<QPushButton *>(QStringLiteral("resetLanguages"))->click(); restarted.saveSettings();
        QVERIFY(!settings.hasKey(QStringLiteral("Preferred")));
    }
    void allTitlesActionIsContextualAndPlainText() {
        SearchTab tab(QStringLiteral("fixture"));
        SearchResult result; result.id = QStringLiteral("titles"); result.title = QStringLiteral("Preferred");
        result.additionalData.insert(QStringLiteral("localizedTitles"), Languages::toData({{QStringLiteral("en"), QStringLiteral("English")}, {QStringLiteral("ja"), QStringLiteral("<img src=file:///secret>原題"), true}}));
        tab.addResults(QStringLiteral("Fixture"), {result});
        auto *tree = tab.findChild<QTreeWidget *>(); auto *item = tree->topLevelItem(0)->child(0);
        std::unique_ptr<QMenu> menu(tab.createResultMenu(item)); auto *action = menu->findChild<QAction *>(QStringLiteral("showAllTitles")); QVERIFY(action); action->trigger();
        auto *dialog = tab.findChild<QDialog *>(QStringLiteral("allTitlesDialog")); QVERIFY(dialog);
        auto *list = dialog->findChild<QTreeWidget *>(QStringLiteral("localizedTitleList")); QVERIFY(list);
        QCOMPARE(list->topLevelItemCount(), 2); QCOMPARE(list->topLevelItem(1)->text(1), QStringLiteral("<img src=file:///secret>原題"));
        dialog->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};
int main(int argc, char **argv) { return runIsolatedGuiTest<LanguagesTest>(argc, argv); }
#include "test_languagepreferences.moc"
