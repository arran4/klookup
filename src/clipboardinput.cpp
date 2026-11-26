#include "clipboardinput.h"
#include <QMimeData>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

ClipboardInput::Classification ClipboardInput::classifyText(const QString &raw)
{
    if (raw.size() > MaximumLength)
        return {Kind::Unsupported, {}};
    for (const auto character : raw) {
        if ((character.category() == QChar::Other_Control && character != QLatin1Char('\n') && character != QLatin1Char('\r') &&
             character != QLatin1Char('\t')) ||
            character == QChar::ReplacementCharacter)
            return {Kind::Unsupported, {}};
    }
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty())
        return {Kind::Empty, {}};

    // Normalize leading/trailing blank lines. Multiline paragraphs, prose, and blocks
    // containing multiple non-blank lines are rejected from automatic search capture.
    const auto lines = trimmed.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    if (lines.size() > 1)
        return {Kind::Unsupported, {}};

    const QString text = lines.isEmpty() ? QString() : lines.first().trimmed();
    if (text.isEmpty())
        return {Kind::Empty, {}};

    static const QRegularExpression whitespace(QStringLiteral("\\s"), QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression path(QStringLiteral("^(?:/|~/|\\.\\.?/|[A-Za-z]:[\\\\/]|\\\\\\\\)"));
    if (path.match(text).hasMatch())
        return {Kind::Unsupported, {}};
    if (!text.contains(whitespace)) {
        static const QRegularExpression scheme(QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]*):"));
        const auto match = scheme.match(text);
        static const QSet<QString> uriSchemes = {QStringLiteral("http"),       QStringLiteral("https"), QStringLiteral("ftp"),
                                                 QStringLiteral("ftps"),       QStringLiteral("file"),  QStringLiteral("mailto"),
                                                 QStringLiteral("tel"),        QStringLiteral("sms"),   QStringLiteral("magnet"),
                                                 QStringLiteral("urn"),        QStringLiteral("data"),  QStringLiteral("about"),
                                                 QStringLiteral("javascript"), QStringLiteral("ssh"),   QStringLiteral("git"),
                                                 QStringLiteral("ws"),         QStringLiteral("wss"),   QStringLiteral("news")};
        if (match.hasMatch() &&
            (uriSchemes.contains(match.captured(1).toLower()) || text.mid(match.capturedLength()).startsWith(QStringLiteral("//"))))
            return {Kind::Url, {}};
        // A bare email is personal data, rather than an automatic lookup query.
        static const QRegularExpression email(QStringLiteral("^[^@]+@[^@]+\\.[^@]+$"));
        if (email.match(text).hasMatch())
            return {Kind::Unsupported, {}};
        const QUrl candidate(QStringLiteral("https://") + text, QUrl::StrictMode);
        const QString host = candidate.host().toLower();
        const QString suffix = host.section(QLatin1Char('.'), -1);
        // A conservative set avoids interpreting code/file names (e.g. readme.md,
        // QtCore.QString) or dotted version numbers as domains. No DNS is used.
        static const QSet<QString> commonSuffixes = {
            QStringLiteral("com"),  QStringLiteral("org"),  QStringLiteral("net"), QStringLiteral("edu"),    QStringLiteral("gov"),
            QStringLiteral("mil"),  QStringLiteral("int"),  QStringLiteral("io"),  QStringLiteral("dev"),    QStringLiteral("app"),
            QStringLiteral("xyz"),  QStringLiteral("info"), QStringLiteral("biz"), QStringLiteral("online"), QStringLiteral("site"),
            QStringLiteral("tech"), QStringLiteral("ai"),   QStringLiteral("co"),  QStringLiteral("uk"),     QStringLiteral("de"),
            QStringLiteral("fr"),   QStringLiteral("jp"),   QStringLiteral("au"),  QStringLiteral("ca"),     QStringLiteral("nz"),
            QStringLiteral("us"),   QStringLiteral("eu"),   QStringLiteral("me")};
        static const QRegularExpression hostPattern(QStringLiteral("^[\\p{L}\\p{N}](?:[\\p{L}\\p{N}.-]*[\\p{L}\\p{N}])?$"),
                                                    QRegularExpression::UseUnicodePropertiesOption);
        const bool looksLikeDomain =
            host.contains(QLatin1Char('.')) && hostPattern.match(host).hasMatch() && !host.contains(QStringLiteral(".."));
        const bool explicitHost = host.startsWith(QStringLiteral("www.")) ||
                                  (host == QStringLiteral("localhost") && candidate.port() >= 0) ||
                                  (text.startsWith(QLatin1Char('[')) && host.contains(QLatin1Char(':')));
        const bool domainWithPath = looksLikeDomain && !candidate.path().isEmpty() && candidate.path() != QStringLiteral("/");
        if (candidate.isValid() && candidate.userInfo().isEmpty() &&
            (explicitHost || (looksLikeDomain && commonSuffixes.contains(suffix)) || domainWithPath))
            return {Kind::WebAddress, {}};
    } else {
        // Text contains whitespace. Check if it contains embedded URLs, URIs, email addresses, or domain references.
        // Mixed text containing links, emails, or URL-rich blocks must not automatically become a provider query.
        static const QRegularExpression embeddedUrlOrEmail(
            QStringLiteral("(?:[A-Za-z][A-Za-z0-9+.-]*://|\\b(?:mailto|urn|magnet|tel|sms|javascript|data|about|file):\\S+|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}|\\bwww\\.[a-z0-9.-]+|\\b[a-z0-9-]+\\.(?:com|org|net|edu|gov|io|dev|app|xyz|info|biz|co|uk|de|fr|jp|au|ca|nz|us|eu|me)(?:/\\S+|\\b))"),
            QRegularExpression::CaseInsensitiveOption);
        if (embeddedUrlOrEmail.match(text).hasMatch())
            return {Kind::Unsupported, {}};
    }
    return {Kind::SearchText, text};
}

bool ClipboardInput::isSensitiveMimeData(const QMimeData *mime)
{
    if (!mime)
        return false;
    const QByteArray kdeHint = mime->data(QStringLiteral("x-kde-passwordManagerHint"));
    if (!kdeHint.isEmpty() && kdeHint.trimmed().toLower() != QByteArrayLiteral("public"))
        return true;
    const QStringList formats = mime->formats();
    for (const QString &format : formats) {
        const QString lower = format.toLower();
        if (lower.contains(QStringLiteral("passwordmanagerhint")) ||
            lower.contains(QStringLiteral("keepass")) ||
            lower.contains(QStringLiteral("bitwarden")) ||
            lower.contains(QStringLiteral("1password")) ||
            lower.contains(QStringLiteral("clipboard.secret"))) {
            return true;
        }
    }
    return false;
}

int ClipboardInput::countWords(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return 0;
    static const QRegularExpression whitespace(QStringLiteral("\\s+"), QRegularExpression::UseUnicodePropertiesOption);
    return trimmed.split(whitespace, Qt::SkipEmptyParts).size();
}

ClipboardInput::Classification ClipboardInput::classifyMimeData(const QMimeData *mime)
{
    // Do not ask Qt to derive text from HTML, images, URI lists, or binary data,
    // and never automatically capture password-manager secret hints.
    if (!mime || isSensitiveMimeData(mime) || !mime->hasFormat(QStringLiteral("text/plain")))
        return {Kind::Unsupported, {}};
    return classifyText(mime->text());
}

ClipboardInput::AutoCaptureDecision ClipboardInput::evaluateAutomaticCaptureText(const QString &raw,
                                                                                 const AutoCaptureLimits &limits)
{
    const Classification classification = classifyText(raw);
    if (!classification.permitsAutomaticCapture()) {
        return {false, false, {}, QStringLiteral("ineligible_input")};
    }
    const QString text = classification.text;
    const int maxChars = qBound(1, limits.maxChars, MaximumLength);
    const int maxWords = qMax(1, limits.maxWords);
    if (text.size() > maxChars) {
        return {false, true, text, QStringLiteral("exceeds_char_limit")};
    }
    if (countWords(text) > maxWords) {
        return {false, true, text, QStringLiteral("exceeds_word_limit")};
    }
    return {true, true, text, {}};
}

ClipboardInput::AutoCaptureDecision ClipboardInput::evaluateAutomaticCapture(const QMimeData *mime,
                                                                             const AutoCaptureLimits &limits)
{
    if (!mime)
        return {false, false, {}, QStringLiteral("empty_mime")};
    if (isSensitiveMimeData(mime))
        return {false, false, {}, QStringLiteral("sensitive_mime")};
    if (!mime->hasFormat(QStringLiteral("text/plain")))
        return {false, false, {}, QStringLiteral("non_text_mime")};
    return evaluateAutomaticCaptureText(mime->text(), limits);
}

QString ClipboardInput::explicitQuery(const QMimeData *mime)
{
    if (!mime || !mime->hasFormat(QStringLiteral("text/plain")))
        return {};
    const QString raw = mime->text();
    if (raw.size() > MaximumLength)
        return {};
    for (const auto character : raw) {
        if ((character.category() == QChar::Other_Control && character != QLatin1Char('\n') && character != QLatin1Char('\r') &&
             character != QLatin1Char('\t')) ||
            character == QChar::ReplacementCharacter)
            return {};
    }
    const auto lines = raw.trimmed().split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    if (lines.size() == 1) {
        return lines.first().trimmed();
    }
    return {};
}
