// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PROVIDERELIGIBILITY_H
#define PROVIDERELIGIBILITY_H
#include "providerclassification.h"
#include <optional>
class KConfigGroup;
namespace ProviderEligibility {
struct Decision { bool enabled; QString reason; };
Decision evaluate(const QString &id, const ProviderClassification &classification,
                  std::optional<bool> provider, const QMap<QString, bool> &groups,
                  std::optional<bool> temporary = {});
Decision resolve(const KConfigGroup &providers, const KConfigGroup &groups, const QString &id,
                 const ProviderClassification &classification, std::optional<bool> temporary = {});
}
struct SearchSourceChoice {
    QString id, name, explanation;
    bool selected = false;
    bool selectable = true;
};
#endif
