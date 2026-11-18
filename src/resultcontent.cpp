// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "resultcontent.h"
#include "languagepreferences.h"
#include "resultmetadata.h"
#include "searchprovider.h"
#include "sourceurl.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QTextTable>
#include <QVBoxLayout>

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
    const auto localized = Languages::fromData(result.additionalData.value(QStringLiteral("localizedTitles")));
    if (!localized.isEmpty()) {
        const KConfigGroup langGroup(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Languages"));
        const QStringList prefs = Languages::read(langGroup);
        const QString origLang = result.additionalData.value(QStringLiteral("originalLanguage")).toString();
        const Languages::Summary summary = Languages::summarizeTitles(localized, prefs, origLang);
        QStringList titleNotes;
        if (!summary.original.text.isEmpty() && summary.original.text.trimmed() != result.title.trimmed()) {
            const QString tag = summary.original.language;
            const QString tagLabel = tag.isEmpty() ? i18n("Original") : i18n("Original · %1", tag);
            const QString tip = tag.isEmpty() ? i18n("Source-original title") : Languages::label(tag);
            titleNotes.append(QStringLiteral("<span title=\"%1\"><b>[%2]</b> %3</span>")
                                  .arg(tip.toHtmlEscaped(), tagLabel.toHtmlEscaped(), summary.original.text.toHtmlEscaped()));
        } else if (!summary.original.text.isEmpty()) {
            const QString tag = summary.original.language;
            const QString tagLabel = tag.isEmpty() ? i18n("Original") : i18n("Original · %1", tag);
            const QString tip = tag.isEmpty() ? i18n("Source-original title") : Languages::label(tag);
            titleNotes.append(QStringLiteral("<span title=\"%1\"><b>[%2]</b></span>")
                                  .arg(tip.toHtmlEscaped(), tagLabel.toHtmlEscaped()));
        }
        if (!summary.uiLocaleTitle.text.isEmpty() && summary.uiLocaleTitle.text.trimmed() != result.title.trimmed()) {
            const QString tag = summary.uiLocaleTitle.language;
            const QString tip = tag.isEmpty() ? i18n("UI language title") : Languages::label(tag);
            titleNotes.append(QStringLiteral("<span title=\"%1\"><b>[%2]</b> %3</span>")
                                  .arg(tip.toHtmlEscaped(), tag.toHtmlEscaped(), summary.uiLocaleTitle.text.toHtmlEscaped()));
        }
        if (summary.omittedCount > 0) {
            titleNotes.append(QStringLiteral("<a href=\"klookup://more-titles\">%1</a>")
                                  .arg(i18n("More titles (%1)…", summary.omittedCount).toHtmlEscaped()));
        }
        if (!titleNotes.isEmpty()) {
            html += QStringLiteral("<p>%1</p>").arg(titleNotes.join(QStringLiteral(" &nbsp; ")));
        }
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
        QStringList facts;
        auto addFact = [&](const QString &label, const QStringList &keys) {
            for (const QString &k : keys) {
                const QVariant v = result.additionalData.value(k);
                if (!v.isValid() || v.isNull())
                    continue;
                const QString s = v.toString().trimmed();
                if (!s.isEmpty() && s.compare(QLatin1String("null"), Qt::CaseInsensitive) != 0 &&
                    s.compare(QLatin1String("unknown"), Qt::CaseInsensitive) != 0) {
                    facts.append(QStringLiteral("<b>%1:</b> %2").arg(label.toHtmlEscaped(), s.toHtmlEscaped()));
                    return;
                }
            }
        };
        addFact(i18n("Status"), {QStringLiteral("status"), QStringLiteral("Status")});
        addFact(i18n("Year"), {QStringLiteral("year"), QStringLiteral("Year"), QStringLiteral("releaseYear")});
        addFact(i18n("Rating"), {QStringLiteral("rating"), QStringLiteral("Rating"), QStringLiteral("score")});
        addFact(i18n("Language"), {QStringLiteral("language"), QStringLiteral("Original title language")});
        addFact(i18n("Source"), {QStringLiteral("source"), QStringLiteral("topicGroup")});
        if (!facts.isEmpty()) {
            html += QStringLiteral("<p>%1</p>").arg(facts.join(QStringLiteral(" &nbsp;|&nbsp; ")));
        }

        QStringList tags;
        for (const QString &tagKey : {QStringLiteral("genres"), QStringLiteral("tags"), QStringLiteral("topics"), QStringLiteral("categories")}) {
            const QVariant v = result.additionalData.value(tagKey);
            if (!v.isValid())
                continue;
            const QStringList list = v.toStringList();
            for (const QString &t : list) {
                const QString trimmed = t.trimmed();
                if (!trimmed.isEmpty() && !tags.contains(trimmed)) {
                    tags.append(trimmed);
                    if (tags.size() >= 12)
                        break;
                }
            }
            if (tags.size() >= 12)
                break;
        }
        if (!tags.isEmpty()) {
            QStringList escapedTags;
            for (const QString &t : tags)
                escapedTags.append(QStringLiteral("<code>%1</code>").arg(t.toHtmlEscaped()));
            html += QStringLiteral("<p><b>%1:</b> %2</p>")
                        .arg(i18n("Tags").toHtmlEscaped(), escapedTags.join(QStringLiteral(" ")));
        }
    }

    if (options.includeRawAdditionalInfo && !result.additionalData.isEmpty()) {
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
        bool hasAdditional = false;
        QString additionalHtml;
        for (auto it = result.additionalData.cbegin(), end = result.additionalData.cend(); it != end; ++it) {
            if (it.key() == QLatin1String("localizedTitles"))
                continue;
            if (totalNodes > kMaxTreeNodes)
                break;
            hasAdditional = true;
            const auto node = Metadata::fromVariant(it.key(), it.value());
            additionalHtml += renderNode(node, 1);
        }
        if (hasAdditional)
            html += additionalHtml + QStringLiteral("</ul>");
        else
            html.chop(QStringLiteral("<h3>Additional Info</h3><ul>").size());
    }

    return html;
}

namespace
{
bool isSensitiveRawKey(const QString &key)
{
    const QString lower = key.toLower();
    return lower.contains(QLatin1String("token")) || lower.contains(QLatin1String("secret")) ||
           lower.contains(QLatin1String("password")) || lower.contains(QLatin1String("authorization")) ||
           lower.contains(QLatin1String("api_key")) || lower.contains(QLatin1String("apikey")) ||
           lower.contains(QLatin1String("cookie"));
}

QVariant redactVariant(const QVariant &value, int depth = 0)
{
    if (depth > 8)
        return QStringLiteral("[Max depth reached]");
    if (value.typeId() == QMetaType::QVariantMap) {
        const QVariantMap in = value.toMap();
        QVariantMap out;
        for (auto it = in.cbegin(); it != in.cend(); ++it) {
            out.insert(it.key(), isSensitiveRawKey(it.key()) ? QVariant(QStringLiteral("[REDACTED]"))
                                                              : redactVariant(it.value(), depth + 1));
        }
        return out;
    }
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList in = value.toList();
        QVariantList out;
        out.reserve(qMin(in.size(), 256));
        for (int i = 0; i < in.size() && i < 256; ++i)
            out.append(redactVariant(in.at(i), depth + 1));
        return out;
    }
    return value;
}

QString boundRawText(QString text)
{
    constexpr int kMaxRawChars = 262144;
    if (text.size() > kMaxRawChars) {
        text.truncate(kMaxRawChars);
        text.append(QStringLiteral("\n\n[Truncated at 256 KiB display limit]"));
    }
    return text;
}
} // namespace

QList<Content::RawSourceDescriptor> Content::extractRawSources(const SearchResult &result)
{
    return extractRawSources(result, ResultMetadata{});
}

QList<Content::RawSourceDescriptor> Content::extractRawSources(const SearchResult &result, const ResultMetadata &metadata)
{
    QList<RawSourceDescriptor> sources;
    constexpr int kMaxSources = 16;

    const QVariant explicitRaw = result.additionalData.value(QStringLiteral("rawSources"));
    if (explicitRaw.typeId() == QMetaType::QVariantList) {
        for (const QVariant &entryVar : explicitRaw.toList()) {
            if (sources.size() >= kMaxSources)
                break;
            const QVariantMap map = entryVar.toMap();
            const QString id = map.value(QStringLiteral("id")).toString().trimmed();
            const QString label = map.value(QStringLiteral("label")).toString().trimmed();
            const QString body = map.value(QStringLiteral("content")).toString();
            if (id.isEmpty() || label.isEmpty() || body.isEmpty())
                continue;
            RawSourceDescriptor desc;
            desc.id = id;
            desc.label = label;
            desc.mimeType = map.value(QStringLiteral("mimeType"), QStringLiteral("text/plain")).toString();
            desc.provenance = map.value(QStringLiteral("provenance")).toString();
            if (desc.mimeType == QLatin1String("application/json")) {
                const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
                if (!doc.isNull()) {
                    const QVariant redacted = redactVariant(doc.toVariant());
                    desc.content = boundRawText(QString::fromUtf8(QJsonDocument::fromVariant(redacted).toJson(QJsonDocument::Indented)));
                } else {
                    desc.content = boundRawText(body);
                }
            } else {
                desc.content = boundRawText(body);
            }
            sources.append(desc);
        }
    }

    QVariantMap filteredAdditional = result.additionalData;
    filteredAdditional.remove(QStringLiteral("rawSources"));
    if (!filteredAdditional.isEmpty() && sources.size() < kMaxSources) {
        const QVariant redacted = redactVariant(filteredAdditional);
        const QByteArray jsonBytes = QJsonDocument::fromVariant(redacted).toJson(QJsonDocument::Indented);
        if (!jsonBytes.isEmpty()) {
            sources.append({QStringLiteral("additional_data"),
                            i18n("Structured Fields (JSON)"),
                            QStringLiteral("application/json"),
                            i18n("Provider search result additionalData"),
                            boundRawText(QString::fromUtf8(jsonBytes)),
                            false});
        }
    }

    if (!metadata.sections.isEmpty() && sources.size() < kMaxSources) {
        QVariantList sectionsJson;
        for (const auto &section : metadata.sections) {
            QVariantList entries;
            for (const auto &node : section.children) {
                QStringList vals;
                for (const auto &v : node.values)
                    vals.append(v.text);
                entries.append(QVariantMap{{QStringLiteral("label"), node.label}, {QStringLiteral("values"), QVariant(vals)}});
            }
            sectionsJson.append(QVariantMap{{QStringLiteral("title"), section.label}, {QStringLiteral("entries"), entries}});
        }
        const QByteArray metaBytes = QJsonDocument::fromVariant(redactVariant(sectionsJson)).toJson(QJsonDocument::Indented);
        if (!metaBytes.isEmpty()) {
            sources.append({QStringLiteral("structured_metadata"),
                            i18n("Result Metadata (JSON)"),
                            QStringLiteral("application/json"),
                            i18n("Provider structured metadata sections"),
                            boundRawText(QString::fromUtf8(metaBytes)),
                            false});
        }
    }

    return sources;
}

void Content::showRawSourceDialog(const RawSourceDescriptor &source, QWidget *parent)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("rawDataDialog"));
    dialog->setWindowTitle(i18n("Raw Data — %1", source.label));
    dialog->resize(700, 480);

    auto *layout = new QVBoxLayout(dialog);
    if (!source.provenance.isEmpty() || !source.mimeType.isEmpty()) {
        auto *info = new QLabel(i18n("Source: %1 (%2)",
                                     source.provenance.isEmpty() ? source.id : source.provenance,
                                     source.mimeType.isEmpty() ? QStringLiteral("text/plain") : source.mimeType),
                                dialog);
        info->setTextFormat(Qt::PlainText);
        info->setWordWrap(true);
        layout->addWidget(info);
    }

    auto *searchRow = new QHBoxLayout;
    auto *findEdit = new QLineEdit(dialog);
    findEdit->setObjectName(QStringLiteral("rawDataSearch"));
    findEdit->setPlaceholderText(i18n("Find in raw data…"));
    findEdit->setAccessibleName(i18n("Find in raw data"));
    auto *findNext = new QPushButton(i18n("Find next"), dialog);
    searchRow->addWidget(findEdit, 1);
    searchRow->addWidget(findNext);
    layout->addLayout(searchRow);

    auto *editor = new QPlainTextEdit(dialog);
    editor->setObjectName(QStringLiteral("rawDataText"));
    editor->setAccessibleName(i18n("Raw provider source text"));
    editor->setReadOnly(true);
    editor->setPlainText(source.content);
    layout->addWidget(editor, 1);

    QObject::connect(findNext, &QPushButton::clicked, dialog, [editor, findEdit] {
        const QString needle = findEdit->text();
        if (!needle.isEmpty() && !editor->find(needle)) {
            editor->moveCursor(QTextCursor::Start);
            editor->find(needle);
        }
    });
    QObject::connect(findEdit, &QLineEdit::returnPressed, findNext, &QPushButton::click);

    auto *buttons = new QHBoxLayout;
    auto *copyBtn = new QPushButton(i18n("Copy all"), dialog);
    copyBtn->setObjectName(QStringLiteral("rawDataCopy"));
    QObject::connect(copyBtn, &QPushButton::clicked, dialog, [editor] {
        QGuiApplication::clipboard()->setText(editor->toPlainText());
    });
    buttons->addWidget(copyBtn);
    buttons->addStretch();
    auto *closeBox = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(closeBox, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    buttons->addWidget(closeBox);
    layout->addLayout(buttons);

    dialog->show();
}
