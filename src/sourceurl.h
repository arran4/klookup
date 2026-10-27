#ifndef SOURCEURL_H
#define SOURCEURL_H

#include <QUrl>

// Canonical source links may be displayed, opened, copied or dragged. Never
// expose embedded credentials or pass non-web schemes to desktop handlers.
inline QUrl validatedSourceUrl(const QString &text)
{
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")))
        return {};
    return url;
}
#endif
