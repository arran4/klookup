// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QSignalSpy>

#include "resultcontent.h"
#include "resultmetadata.h"
#include "resultdetailwindow.h"
#include "searchprovider.h"
#include "searchtab.h"

class TestResultContent : public QObject
{
    Q_OBJECT

private slots:
    void testPlainTextRendering()
    {
        ResultContent content;
        content.description = QStringLiteral("Simple plain text description with <tags> & symbols.");
        content.format = ResultContent::Format::PlainText;

        const auto rendered = Content::render(content);
        QCOMPARE(rendered.plainText, content.description);
        QVERIFY(rendered.html.contains(QStringLiteral("&lt;tags&gt;")));
        QVERIFY(rendered.html.contains(QStringLiteral("&amp;")));
        QVERIFY(!rendered.html.contains(QStringLiteral("<tags>")));
        QVERIFY(!rendered.truncated);
    }

    void testMarkdownFormatting()
    {
        ResultContent content;
        content.format = ResultContent::Format::Markdown;
        content.description = QStringLiteral(
            "# Main Heading\n\n"
            "This has **bold text**, *italic text*, and `inline code`.\n\n"
            "## Secondary Heading\n\n"
            "- Item Alpha\n"
            "- Item Beta\n\n"
            "| Key | Value |\n"
            "|---|---|\n"
            "| Version | 2.0 |\n"
            "| Status | Active |\n"
        );

        const auto rendered = Content::render(content);
        QVERIFY(rendered.html.contains(QStringLiteral("<h1>")));
        QVERIFY(rendered.html.contains(QStringLiteral("Main Heading")));
        QVERIFY(rendered.html.contains(QStringLiteral("<h2>")));
        QVERIFY(rendered.html.contains(QStringLiteral("Secondary Heading")));
        QVERIFY(rendered.html.contains(QStringLiteral("<b>bold text</b>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<i>italic text</i>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<code>inline code</code>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<ul>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<li>Item Alpha</li>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<li>Item Beta</li>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<table")));
        QVERIFY(rendered.html.contains(QStringLiteral("Version")));
        QVERIFY(rendered.html.contains(QStringLiteral("Active")));
        QVERIFY(!rendered.truncated);
    }

    void testAdversarialHtmlAndMaliciousLinks()
    {
        ResultContent content;
        content.format = ResultContent::Format::Markdown;
        content.description = QStringLiteral(
            "Hello <script>alert('pwned')</script>\n\n"
            "<iframe src=\"http://malicious.example.com\"></iframe>\n\n"
            "<img src=\"x\" onerror=\"alert(1)\" />\n\n"
            "[Unsafe Link](javascript:alert(1))\n\n"
            "[File Link](file:///etc/passwd)\n\n"
            "[Creds Link](https://user:password@example.com/api)\n\n"
            "[Safe Link](https://kde.org/announcements/)"
        );

        const auto rendered = Content::render(content);

        // Raw script/iframe/onerror must be neutralized and not appear as raw executable tags
        QVERIFY(!rendered.html.contains(QStringLiteral("<script>")));
        QVERIFY(!rendered.html.contains(QStringLiteral("<iframe>")));
        QVERIFY(!rendered.html.contains(QStringLiteral("onerror=")));

        // Dangerous links must not produce clickable anchors
        QVERIFY(!rendered.html.contains(QStringLiteral("href=\"javascript:")));
        QVERIFY(!rendered.html.contains(QStringLiteral("href=\"file:")));
        QVERIFY(!rendered.html.contains(QStringLiteral("user:password@")));

        // Safe link must be preserved
        QVERIFY(rendered.html.contains(QStringLiteral("href=\"https://kde.org/announcements/\"")));
    }

    void testLargePayloadTruncation()
    {
        ResultContent content;
        content.format = ResultContent::Format::Markdown;
        // Generate payload larger than 256 KiB limit
        content.description = QStringLiteral("Paragraph of repetitive content for size testing.\n\n").repeated(6000);
        QVERIFY(content.description.size() > 256 * 1024);

        const auto rendered = Content::render(content);
        QVERIFY(rendered.truncated);
        QVERIFY(rendered.html.contains(QStringLiteral("Additional content omitted at display limit")));
    }

    void testStructuredBlocksRendering()
    {
        ResultContent content;
        content.format = ResultContent::Format::StructuredBlocks;

        ContentBlock h;
        h.type = ContentBlock::Type::Heading;
        h.level = 2;
        h.text = QStringLiteral("Block Title");
        content.blocks.append(h);

        ContentBlock p;
        p.type = ContentBlock::Type::Paragraph;
        p.text = QStringLiteral("Block paragraph description.");
        content.blocks.append(p);

        ContentBlock code;
        code.type = ContentBlock::Type::CodeBlock;
        code.language = QStringLiteral("cpp");
        code.text = QStringLiteral("int x = 42;");
        content.blocks.append(code);

        ContentBlock list;
        list.type = ContentBlock::Type::List;
        list.items = {QStringLiteral("First"), QStringLiteral("Second")};
        content.blocks.append(list);

        ContentBlock table;
        table.type = ContentBlock::Type::Table;
        table.tableRows = {
            {QStringLiteral("Header 1"), QStringLiteral("Header 2")},
            {QStringLiteral("Data 1"), QStringLiteral("Data 2")}
        };
        content.blocks.append(table);

        ContentBlock fields;
        fields.type = ContentBlock::Type::FieldList;
        fields.fields = {
            {QStringLiteral("License"), QStringLiteral("GPL-3.0-or-later")},
            {QStringLiteral("Author"), QStringLiteral("KLookup Contributors")}
        };
        content.blocks.append(fields);

        const auto rendered = Content::render(content);
        QVERIFY(rendered.html.contains(QStringLiteral("<h2>Block Title</h2>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<p>Block paragraph description.</p>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<pre><code class=\"language-cpp\">int x = 42;</code></pre>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<li>First</li>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<th>Header 1</th>")));
        QVERIFY(rendered.html.contains(QStringLiteral("<td>Data 1</td>")));
        QVERIFY(rendered.html.contains(QStringLiteral("License:")));
        QVERIFY(rendered.html.contains(QStringLiteral("GPL-3.0-or-later")));
        QVERIFY(!rendered.truncated);
    }

    void testHierarchicalResultsInSearchTab()
    {
        SearchTab tab(QStringLiteral("plasma"));

        SearchResult parent;
        parent.id = QStringLiteral("parent-1");
        parent.title = QStringLiteral("KDE Plasma 6");
        parent.type = QStringLiteral("Desktop Environment");

        SearchResult child1;
        child1.id = QStringLiteral("child-1");
        child1.title = QStringLiteral("KWin Compositor");
        child1.parentId = QStringLiteral("parent-1");

        SearchResult child2;
        child2.id = QStringLiteral("child-2");
        child2.title = QStringLiteral("Breeze Theme");
        parent.children.append(child2);

        QList<SearchResult> results = {parent, child1};
        tab.addResults(QStringLiteral("KDE"), results, QStringLiteral("kde-provider"));

        auto *treeWidget = tab.findChild<QTreeWidget *>();
        QVERIFY(treeWidget);

        QCOMPARE(treeWidget->topLevelItemCount(), 1);
        QTreeWidgetItem *group = treeWidget->topLevelItem(0);
        QCOMPARE(group->text(0), QStringLiteral("KDE"));

        // Under group, parent-1 should be the top item
        QVERIFY(group->childCount() >= 1);
        QTreeWidgetItem *parentItem = group->child(0);
        QCOMPARE(parentItem->text(0), QStringLiteral("KDE Plasma 6"));

        // Children should be nested under parentItem
        QCOMPARE(parentItem->childCount(), 2);
        QCOMPARE(parentItem->child(0)->text(0), QStringLiteral("Breeze Theme"));
        QCOMPARE(parentItem->child(1)->text(0), QStringLiteral("KWin Compositor"));
    }

    void testTabAndDetachedWindowRenderingParity()
    {
        SearchResult res;
        res.id = QStringLiteral("item-42");
        res.title = QStringLiteral("Unified Rendering Item");
        res.type = QStringLiteral("Documentation");
        res.url = QStringLiteral("https://kde.org/docs/");

        ResultContent content;
        content.format = ResultContent::Format::Markdown;
        content.description = QStringLiteral("### Subtitle\n\nContent rendered uniformly across widgets.");

        ResultMetadata metadata;
        metadata.sections.append(Metadata::text(QStringLiteral("Category"), {QStringLiteral("Frameworks"), QStringLiteral("Plasma")}));

        // SearchTab
        SearchTab tab(QStringLiteral("query"));
        tab.addResults(QStringLiteral("Provider"), {res}, QStringLiteral("prov-id"));

        auto *tree = tab.findChild<QTreeWidget *>();
        QTreeWidgetItem *providerItem = tree->topLevelItem(0);
        QTreeWidgetItem *item = providerItem->child(0);
        item->setData(0, Qt::UserRole + 4, QVariant::fromValue(metadata));
        item->setData(0, Qt::UserRole + 5, QVariant::fromValue(content));

        emit tree->itemClicked(item, 0);

        auto *tabDetailsView = tab.findChild<QTextBrowser *>();
        QVERIFY(tabDetailsView);
        const QString tabHtml = tabDetailsView->toHtml();

        // ResultDetailWindow
        ResultDetailWindow window(res, QStringLiteral("Provider"), metadata, content);
        auto *windowDetailsView = window.findChild<QTextBrowser *>();
        QVERIFY(windowDetailsView);
        const QString windowHtml = windowDetailsView->toHtml();

        // Verify key structural elements are present identically in both
        QVERIFY(tabHtml.contains(QStringLiteral("Unified Rendering Item")));
        QVERIFY(windowHtml.contains(QStringLiteral("Unified Rendering Item")));
        QVERIFY(tabHtml.contains(QStringLiteral("Subtitle")));
        QVERIFY(windowHtml.contains(QStringLiteral("Subtitle")));
        QVERIFY(tabHtml.contains(QStringLiteral("https://kde.org/docs/")));
        QVERIFY(windowHtml.contains(QStringLiteral("https://kde.org/docs/")));
    }
};

QTEST_MAIN(TestResultContent)
#include "test_resultcontent.moc"
