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

    void testStructuredBlockBounds()
    {
        ResultContent content;
        content.format = ResultContent::Format::StructuredBlocks;

        // Add 500 blocks (exceeding kMaxBlocks = 200)
        for (int i = 0; i < 500; ++i) {
            ContentBlock p;
            p.type = ContentBlock::Type::Paragraph;
            p.text = QStringLiteral("Large paragraph text ").repeated(100);
            content.blocks.append(p);
        }

        const auto rendered = Content::render(content);
        QVERIFY(rendered.truncated);
        QVERIFY(rendered.html.size() <= 1024 * 1024 + 1024);
        QVERIFY(rendered.html.contains(QStringLiteral("display limit")));

        // Test huge table bounds
        ResultContent tableContent;
        tableContent.format = ResultContent::Format::StructuredBlocks;
        ContentBlock tbl;
        tbl.type = ContentBlock::Type::Table;
        for (int r = 0; r < 300; ++r) {
            QStringList row;
            for (int c = 0; c < 50; ++c) {
                row.append(QStringLiteral("Cell content ").repeated(10));
            }
            tbl.tableRows.append(row);
        }
        tableContent.blocks.append(tbl);
        const auto renderedTable = Content::render(tableContent);
        QVERIFY(renderedTable.truncated);
        QVERIFY(renderedTable.html.size() <= 1024 * 1024 + 1024);

        // Test deep metadata recursion bounds in renderResultHtml
        SearchResult res;
        res.title = QStringLiteral("Deep Metadata Test");
        QVariantMap root;
        QVariantMap *current = &root;
        for (int d = 0; d < 20; ++d) {
            QVariantMap next;
            (*current)[QStringLiteral("level_%1").arg(d)] = QStringLiteral("node_%1").arg(d);
            (*current)[QStringLiteral("child")] = next;
            current = &next;
        }
        res.additionalData = root;
        const QString html = Content::renderResultHtml(res, ResultContent());
        QVERIFY(!html.isEmpty());
        QVERIFY(html.size() < 1024 * 1024);
    }

    void testNestedListsInMarkdown()
    {
        ResultContent content;
        content.format = ResultContent::Format::Markdown;
        content.description = QStringLiteral(
            "- Level 1 A\n"
            "  - Level 2 A1\n"
            "  - Level 2 A2\n"
            "    - Level 3 A2a\n"
            "- Level 1 B\n"
        );

        const auto rendered = Content::render(content);
        QVERIFY(!rendered.truncated);
        QVERIFY(rendered.html.contains(QStringLiteral("Level 1 A")));
        QVERIFY(rendered.html.contains(QStringLiteral("Level 2 A1")));
        QVERIFY(rendered.html.contains(QStringLiteral("Level 3 A2a")));
        QVERIFY(rendered.html.contains(QStringLiteral("Level 1 B")));

        // Verify balanced ul tags
        int openUl = rendered.html.count(QStringLiteral("<ul>"));
        int closeUl = rendered.html.count(QStringLiteral("</ul>"));
        QCOMPARE(openUl, closeUl);
        QVERIFY(openUl >= 2);
    }

    void testHostControlledImagePolicy()
    {
        ResultContent content;
        content.format = ResultContent::Format::StructuredBlocks;
        ContentBlock img;
        img.type = ContentBlock::Type::Image;
        img.imageUrl = QUrl(QStringLiteral("https://kde.org/stuff/banner.png"));
        img.caption = QStringLiteral("Banner Preview");
        content.blocks.append(img);

        // Default: allowRemoteImages is false
        const auto defaultRender = Content::render(content);
        QVERIFY(!defaultRender.html.contains(QStringLiteral("<img src=")));
        QVERIFY(defaultRender.html.contains(QStringLiteral("Banner Preview")));
        QVERIFY(defaultRender.html.contains(QStringLiteral("https://kde.org/stuff/banner.png")));

        // Opt-in: allowRemoteImages is true
        Content::RenderOptions optIn;
        optIn.allowRemoteImages = true;
        const auto optInRender = Content::render(content, optIn);
        QVERIFY(optInRender.html.contains(QStringLiteral("<img src=\"https://kde.org/stuff/banner.png\"")));
        QVERIFY(optInRender.html.contains(QStringLiteral("max-height:400px;")));

        // Markdown image
        ResultContent mdContent;
        mdContent.format = ResultContent::Format::Markdown;
        mdContent.description = QStringLiteral("![Alt text](https://kde.org/logo.png)");

        const auto mdDefault = Content::render(mdContent);
        QVERIFY(!mdDefault.html.contains(QStringLiteral("<img src=")));
        QVERIFY(mdDefault.html.contains(QStringLiteral("https://kde.org/logo.png")));

        const auto mdOptIn = Content::render(mdContent, optIn);
        QVERIFY(mdOptIn.html.contains(QStringLiteral("<img src=\"https://kde.org/logo.png\"")));
    }

    void testAdversarialMarkdownNoHtmlMode()
    {
        ResultContent content;
        content.format = ResultContent::Format::Markdown;
        content.description = QStringLiteral(
            "<div onclick=\"malicious()\">Click me</div>\n\n"
            "<object data=\"data:text/html,<script>alert(1)</script>\"></object>\n\n"
            "<embed src=\"evil.swf\">\n\n"
            "<style>body { display: none; }</style>\n\n"
            "<link rel=\"stylesheet\" href=\"http://evil.com/style.css\">\n\n"
            "```html\n<div class=\"safe-code\">code block should be preserved</div>\n```"
        );

        const auto rendered = Content::render(content);
        QVERIFY(!rendered.html.contains(QStringLiteral("<div onclick")));
        QVERIFY(!rendered.html.contains(QStringLiteral("<object")));
        QVERIFY(!rendered.html.contains(QStringLiteral("<embed")));
        QVERIFY(!rendered.html.contains(QStringLiteral("<style>")));
        QVERIFY(!rendered.html.contains(QStringLiteral("<link rel")));
        // Fenced code block is safely escaped but readable
        QVERIFY(rendered.html.contains(QStringLiteral("&lt;div class=&quot;safe-code&quot;&gt;")));
    }
};

QTEST_MAIN(TestResultContent)
#include "test_resultcontent.moc"
