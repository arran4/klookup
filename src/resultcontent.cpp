#include "resultcontent.h"
#include "sourceurl.h"
#include <QFont>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>

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
    result.truncated = content.description.size() > inputLimit;
    const QString input = content.description.left(inputLimit);
    if (content.format == ResultContent::Format::PlainText) {
        result.html = QStringLiteral("<p>%1</p>").arg(input.toHtmlEscaped());
        result.plainText = input;
    } else {
        ResourceFreeDocument document;
        document.setUndoRedoEnabled(false);
        document.setResourceProvider([](const QUrl &) { return QVariant(QByteArray()); });
        document.setDefaultStyleSheet(QStringLiteral(".searchmatch { font-weight: bold; }"));
        document.setHtml(input);
        // Rebuild from text runs; never pass through source tags, attributes,
        // stylesheet, image/object formats or resource URLs to the display view.
        int runs = 0;
        int blocks = 0;
        for (auto block = document.begin(); block.isValid(); block = block.next()) {
            if (++blocks > 4000 || runs >= 12000 || result.html.size() > 1024 * 1024) {
                result.truncated = true;
                break;
            }
            QString paragraph;
            QString plain;
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                if (++runs > 12000) {
                    result.truncated = true;
                    break;
                }
                const auto fragment = it.fragment();
                if (!fragment.isValid() || fragment.charFormat().isImageFormat())
                    continue;
                QString text = fragment.text();
                text.remove(QChar::ObjectReplacementCharacter);
                plain += text;
                QString html = text.toHtmlEscaped();
                html.replace(QChar::LineSeparator, QStringLiteral("<br>"));
                const auto format = fragment.charFormat();
                if (format.fontWeight() >= QFont::Bold)
                    html = QStringLiteral("<b>%1</b>").arg(html);
                if (format.fontItalic())
                    html = QStringLiteral("<i>%1</i>").arg(html);
                if (format.isAnchor()) {
                    QUrl link(format.anchorHref(), QUrl::StrictMode);
                    const auto base = validatedSourceUrl(content.baseUrl.toString(QUrl::FullyEncoded));
                    if (link.isRelative() && !base.isEmpty())
                        link = base.resolved(link);
                    link = validatedSourceUrl(link.toString(QUrl::FullyEncoded));
                    if (!link.isEmpty())
                        html = QStringLiteral("<a href=\"%1\">%2</a>").arg(link.toString(QUrl::FullyEncoded).toHtmlEscaped(), html);
                }
                paragraph += html;
            }
            if (!plain.isEmpty()) {
                result.html += QStringLiteral("<p>%1</p>").arg(paragraph);
                if (!result.plainText.isEmpty())
                    result.plainText += QLatin1Char('\n');
                result.plainText += plain;
            }
        }
    }
    if (result.truncated)
        result.html += QStringLiteral("<p>Additional content omitted at display limit. Open the source for the full article.</p>");
    return result;
}
