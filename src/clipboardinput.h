#ifndef CLIPBOARDINPUT_H
#define CLIPBOARDINPUT_H

#include <QString>
class QMimeData;

namespace ClipboardInput
{
enum class Kind { Empty, Url, WebAddress, SearchText, Unsupported };
struct Classification {
    Kind kind = Kind::Unsupported;
    QString text;
    bool permitsAutomaticCapture() const
    {
        return kind == Kind::SearchText;
    }
};
constexpr int MaximumLength = 4096;
Classification classifyText(const QString &text);
Classification classifyMimeData(const QMimeData *mime);
QString explicitQuery(const QMimeData *mime);
} // namespace ClipboardInput
#endif
