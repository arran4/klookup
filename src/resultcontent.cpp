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

Content::Rendered Content::render(const ResultContent &content)
{
    constexpr int inputLimit = 256 * 1024;
    Rendered result;

    if (content.format == ResultContent::Format::StructuredBlocks) {
        QString html;
        QString plain;
        for (const auto &block : content.blocks) {
            if (html.size() > 1024 * 1024) {
                result.truncated = true;
                break;
            }
            switch (block.type) {
            case ContentBlock::Type::Heading: {
                int lvl = qBound(1, block.level, 6);
                html += QStringLiteral("<h%1>%2</h%1>").arg(lvl).arg(block.text.toHtmlEscaped());
                plain += block.text + QLatin1Char('\n');
                break;
            }
            case ContentBlock::Type::Paragraph: {
                html += QStringLiteral("<p>%1</p>").arg(block.text.toHtmlEscaped());
                plain += block.text + QLatin1Char('\n');
                break;
            }
            case ContentBlock::Type::CodeBlock: {
                QString langClass = block.language.isEmpty() ? QString() : QStringLiteral(" class=\"language-%1\"").arg(block.language.toHtmlEscaped());
                html += QStringLiteral("<pre><code%1>%2</code></pre>").arg(langClass, block.text.toHtmlEscaped());
                plain += block.text + QLatin1Char('\n');
                break;
            }
            case ContentBlock::Type::List: {
                html += QStringLiteral("<ul>");
                for (const auto &it : block.items) {
                    html += QStringLiteral("<li>%1</li>").arg(it.toHtmlEscaped());
                    plain += QStringLiteral("- ") + it + QLatin1Char('\n');
                }
                html += QStringLiteral("</ul>");
                break;
            }
            case ContentBlock::Type::Table: {
                html += QStringLiteral("<table border=\"1\" cellpadding=\"4\" style=\"border-collapse:collapse;\">");
                bool first = true;
                for (const auto &row : block.tableRows) {
                    html += QStringLiteral("<tr>");
                    QString rowPlain;
                    for (const auto &col : row) {
                        if (first) {
                            html += QStringLiteral("<th>%1</th>").arg(col.toHtmlEscaped());
                        } else {
                            html += QStringLiteral("<td>%1</td>").arg(col.toHtmlEscaped());
                        }
                        rowPlain += col + QStringLiteral(" | ");
                    }
                    html += QStringLiteral("</tr>");
                    plain += rowPlain + QLatin1Char('\n');
                    first = false;
                }
                html += QStringLiteral("</table>");
                break;
            }
            case ContentBlock::Type::FieldList: {
                html += QStringLiteral("<table class=\"field-list\" cellpadding=\"2\">");
                for (const auto &field : block.fields) {
                    html += QStringLiteral("<tr><th align=\"left\">%1:</th><td>%2</td></tr>")
                                .arg(field.first.toHtmlEscaped(), field.second.toHtmlEscaped());
                    plain += QStringLiteral("%1: %2\n").arg(field.first, field.second);
                }
                html += QStringLiteral("</table>");
                break;
            }
            case ContentBlock::Type::Quote: {
                html += QStringLiteral("<blockquote><p>%1</p></blockquote>").arg(block.text.toHtmlEscaped());
                plain += QStringLiteral("> ") + block.text + QLatin1Char('\n');
                break;
            }
            case ContentBlock::Type::Image: {
                const auto safeUrl = Metadata::safeWebUrl(block.imageUrl.toString(QUrl::FullyEncoded));
                const QString caption = block.caption.isEmpty() ? QStringLiteral("Image") : block.caption;
                if (!safeUrl.isEmpty()) {
                    html += QStringLiteral("<p><a href=\"%1\"><img src=\"%1\" alt=\"%2\" style=\"max-width:100%; max-height:400px; height:auto;\" /></a><br/><i>%2</i></p>")
                                .arg(safeUrl.toString(QUrl::FullyEncoded).toHtmlEscaped(), caption.toHtmlEscaped());
                } else {
                    html += QStringLiteral("<p><i>[%1]</i></p>").arg(caption.toHtmlEscaped());
                }
                plain += QStringLiteral("[%1]\n").arg(caption);
                break;
            }
            }
        }
        result.html = html;
        result.plainText = plain;
        return result;
    }

    result.truncated = content.description.size() > inputLimit;
    const QString input = content.description.left(inputLimit);

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
        document.setMarkdown(safeMarkdown, QTextDocument::MarkdownDialectGitHub);
    } else {
        document.setDefaultStyleSheet(QStringLiteral(".searchmatch { font-weight: bold; }"));
        document.setHtml(input);
    }

    result.plainText = document.toPlainText();

    int runs = 0;
    int blocks = 0;
    bool inList = false;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        if (++blocks > 4000 || runs >= 12000 || result.html.size() > 1024 * 1024) {
            result.truncated = true;
            break;
        }

        // Table detection in Markdown
        QTextCursor cur(block);
        auto *table = cur.currentTable();
        if (table) {
            if (inList) {
                result.html += QStringLiteral("</ul>");
                inList = false;
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
        if (isListItem && !inList) {
            result.html += QStringLiteral("<ul>");
            inList = true;
        } else if (!isListItem && inList) {
            result.html += QStringLiteral("</ul>");
            inList = false;
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
                    paragraph += QStringLiteral("<img src=\"%1\" alt=\"Image\" style=\"max-width:100%; max-height:400px;\" />")
                                     .arg(safeUrl.toString(QUrl::FullyEncoded).toHtmlEscaped());
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

    if (inList) {
        result.html += QStringLiteral("</ul>");
    }

    if (result.truncated) {
        result.html += QStringLiteral("<p>Additional content omitted at display limit. Open the source for the full article.</p>");
    }

    return result;
}

QString Content::renderResultHtml(const SearchResult &result, const ResultContent &content, const QString &providerName)
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
        html += render(content).html;
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
        std::function<QString(const MetadataNode &)> renderNode = [&renderNode](const MetadataNode &entry) {
            QStringList values;
            for (const auto &value : entry.values)
                values.append(value.text.toHtmlEscaped());
            QString text = QStringLiteral("<li><b>%1:</b> %2").arg(entry.label.toHtmlEscaped(), values.join(QStringLiteral(", ")));
            if (!entry.children.isEmpty()) {
                text += QStringLiteral("<ul>");
                for (const auto &child : entry.children)
                    text += renderNode(child);
                text += QStringLiteral("</ul>");
            }
            return text + QStringLiteral("</li>");
        };
        for (auto it = result.additionalData.cbegin(), end = result.additionalData.cend(); it != end; ++it) {
            const auto node = Metadata::fromVariant(it.key(), it.value());
            html += renderNode(node);
        }
        html += QStringLiteral("</ul>");
    }

    return html;
}
