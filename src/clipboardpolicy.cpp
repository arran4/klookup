#include "clipboardpolicy.h"
#include <KConfigGroup>
#include <QtGlobal>

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

bool ClipboardPolicy::Policy::permitsAutomaticLookup(bool hasVisibleWindow, bool focused) const
{
    // Tray-only / hidden / minimized windows must never perform automatic lookups.
    if (!hasVisibleWindow)
        return false;
    return permitsCapture(true, focused);
}

QString ClipboardPolicy::providerIdentityToken(const QString &providerId, const QString &scriptApprovalFingerprint)
{
    if (providerId.isEmpty())
        return {};
    if (!scriptApprovalFingerprint.isEmpty())
        return QStringLiteral("script:%1:%2").arg(providerId, scriptApprovalFingerprint);
    return QStringLiteral("native:%1").arg(providerId);
}

bool ClipboardPolicy::Policy::permitsAutoOnlineProvider(const QString &providerId, const QStringList &facets) const
{
    return permitsAutoOnlineProvider(providerId, QString(), facets);
}

bool ClipboardPolicy::Policy::permitsAutoOnlineProvider(const QString &providerId, const QString &identityToken, const QStringList &facets) const
{
    if (!autoOnlineEnabled || providerId.isEmpty())
        return false;
    // Only providers acknowledged at explicit opt-in may ever run automatically online.
    if (!autoAcknowledgedOnlineProviderIds.contains(providerId))
        return false;

    const bool explicitIdAllowed = autoOnlineAllowedProviderIds.contains(providerId);
    bool facetAllowed = false;
    if (!explicitIdAllowed) {
        for (const QString &facet : facets) {
            if (autoOnlineAllowedFacets.contains(facet)) {
                facetAllowed = true;
                break;
            }
        }
    }
    if (!explicitIdAllowed && !facetAllowed)
        return false;

    if (!identityToken.isEmpty()) {
        const bool isScript = identityToken.startsWith(QStringLiteral("script:"));
        // Replacing a native provider with a script override or changing a script's
        // approved fingerprint MUST fail closed until re-acknowledged in Settings.
        if (isScript || !autoAcknowledgedOnlineProviderTokens.isEmpty()) {
            if (!autoAcknowledgedOnlineProviderTokens.contains(identityToken))
                return false;
        }
        if (explicitIdAllowed && (isScript || !autoOnlineAllowedProviderTokens.isEmpty())) {
            if (!autoOnlineAllowedProviderTokens.contains(identityToken))
                return false;
        }
    }
    return true;
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

    const int inputSourceValue = group.readEntry("ClipboardInputSource", int(InputSource::Clipboard));
    if (inputSourceValue == int(InputSource::Clipboard) || inputSourceValue == int(InputSource::PrimarySelection))
        policy.inputSource = InputSource(inputSourceValue);

    policy.reactOnlyToNewCopy = group.readEntry("ClipboardReactOnlyToNewCopy", true);
    policy.readExistingOnOpen = group.readEntry("ClipboardReadExistingOnOpen", false);
    if (group.hasKey("ClipboardAutoLookupEnabled")) {
        policy.autoLookupEnabled = group.readEntry("ClipboardAutoLookupEnabled", false);
    } else {
        policy.autoLookupEnabled = (policy.trigger != Trigger::Never);
    }
    policy.explicitCaptureImmediateSearch = group.readEntry("ClipboardExplicitCaptureImmediateSearch", false);
    policy.autoMaxChars = qBound(20, group.readEntry("ClipboardAutoMaxChars", 180), 4096);
    policy.autoMaxWords = qBound(1, group.readEntry("ClipboardAutoMaxWords", 10), 500);
    policy.autoDebounceMs = qBound(0, group.readEntry("ClipboardAutoDebounceMs", 300), 5000);

    const int historyValue = group.readEntry("ClipboardAutoHistoryMode", int(AutoHistoryMode::OnInteraction));
    if (historyValue >= int(AutoHistoryMode::Never) && historyValue <= int(AutoHistoryMode::Always))
        policy.autoHistoryMode = AutoHistoryMode(historyValue);
    policy.autoOnlineEnabled = group.readEntry("ClipboardAutoOnlineEnabled", false);
    policy.autoOnlineAllowedFacets = group.readEntry("ClipboardAutoOnlineAllowedFacets", QStringList());
    policy.autoOnlineAllowedProviderIds = group.readEntry("ClipboardAutoOnlineAllowedProviderIds", QStringList());
    policy.autoAcknowledgedOnlineProviderIds = group.readEntry("ClipboardAutoAcknowledgedOnlineProviderIds", QStringList());
    policy.autoOnlineAllowedProviderTokens = group.readEntry("ClipboardAutoOnlineAllowedTokens", QStringList());
    policy.autoAcknowledgedOnlineProviderTokens = group.readEntry("ClipboardAutoAcknowledgedOnlineTokens", QStringList());
    return policy;
}

void ClipboardPolicy::write(KConfigGroup &group, const Policy &policy)
{
    group.writeEntry("ClipboardCaptureMode", int(policy.capture));
    group.writeEntry("ClipboardSearchTrigger", int(policy.trigger));
    group.writeEntry("ClipboardInputSource", int(policy.inputSource));
    group.writeEntry("ClipboardReactOnlyToNewCopy", policy.reactOnlyToNewCopy);
    group.writeEntry("ClipboardReadExistingOnOpen", policy.readExistingOnOpen);
    group.writeEntry("ClipboardAutoLookupEnabled", policy.autoLookupEnabled);
    group.writeEntry("ClipboardExplicitCaptureImmediateSearch", policy.explicitCaptureImmediateSearch);
    group.writeEntry("ClipboardAutoMaxChars", qBound(20, policy.autoMaxChars, 4096));
    group.writeEntry("ClipboardAutoMaxWords", qBound(1, policy.autoMaxWords, 500));
    group.writeEntry("ClipboardAutoDebounceMs", qBound(0, policy.autoDebounceMs, 5000));
    group.writeEntry("ClipboardAutoHistoryMode", int(policy.autoHistoryMode));
    group.writeEntry("ClipboardAutoOnlineEnabled", policy.autoOnlineEnabled);
    group.writeEntry("ClipboardAutoOnlineAllowedFacets", policy.autoOnlineAllowedFacets);
    group.writeEntry("ClipboardAutoOnlineAllowedProviderIds", policy.autoOnlineAllowedProviderIds);
    group.writeEntry("ClipboardAutoAcknowledgedOnlineProviderIds", policy.autoAcknowledgedOnlineProviderIds);
    group.writeEntry("ClipboardAutoOnlineAllowedTokens", policy.autoOnlineAllowedProviderTokens);
    group.writeEntry("ClipboardAutoAcknowledgedOnlineTokens", policy.autoAcknowledgedOnlineProviderTokens);
}

void ClipboardPolicy::migrate(KConfigGroup &group)
{
    const auto policy = read(group);
    if (!group.hasKey("ClipboardCaptureMode"))
        group.writeEntry("ClipboardCaptureMode", int(policy.capture));
    if (!group.hasKey("ClipboardSearchTrigger"))
        group.writeEntry("ClipboardSearchTrigger", int(policy.trigger));
}
