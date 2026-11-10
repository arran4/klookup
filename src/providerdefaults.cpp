// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providerdefaults.h"
#include <KConfigGroup>
#include <KLocalizedString>

bool ProviderDefaults::enabled(const QString &id)
{
    return id != QLatin1String("cheapshark");
}

bool ProviderDefaults::enabled(const KConfigGroup &settings, const QString &id)
{
    return settings.readEntry(id + QStringLiteral("_Enabled"), enabled(id));
}

QString ProviderDefaults::reason(const QString &id)
{
    return id == QLatin1String("cheapshark")
        ? i18n("US-dollar PC-game deals; enable to include this source in searches.") : QString();
}
