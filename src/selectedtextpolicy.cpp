// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "selectedtextpolicy.h"
#include "clipboardinput.h"
#include "searchprovider.h"

#include <KConfigGroup>
#include <QMimeData>
#include <QRegularExpression>
#include <QUrl>

namespace
{
bool hasSecretMimeHint(const QMimeData *mime)
{
    if (!mime)
        return false;
    const QByteArray hint = mime->data(QStringLiteral("x-kde-passwordManagerHint"));
    return hint.trimmed().compare("secret", Qt::CaseInsensitive) == 0;
}

bool looksSensitiveOrNonTermForAuto(const QString &trimmed)
{
    if (trimmed.contains(QLatin1Char('\n')) || trimmed.contains(QLatin1Char('\r')))
        return true;
    if (ClipboardInput::classifyText(trimmed).kind != ClipboardInput::Kind::SearchText)
        return true;
    static const QRegularExpression emailRe(QStringLiteral(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)"));
    if (emailRe.match(trimmed).hasMatch())
        return true;
    if (trimmed.startsWith(QLatin1Char('/')) || trimmed.startsWith(QStringLiteral("~/")))
        return true;
    return false;
}
} // namespace

bool SelectedTextPolicy::isEligibleSelection(const QString &text, bool isAutomatic, const QMimeData *mime) const
{
    if (!enabled)
        return false;
    if (isAutomatic && !autoPopup)
        return false;
    if (hasSecretMimeHint(mime))
        return false;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;
    const int boundedMax = qBound(1, maxLength, 1000);
    if (trimmed.size() > boundedMax)
        return false;
    if (isAutomatic && looksSensitiveOrNonTermForAuto(trimmed))
        return false;
    return true;
}

bool SelectedTextPolicy::isProviderAllowed(const QString &providerId,
                                           bool isLocalProvider,
                                           bool isAutomatic,
                                           bool explicitConsentGiven) const
{
    if (!enabled || providerId.isEmpty())
        return false;
    if (isLocalProvider)
        return true;
    // Maintainer policy (11 Nov 2026): automatic selected-text lookups are strictly local-only.
    if (isAutomatic)
        return false;
    return explicitConsentGiven || approvedRemoteProviderIds.contains(providerId);
}

bool SelectedTextPolicy::isLocalProvider(const SearchProvider *provider)
{
    if (!provider)
        return false;
    const ProviderClassification cls = provider->classification();
    if (cls.facets.contains(QStringLiteral("local"), Qt::CaseInsensitive)
        && !cls.facets.contains(QStringLiteral("network"), Qt::CaseInsensitive)) {
        return true;
    }
    const QString pid = provider->id();
    return pid == QLatin1String("localdocs") || pid.startsWith(QLatin1String("local-"));
}

SelectedTextPolicy SelectedTextPolicy::read(const KConfigGroup &group)
{
    SelectedTextPolicy p;
    p.enabled = group.readEntry(QStringLiteral("Enabled"), false);
    p.autoPopup = p.enabled && group.readEntry(QStringLiteral("AutoPopup"), false);
    p.recordHistory = group.readEntry(QStringLiteral("RecordHistory"), false);
    p.maxLength = qBound(1, group.readEntry(QStringLiteral("MaxLength"), 200), 1000);
    p.approvedRemoteProviderIds = group.readEntry(QStringLiteral("ApprovedRemoteProviders"), QStringList());
    return p;
}

void SelectedTextPolicy::write(KConfigGroup &group, const SelectedTextPolicy &policy)
{
    group.writeEntry(QStringLiteral("Enabled"), policy.enabled);
    group.writeEntry(QStringLiteral("AutoPopup"), policy.enabled && policy.autoPopup);
    group.writeEntry(QStringLiteral("RecordHistory"), policy.recordHistory);
    group.writeEntry(QStringLiteral("MaxLength"), qBound(1, policy.maxLength, 1000));
    group.writeEntry(QStringLiteral("ApprovedRemoteProviders"), policy.approvedRemoteProviderIds);
}
