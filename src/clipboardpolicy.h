#ifndef CLIPBOARDPOLICY_H
#define CLIPBOARDPOLICY_H

class KConfigGroup;
namespace ClipboardPolicy
{
enum class Capture { Never, Visible, Focused, Running };
enum class Trigger { Never, TrayOpened, ClipboardChanged, FocusGained, GlobalShortcut };
enum class Event { TrayOpened, ClipboardChanged, FocusGained, GlobalShortcut };
enum class InputSource { Clipboard = 0, PrimarySelection = 1 };
enum class AutoHistoryMode { Never = 0, OnInteraction = 1, Always = 2 };

struct Policy {
    Capture capture = Capture::Never;
    Trigger trigger = Trigger::Never;
    InputSource inputSource = InputSource::Clipboard;
    bool reactOnlyToNewCopy = true;
    bool readExistingOnOpen = false;
    bool autoLookupEnabled = false;
    bool explicitCaptureImmediateSearch = false;
    int autoMaxChars = 180;
    int autoMaxWords = 10;
    int autoDebounceMs = 300;
    AutoHistoryMode autoHistoryMode = AutoHistoryMode::OnInteraction;

    bool permitsCapture(bool visible, bool focused) const;
    bool permitsAutomaticLookup(bool hasVisibleWindow, bool focused) const;
    bool searchesOn(Event event) const;
};
Policy read(const KConfigGroup &group);
void write(KConfigGroup &group, const Policy &policy);
void migrate(KConfigGroup &group);
} // namespace ClipboardPolicy
#endif
