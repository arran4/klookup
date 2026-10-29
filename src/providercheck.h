// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QJsonObject>
#include <QtPlugin>

// Optional interface: existing SearchProvider plugin vtables remain unchanged.
class ProviderCheckV1 {
public:
    virtual ~ProviderCheckV1() = default;
    // Offline only, deterministic, no network, credentials or private user data.
    virtual QJsonObject selfTestV1() const = 0;
};
Q_DECLARE_INTERFACE(ProviderCheckV1, "org.kde.klookup.ProviderCheck/1.0")
