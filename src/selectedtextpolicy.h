// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SELECTEDTEXTPOLICY_H
#define SELECTEDTEXTPOLICY_H

#include <QString>
#include <QStringList>

class KConfigGroup;
class QMimeData;
class SearchProvider;

struct SelectedTextPolicy {
    bool enabled = false;
    bool autoPopup = false;
    bool recordHistory = false;
    int maxLength = 200;
    QStringList approvedRemoteProviderIds;

    bool isEligibleSelection(const QString &text, bool isAutomatic, const QMimeData *mime = nullptr) const;
    bool isProviderAllowed(const QString &providerId,
                           bool isLocalProvider,
                           bool isAutomatic,
                           bool explicitConsentGiven = false) const;

    static bool isLocalProvider(const SearchProvider *provider);
    static SelectedTextPolicy read(const KConfigGroup &group);
    static void write(KConfigGroup &group, const SelectedTextPolicy &policy);
};

#endif // SELECTEDTEXTPOLICY_H
