// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PROVIDER_MANGABAKA_EXPERIMENTAL_H
#define PROVIDER_MANGABAKA_EXPERIMENTAL_H

#include "provider_mangabaka.h"

class ProviderMangaBakaExperimental : public ProviderMangaBaka
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
    Q_INTERFACES(SearchProvider SearchProviderContinuation SearchProviderOptions)
public:
    explicit ProviderMangaBakaExperimental(QObject *parent = nullptr);
    ~ProviderMangaBakaExperimental() override = default;

    QString name() const override;
    QString id() const override;
    SearchProvider *clone(QObject *parent = nullptr) const override;

protected:
    SearchResult decorateSearchResult(SearchResult result, const QJsonObject &rawObj) const override;
    SearchDetails decorateSearchDetails(SearchDetails details, const QJsonObject &rawObj) const override;
};

#endif // PROVIDER_MANGABAKA_EXPERIMENTAL_H
