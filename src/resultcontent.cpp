// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "resultcontent.h"
#include "resultmetadata.h"
#include "searchprovider.h"
#include "sourceurl.h"

#include <QFont>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QTextTable>

namespace
{
class ResourceFreeDocument : public QTextDocument
{
protected:
    QVariant loadResource(int, const QUrl &) override
    {
        return QByteArray();
    }
};
} // namespace

Content::Rendered Content::render(const ResultContent &content, const RenderOptions &options)
{
    constexpr int kMaxInputLimit = 256 * 1024;
    constexpr int kMaxTotalHtml = 1024 * 1024; // 1 MiB
    constexpr int kMaxBlocks = 200;
    constexpr int kMaxBlockTextLen = 64 * 1024;
    constexpr int kMaxListItems = 500;
    constexpr int kMaxTableRows = 200;
    constexpr int kMaxTableCols = 30;
    constexpr int kMaxCellLen = 4 * 1024;
    constexpr int kMaxFieldItems = 200;
    constexpr int kMaxFieldKeyLen = 1024;
    constexpr int kMaxFieldValueLen = 16 * 1024;

    Rendered result;

    if (content.format == ResultContent::Format::StructuredBlocks) {
        QString html;
        QString plain;
        int blockCount = 0;
        for (const auto &block : content.blocks) {
            if (++blockCount > kMaxBlocks || html.size() >= kMaxTotalHtml) {
                result.truncated = true;
                break;
            }
            QString blockHtml;
            QString blockPlain;
            switch (block.type) {
            case ContentBlock::Type::Heading: {
                int lvl = qBound(1, block.level, 6);
                const QString text = block.text.left(kMaxBlockTextLen);
                blockHtml = QStringLiteral("<h%1>%2</h%1>").arg(lvl).arg(text.toHtmlEscaped());
                blockPlain = text + QLatin1Char('\n');
                break;
            }
            case ContentBlock::Type::Paragraph: {
                const QString text = block.text.left(kMaxBlockTextLen);
                blockHtml = QStringLiteral("<p>%1</p>").arg(text.toHtmlEscaped());
                blockPlain = text + QLatin1Char('\n');
                break;
            }
            case ContentBlock::Type::CodeBlock: {
                const QString text = block.text.left(kMaxBlockTextLen);
                const QString lang = block.language.left(64);
                QString langClass = lang.isEmpty() ? QString() : QStringLiteral(" class=\"language-%1\"").arg(lang.toHtmlEscaped());
                blockHtml = QStringLiteral("<pre><code%1>%2</code></pre>").arg(langClass, text.toHtmlEscaped());
                blockPlain = text + QLatin1Char('\n');
                break;
            }
            case ContentBlock::Type::List: {
                blockHtml = QStringLiteral("<ul>");
                int itemCount = 0;
                for (const auto &it : block.items) {
                    if (++itemCount > kMaxListItems) {
                        result.truncated = true;
                        break;
                    }
                    const QString itemText = it.left(kMaxBlockTextLen);
                    blockHtml += QStringLiteral("<li>%1</li>").arg(itemText.toHtmlEscaped());
                    blockPlain += QStringLiteral("- ") + itemText + QLatin1Char('\n');
                }
                blockHtml += QStringLiteral("</ul>");
                break;
            }
            case ContentBlock::Type::Table: {
                blockHtml = QStringLiteral("<table border=\"1\" cellpadding=\"4\" style=\"border-collapse:collapse;\">");
                bool first = true;
                int rowCount = 0;
                for (const auto &row : block.tableRows) {
                    if (++rowCount > kMaxTableRows) {
                        result.truncated = true;
                        break;
                    }
                    blockHtml += QStringLiteral("<tr>");
                    QString rowPlain;
                    int colCount = 0;
                    for (const auto &col : row) {
                        if (++colCount > kMaxTableCols) {
                            result.truncated = true;
                            break;
                        }
                        const QString colText = col.left(kMaxCellLen);
                        if (first) {
                            blockHtml += QStringLiteral("<th>%1</th>").arg(colText.toHtmlEscaped());
                        } else {
                            blockHtml += QStringLiteral("<td>%1</td>").arg(colText.toHtmlEscaped());
                        }
                        rowPlain += colText + QStringLiteral(" | ");
                    }
                    blockHtml += QStringLiteral("</tr>");
                    blockPlain += rowPlain + QLatin1Char('\n');
                    first = false;
                }
                blockHtml += QStringLiteral("</table>");
                break;
            }
            case ContentBlock::Type::FieldList: {
                blockHtml = QStringLiteral("<table class=\"field-list\" cellpadding=\"2\">");
                int fieldCount = 0;
                for (const auto &field : block.fields) {
                    if (++fieldCount > kMaxFieldItems) {
                        result.truncated = true;
                        break;
                    }
                    const QString key = field.first.left(kMaxFieldKeyLen);
                    const QString val = field.second.left(kMaxFieldValueLen);
                    blockHtml += QStringLiteral("<tr><th align=\"left\">%1:</th><td>%2</td></tr>")
                                .arg(key.toHtmlEscaped(), val.toHtmlEscaped());
                    blockPlain += QStringLiteral("%1: %2\n").arg(key, val);
                }
                blockHtml += QStringLiteral("</table>");
                break;
            }
            case ContentBlock::Type::Quote: {
                const QString text = block.text.left(kMaxBlockTextLen);
                blockHtml = QStringLiteral("<blockquote><p>%1</p></blockquote>").arg(text.toHtmlEscaped());
                blockPlain = QStringLiteral("> ") + text + QLatin1Char('\n');
                break;
            }
            case ContentBlock::Type::Image: {
                const auto safeUrl = Metadata::safeWebUrl(block.imageUrl.toString(QUrl::FullyEncoded));
                const QString caption = block.caption.isEmpty() ? QStringLiteral("Image") : block.caption.left(1024);
                if (!safeUrl.isEmpty()) {
                    if (options.allowRemoteImages) {
                        blockHtml = QStringLiteral("<p><a href=\"%1\"><img src=\"%1\" alt=\"%2\" style=\"max-width:100%; max-height:400px; height:auto;\" /></a><br/><i>%2</i></p>")
                                    .arg(safeUrl.toString(QUrl::FullyEncoded).toHtmlEscaped(), caption.toHtmlEscaped());
                    } else {
                        blockHtml = QStringLiteral("<p class=\"image-placeholder\"><i>[Image: <a href=\"%1\">%2</a>]</i></p>")
                                    .arg(safeUrl.toString(QUrl::FullyEncoded).toHtmlEscaped(), caption.toHtmlEscaped());
                    }
                } else {
                    blockHtml = QStringLiteral("<p><i>[%1]</i></p>").arg(caption.toHtmlEscaped());
                }
                blockPlain = QStringLiteral("[%1]\n").arg(caption);
                break;
            }
            }

            if (html.size() + blockHtml.size() > kMaxTotalHtml) {
                result.truncated = true;
                break;
            }
            html += blockHtml;
            plain += blockPlain;
        }

        if (result.truncated) {
            html += QStringLiteral("<p><i>[Content omitted at display limit]</i></p>");
        }
        result.html = html;
        result.plainText = plain;
        return result;
    }

    result.truncated = content.description.size() > kMaxInputLimit;
    const QString input = content.description.left(kMaxInputLimit);

    if (content.format == ResultContent::Format::PlainText) {
        result.html = QStringLiteral("<p>%1</p>").arg(input.toHtmlEscaped());
        result.plainText = input;
        return result;
    }

    ResourceFreeDocument document;
    document.setUndoRedoEnabled(false);
    document.setResourceProvider([](const QUrl &) { return QVariant(QByteArray()); });

    if (content.format == ResultContent::Format::Markdown) {
        // Strip raw HTML tags outside of code blocks/inline code so untrusted HTML never executes
        static const QRegularExpression mdTokenRegex(
            QStringLiteral("(```[\\s\\S]*?```|`[^`\\n]+`)|(</?[a-zA-Z][^>]*>|<!--[\\s\\S]*?-->|<![a-zA-Z][^>]*>)")
        );
        QString safeMarkdown;
        safeMarkdown.reserve(input.size());
        int lastIndex = 0;
        auto it = mdTokenRegex.globalMatch(input);
        while (it.hasNext()) {
            auto match = it.next();
            safeMarkdown.append(input.mid(lastIndex, match.capturedStart() - lastIndex));
            if (!match.capturedView(1).isEmpty()) {
                safeMarkdown.append(match.capturedView(1));
            }
            // If group 2 (raw HTML tag) matched, strip it by not appending
            lastIndex = match.capturedEnd();
        }
        safeMarkdown.append(input.mid(lastIndex));

        // Use Qt's MarkdownNoHTML feature to explicitly disable parsing of raw HTML elements
        const QTextDocument::MarkdownFeatures mdFeatures = QTextDocument::MarkdownFeatures::fromInt(
            static_cast<int>(QTextDocument::MarkdownDialectGitHub) | static_cast<int>(QTextDocument::MarkdownNoHTML)
        );
        document.setMarkdown(safeMarkdown, mdFeatures);
    } else {
        document.setDefaultStyleSheet(QStringLiteral(".searchmatch { font-weight: bold; }"));
        document.setHtml(input);
    }

    result.plainText = document.toPlainText();

    int runs = 0;
    int blocks = 0;
    int currentListIndent = 0;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        if (++blocks > 4000 || runs >= 12000 || result.html.size() > kMaxTotalHtml) {
            result.truncated = true;
            break;
        }

        // Table detection in Markdown
        QTextCursor cur(block);
        auto *table = cur.currentTable();
        if (table) {
            while (currentListIndent > 0) {
                result.html += QStringLiteral("</ul>");
                --currentListIndent;
            }
            result.html += QStringLiteral("<table border=\"1\" cellpadding=\"4\" style=\"border-collapse:collapse;\">");
            for (int r = 0; r < table->rows(); ++r) {
                result.html += QStringLiteral("<tr>");
                for (int c = 0; c < table->columns(); ++c) {
                    QTextTableCell cell = table->cellAt(r, c);
                    const QString cellText = cell.firstCursorPosition().block().text().toHtmlEscaped();
                    if (r == 0) {
                        result.html += QStringLiteral("<th>%1</th>").arg(cellText);
                    } else {
                        result.html += QStringLiteral("<td>%1</td>").arg(cellText);
                    }
                }
                result.html += QStringLiteral("</tr>");
            }
            result.html += QStringLiteral("</table>");
            block = table->lastCursorPosition().block();
            continue;
        }

        const bool isListItem = (block.textList() != nullptr);
        if (isListItem) {
            int listIndent = block.textList()->format().indent();
            int targetIndent = qBound(1, listIndent > 0 ? listIndent : block.blockFormat().indent(), 6);
            while (currentListIndent < targetIndent) {
                result.html += QStringLiteral("<ul>");
                ++currentListIndent;
            }
            while (currentListIndent > targetIndent) {
                result.html += QStringLiteral("</ul>");
                --currentListIndent;
            }
        } else {
            while (currentListIndent > 0) {
                result.html += QStringLiteral("</ul>");
                --currentListIndent;
            }
        }

        QString paragraph;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            if (++runs > 12000) {
                result.truncated = true;
                break;
            }
            const auto fragment = it.fragment();
            if (!fragment.isValid())
                continue;

            if (fragment.charFormat().isImageFormat()) {
                const auto imgFormat = fragment.charFormat().toImageFormat();
                const QString imgSrc = imgFormat.name();
                const auto safeUrl = Metadata::safeWebUrl(imgSrc);
                if (!safeUrl.isEmpty()) {
                    if (options.allowRemoteImages) {
                        paragraph += QStringLiteral("<img src=\"%1\" alt=\"Image\" style=\"max-width:100%; max-height:400px;\" />")
                                         .arg(safeUrl.toString(QUrl::FullyEncoded).toHtmlEscaped());
                    } else {
                        paragraph += QStringLiteral("<i>[Image: <a href=\"%1\">%2</a>]</i>")
                                         .arg(safeUrl.toString(QUrl::FullyEncoded).toHtmlEscaped(), QStringLiteral("Image"));
                    }
                } else {
                    paragraph += QStringLiteral("<i>[Image]</i>");
                }
                continue;
            }

            QString text = fragment.text();
            text.remove(QChar::ObjectReplacementCharacter);
            QString html = text.toHtmlEscaped();
            html.replace(QChar::LineSeparator, QStringLiteral("<br>"));
            const auto format = fragment.charFormat();

            if (format.fontFixedPitch()) {
                html = QStringLiteral("<code>%1</code>").arg(html);
            }
            if (format.fontWeight() >= QFont::Bold) {
                html = QStringLiteral("<b>%1</b>").arg(html);
            }
            if (format.fontItalic()) {
                html = QStringLiteral("<i>%1</i>").arg(html);
            }
            if (format.isAnchor()) {
                QUrl link(format.anchorHref(), QUrl::StrictMode);
                const auto base = validatedSourceUrl(content.baseUrl.toString(QUrl::FullyEncoded));
                if (link.isRelative() && !base.isEmpty()) {
                    link = base.resolved(link);
                }
                link = validatedSourceUrl(link.toString(QUrl::FullyEncoded));
                if (!link.isEmpty()) {
                    html = QStringLiteral("<a href=\"%1\">%2</a>")
                               .arg(link.toString(QUrl::FullyEncoded).toHtmlEscaped(), html);
                }
            }
            paragraph += html;
        }

        if (!paragraph.isEmpty()) {
            const int heading = block.blockFormat().headingLevel();
            if (heading >= 1 && heading <= 6) {
                result.html += QStringLiteral("<h%1>%2</h%1>").arg(heading).arg(paragraph);
            } else if (isListItem) {
                result.html += QStringLiteral("<li>%1</li>").arg(paragraph);
            } else {
                result.html += QStringLiteral("<p>%1</p>").arg(paragraph);
            }
        }
    }

    while (currentListIndent > 0) {
        result.html += QStringLiteral("</ul>");
        --currentListIndent;
    }

    if (result.truncated) {
        result.html += QStringLiteral("<p>Additional content omitted at display limit. Open the source for the full article.</p>");
    }

    return result;
}

QString Content::renderResultHtml(const SearchResult &result, const ResultContent &content, const QString &providerName, const RenderOptions &options)
{
    QString html;
    if (!result.title.isEmpty()) {
        html += QStringLiteral("<h2>%1</h2>").arg(result.title.toHtmlEscaped());
    }

    QStringList subInfo;
    if (!providerName.isEmpty()) {
        subInfo.append(QStringLiteral("<b>Provider:</b> %1").arg(providerName.toHtmlEscaped()));
    }
    if (!result.type.isEmpty()) {
        subInfo.append(QStringLiteral("<b>Type:</b> %1").arg(result.type.toHtmlEscaped()));
    }
    if (!subInfo.isEmpty()) {
        html += QStringLiteral("<p>%1</p>").arg(subInfo.join(QStringLiteral(" &nbsp;|&nbsp; ")));
    }

    if (!content.description.isEmpty() || !content.blocks.isEmpty()) {
        html += render(content, options).html;
    } else if (!result.description.isEmpty()) {
        html += QStringLiteral("<p>%1</p>").arg(result.description.toHtmlEscaped());
    } else {
        html += QStringLiteral("<p><i>No description provided.</i></p>");
    }

    if (!result.url.isEmpty()) {
        const QUrl qurl = validatedSourceUrl(result.url);
        if (!qurl.isEmpty()) {
            html += QStringLiteral("<p>Source: <a href=\"%1\">%2</a></p>")
                        .arg(QString::fromUtf8(qurl.toEncoded()).toHtmlEscaped(), qurl.toDisplayString().toHtmlEscaped());
        } else {
            html += QStringLiteral("<p>Invalid or unsupported URL: %1</p>").arg(result.url.toHtmlEscaped());
        }
    }

    if (!result.additionalData.isEmpty()) {
        html += QStringLiteral("<h3>Additional Info</h3><ul>");
        constexpr int kMaxTreeDepth = 10;
        constexpr int kMaxTreeNodes = 500;
        int totalNodes = 0;
        std::function<QString(const MetadataNode &, int)> renderNode = [&](const MetadataNode &entry, int depth) -> QString {
            if (++totalNodes > kMaxTreeNodes || depth > kMaxTreeDepth) {
                return QStringLiteral("<li><i>[Additional info nested limit reached]</i></li>");
            }
            QStringList values;
            for (const auto &value : entry.values)
                values.append(value.text.toHtmlEscaped());
            QString text = QStringLiteral("<li><b>%1:</b> %2").arg(entry.label.toHtmlEscaped(), values.join(QStringLiteral(", ")));
            if (!entry.children.isEmpty() && depth < kMaxTreeDepth) {
                text += QStringLiteral("<ul>");
                for (const auto &child : entry.children) {
                    text += renderNode(child, depth + 1);
                    if (totalNodes > kMaxTreeNodes)
                        break;
                }
                text += QStringLiteral("</ul>");
            }
            return text + QStringLiteral("</li>");
        };
        for (auto it = result.additionalData.cbegin(), end = result.additionalData.cend(); it != end; ++it) {
            if (totalNodes > kMaxTreeNodes)
                break;
            const auto node = Metadata::fromVariant(it.key(), it.value());
            html += renderNode(node, 1);
        }
        html += QStringLiteral("</ul>");
    }

    return html;
}
