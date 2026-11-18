// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providerdefaults.h"
#include <KConfigGroup>
#include <KLocalizedString>

bool ProviderDefaults::enabled(const QString &id)
{
    return id != QLatin1String("cheapshark") && id != QLatin1String("duckduckgo") && id != QLatin1String("localdocs");
}

bool ProviderDefaults::enabled(const KConfigGroup &settings, const QString &id)
{
    return settings.readEntry(id + QStringLiteral("_Enabled"), enabled(id));
}

QString ProviderDefaults::reason(const QString &id)
{
    if (id == QLatin1String("cheapshark"))
        return i18n("US-dollar PC-game deals; enable to include this source in searches.");
    if (id == QLatin1String("duckduckgo"))
        return i18n("DuckDuckGo Instant Answer API (encyclopedic abstracts, definitions, and related topics; not full web search). Enable to include this source in searches.");
    if (id == QLatin1String("localdocs"))
        return i18n("Offline local documentation directories (.md, .html, .txt); configure scan roots and enable.");
    return {};
}
