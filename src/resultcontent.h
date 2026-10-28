#ifndef RESULTCONTENT_H
#define RESULTCONTENT_H

#include <QMetaType>
#include <QString>
#include <QUrl>

// Opt-in description content for the continuation interface. SearchResult's
// existing fields remain plain text. LimitedHtml is untrusted at every boundary.
struct ResultContent {
    enum class Format { PlainText, LimitedHtml };
    QString description;
    Format format = Format::PlainText;
    QUrl baseUrl;
};
Q_DECLARE_METATYPE(ResultContent)

namespace Content
{
struct Rendered {
    QString html;
    QString plainText;
    bool truncated = false;
};
Rendered render(const ResultContent &content);
} // namespace Content
#endif
