#ifndef CLIPBOARDPOLICY_H
#define CLIPBOARDPOLICY_H

class KConfigGroup;
namespace ClipboardPolicy
{
enum class Capture { Never, Visible, Focused, Running };
enum class Trigger { Never, TrayOpened, ClipboardChanged, FocusGained, GlobalShortcut };
enum class Event { TrayOpened, ClipboardChanged, FocusGained, GlobalShortcut };
struct Policy {
    Capture capture = Capture::Never;
    Trigger trigger = Trigger::Never;
    bool permitsCapture(bool visible, bool focused) const;
    bool searchesOn(Event event) const;
};
Policy read(const KConfigGroup &group);
void write(KConfigGroup &group, const Policy &policy);
void migrate(KConfigGroup &group);
} // namespace ClipboardPolicy
#endif
