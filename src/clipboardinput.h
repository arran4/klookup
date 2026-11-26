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
struct AutoCaptureLimits {
    int maxChars = 180;
    int maxWords = 10;
};
struct AutoCaptureDecision {
    bool eligibleForAutomaticLookup = false;
    bool eligibleForManualPopulation = false;
    QString text;
    QString rejectionReason;
};
constexpr int MaximumLength = 4096;
bool isSensitiveMimeData(const QMimeData *mime);
int countWords(const QString &text);
Classification classifyText(const QString &raw);
Classification classifyMimeData(const QMimeData *mime);
AutoCaptureDecision evaluateAutomaticCaptureText(const QString &raw, const AutoCaptureLimits &limits = {});
AutoCaptureDecision evaluateAutomaticCapture(const QMimeData *mime, const AutoCaptureLimits &limits = {});
QString explicitQuery(const QMimeData *mime);
} // namespace ClipboardInput
#endif
