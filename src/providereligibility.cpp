// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providereligibility.h"
#include "providerdefaults.h"
#include <KConfigGroup>
#include <KLocalizedString>

ProviderEligibility::Decision ProviderEligibility::evaluate(const QString &id, const ProviderClassification &classification,
    std::optional<bool> provider, const QMap<QString, bool> &groups, std::optional<bool> temporary)
{
    if (temporary && !*temporary) return {false, i18n("Excluded from this search")};
    // A temporary opt-in cannot override an explicitly disabled source.
    if (provider && !*provider) return {false, i18n("Explicitly disabled in Settings; enable or reset it there")};
    if (temporary && *temporary) return {true, i18n("Selected for this search only")};
    if (provider) return {*provider, i18n("Explicit provider override")};
    QStringList allowed, denied;
    for (const auto &facet : classification.facets) if (groups.contains(facet)) {
        (groups.value(facet) ? allowed : denied).append(classification.label(facet));
    }
    if (!denied.isEmpty()) return {false, i18n("Disabled by groups: %1", denied.join(QStringLiteral(", "))) +
        (allowed.isEmpty() ? QString() : i18n("; conflicting enabled groups: %1", allowed.join(QStringLiteral(", "))))};
    if (!allowed.isEmpty()) return {true, i18n("Enabled by groups: %1", allowed.join(QStringLiteral(", ")))};
    return {ProviderDefaults::enabled(id), ProviderDefaults::reason(id).isEmpty() ? i18n("Provider default") : ProviderDefaults::reason(id)};
}
ProviderEligibility::Decision ProviderEligibility::resolve(const KConfigGroup &providers, const KConfigGroup &groups,
    const QString &id, const ProviderClassification &classification, std::optional<bool> temporary)
{
    const auto key = id + QStringLiteral("_Enabled");
    std::optional<bool> choice;
    if (providers.hasKey(key)) choice = providers.readEntry(key, false);
    QMap<QString, bool> groupChoices;
    for (const auto &facet : classification.facets) if (groups.hasKey(facet)) groupChoices.insert(facet, groups.readEntry(facet, false));
    return evaluate(id, classification, choice, groupChoices, temporary);
}
