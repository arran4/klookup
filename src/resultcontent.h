// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RESULTCONTENT_H
#define RESULTCONTENT_H

#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>

struct SearchResult;

// A typed structured content block for rich responses
struct ContentBlock {
    enum class Type {
        Heading,
        Paragraph,
        CodeBlock,
        List,
        Table,
        FieldList,
        Quote,
        Image
    };
    Type type = Type::Paragraph;
    QString text;
    int level = 1; // for Heading (1-6)
    QString language; // for CodeBlock
    QStringList items; // for List
    QList<QStringList> tableRows; // for Table (rows of columns)
    QList<QPair<QString, QString>> fields; // for FieldList (key-value pairs)
    QUrl imageUrl; // for Image
    QString caption;
};
Q_DECLARE_METATYPE(ContentBlock)

// Opt-in description content for search results and the continuation interface.
// SearchResult's existing fields remain plain text.
// LimitedHtml and Markdown are untrusted at every boundary and rendered safely.
struct ResultContent {
    enum class Format { PlainText, LimitedHtml, Markdown, StructuredBlocks };
    QString description;
    Format format = Format::PlainText;
    QUrl baseUrl;
    QList<ContentBlock> blocks = {};

    ResultContent() = default;
    ResultContent(QString desc, Format fmt = Format::PlainText, QUrl base = QUrl(), QList<ContentBlock> blks = {})
        : description(std::move(desc)), format(fmt), baseUrl(std::move(base)), blocks(std::move(blks)) {}
};
Q_DECLARE_METATYPE(ResultContent)

namespace Content
{
struct Rendered {
    QString html;
    QString plainText;
    bool truncated = false;
};

// Render content into safe, isolated HTML and plain text
Rendered render(const ResultContent &content);

// Standardized unified HTML renderer for search results across SearchTab and ResultDetailWindow
QString renderResultHtml(const SearchResult &result, const ResultContent &content, const QString &providerName = QString());

} // namespace Content

#endif // RESULTCONTENT_H
