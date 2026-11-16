// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "resultcontent.h"
#include "resultdetailwindow.h"
#include "resultmetadata.h"
#include "searchtab.h"

#include <QAction>
#include <QDialog>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>

class TestSemanticAndRawData : public QObject
{
    Q_OBJECT

private slots:
    void testSemanticSummaryOmitsRawAdditionalInfoAndUnknowns()
    {
        SearchResult res;
        res.id = QStringLiteral("1");
        res.title = QStringLiteral("Semantic Work");
        res.description = QStringLiteral("Concise synopsis.");
        res.url = QStringLiteral("https://example.org/work/1");
        res.additionalData.insert(QStringLiteral("status"), QStringLiteral("Publishing"));
        res.additionalData.insert(QStringLiteral("year"), 2024);
        res.additionalData.insert(QStringLiteral("rating"), QStringLiteral("8.9/10"));
        res.additionalData.insert(QStringLiteral("language"), QStringLiteral("unknown"));
        res.additionalData.insert(QStringLiteral("genres"), QStringList{QStringLiteral("Sci-Fi"), QStringLiteral("Mystery")});
        res.additionalData.insert(QStringLiteral("internal_cursor_id"), QStringLiteral("opaque_api_field_999"));

        const QString html = Content::renderResultHtml(res, {}, QStringLiteral("ExampleProvider"));
        QVERIFY(html.contains(QStringLiteral("Publishing")));
        QVERIFY(html.contains(QStringLiteral("2024")));
        QVERIFY(html.contains(QStringLiteral("8.9/10")));
        QVERIFY(html.contains(QStringLiteral("Sci-Fi")));
        QVERIFY(!html.contains(QStringLiteral("unknown")));
        QVERIFY(!html.contains(QStringLiteral("Additional Info")));
        QVERIFY(!html.contains(QStringLiteral("opaque_api_field_999")));
    }

    void testRawDataMenuAbsentWhenNoRawSources()
    {
        SearchTab tab(QStringLiteral("empty raw"));
        SearchResult plain;
        plain.id = QStringLiteral("p1");
        plain.title = QStringLiteral("Plain Result");
        tab.addResults(QStringLiteral("Plain"), {plain}, QStringLiteral("plain"));

        auto *tree = tab.findChild<QTreeWidget *>(QStringLiteral("resultsTree"));
        auto *item = tree->topLevelItem(0)->child(0);
        std::unique_ptr<QMenu> menu(tab.createResultMenu(item));
        QVERIFY(menu != nullptr);
        QVERIFY(menu->findChild<QAction *>(QStringLiteral("showRawData")) == nullptr);
        QVERIFY(menu->findChild<QMenu *>(QStringLiteral("showRawDataMenu")) == nullptr);

        ResultDetailWindow win(plain, QStringLiteral("Plain"));
        QVERIFY(win.findChild<QPushButton *>(QStringLiteral("detailRawDataButton")) == nullptr);
    }

    void testMultipleRawSourcesRedactionBoundingAndDialog()
    {
        SearchResult rich;
        rich.id = QStringLiteral("r1");
        rich.title = QStringLiteral("Rich Result");
        rich.additionalData.insert(QStringLiteral("api_key"), QStringLiteral("TOP_SECRET_SHOULD_REDACT"));
        rich.additionalData.insert(QStringLiteral("rawSources"), QVariantList{
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("api_json")},
                {QStringLiteral("label"), QStringLiteral("Original API JSON")},
                {QStringLiteral("mimeType"), QStringLiteral("application/json")},
                {QStringLiteral("provenance"), QStringLiteral("https://api.example.org/item/1")},
                {QStringLiteral("content"), QStringLiteral("{\"id\":1,\"authorization\":\"Bearer SECRET_BEARER\"}")}
            },
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("raw_html")},
                {QStringLiteral("label"), QStringLiteral("Raw HTML")},
                {QStringLiteral("mimeType"), QStringLiteral("text/html")},
                {QStringLiteral("content"), QStringLiteral("<script>alert(1)</script><p>Source</p>")}
            }
        });

        const auto sources = Content::extractRawSources(rich);
        QCOMPARE(sources.size(), 3); // api_json, raw_html, additional_data
        QVERIFY(!sources[0].content.contains(QStringLiteral("SECRET_BEARER")));
        QVERIFY(sources[0].content.contains(QStringLiteral("[REDACTED]")));
        QVERIFY(!sources[2].content.contains(QStringLiteral("TOP_SECRET_SHOULD_REDACT")));
        QVERIFY(sources[2].content.contains(QStringLiteral("[REDACTED]")));

        SearchTab tab(QStringLiteral("rich raw"));
        tab.addResults(QStringLiteral("Rich"), {rich}, QStringLiteral("rich"));
        auto *tree = tab.findChild<QTreeWidget *>(QStringLiteral("resultsTree"));
        auto *item = tree->topLevelItem(0)->child(0);
        std::unique_ptr<QMenu> menu(tab.createResultMenu(item));
        auto *subMenu = menu->findChild<QMenu *>(QStringLiteral("showRawDataMenu"));
        QVERIFY(subMenu != nullptr);
        auto *rawHtmlAct = subMenu->findChild<QAction *>(QStringLiteral("rawSource-raw_html"));
        QVERIFY(rawHtmlAct != nullptr);
        rawHtmlAct->trigger();

        auto *dialog = tab.findChild<QDialog *>(QStringLiteral("rawDataDialog"));
        QVERIFY(dialog != nullptr);
        auto *editor = dialog->findChild<QPlainTextEdit *>(QStringLiteral("rawDataText"));
        QVERIFY(editor != nullptr);
        QVERIFY(editor->isReadOnly());
        QCOMPARE(editor->toPlainText(), QStringLiteral("<script>alert(1)</script><p>Source</p>"));
        dialog->close();

        ResultDetailWindow win(rich, QStringLiteral("Rich"));
        QVERIFY(win.findChild<QPushButton *>(QStringLiteral("detailRawDataButton")) != nullptr);
    }
};

QTEST_MAIN(TestSemanticAndRawData)
#include "test_semantic_and_raw_data.moc"
