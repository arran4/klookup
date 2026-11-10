// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PROVIDERDEFAULTS_H
#define PROVIDERDEFAULTS_H
#include <QString>
class KConfigGroup;
namespace ProviderDefaults {
bool enabled(const QString &id);
bool enabled(const KConfigGroup &settings, const QString &id);
QString reason(const QString &id);
}
#endif
