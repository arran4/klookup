#include "clipboardpolicy.h"
#include <KConfigGroup>

bool ClipboardPolicy::Policy::permitsCapture(bool visible, bool focused) const
{
    switch (capture) {
    case Capture::Never:
        return false;
    case Capture::Visible:
        return visible;
    case Capture::Focused:
        return visible && focused;
    case Capture::Running:
        return true;
    }
    return false;
}

bool ClipboardPolicy::Policy::searchesOn(Event event) const
{
    return (trigger == Trigger::TrayOpened && event == Event::TrayOpened) ||
           (trigger == Trigger::ClipboardChanged && event == Event::ClipboardChanged) ||
           (trigger == Trigger::FocusGained && event == Event::FocusGained) ||
           (trigger == Trigger::GlobalShortcut && event == Event::GlobalShortcut);
}

ClipboardPolicy::Policy ClipboardPolicy::read(const KConfigGroup &group)
{
    Policy policy;
    if (group.hasKey("ClipboardCaptureMode")) {
        const int value = group.readEntry("ClipboardCaptureMode", 0);
        if (value >= 0 && value <= int(Capture::Running))
            policy.capture = Capture(value);
    } else if (group.readEntry("MonitorClipboard", false)) {
        policy.capture = group.readEntry("ClipboardOnlyWhenFocused", true) ? Capture::Focused : Capture::Running;
    }
    if (group.hasKey("ClipboardSearchTrigger")) {
        const int value = group.readEntry("ClipboardSearchTrigger", 0);
        if (value >= 0 && value <= int(Trigger::GlobalShortcut))
            policy.trigger = Trigger(value);
    } else if (group.readEntry("MonitorClipboard", false) && group.readEntry("AutoSearchClipboard", false)) {
        policy.trigger = Trigger::ClipboardChanged;
    }
    return policy;
}

void ClipboardPolicy::write(KConfigGroup &group, const Policy &policy)
{
    group.writeEntry("ClipboardCaptureMode", int(policy.capture));
    group.writeEntry("ClipboardSearchTrigger", int(policy.trigger));
}

void ClipboardPolicy::migrate(KConfigGroup &group)
{
    const auto policy = read(group);
    if (!group.hasKey("ClipboardCaptureMode"))
        group.writeEntry("ClipboardCaptureMode", int(policy.capture));
    if (!group.hasKey("ClipboardSearchTrigger"))
        group.writeEntry("ClipboardSearchTrigger", int(policy.trigger));
}
