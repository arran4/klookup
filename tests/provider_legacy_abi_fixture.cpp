// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixtures/legacy_searchprovider.h"
class LegacyAbiProvider : public SearchProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SearchProvider_iid)
    Q_INTERFACES(SearchProvider)
public:
    using SearchProvider::SearchProvider;
    QString id() const override { return QStringLiteral("legacy_abi_fixture"); }
    QString name() const override { return QStringLiteral("Legacy ABI fixture"); }
    void search(const QString &query) override { SearchResult result; result.id = QStringLiteral("old-slot"); result.title = query; emit searchCompleted(id(), {result}); }
    SearchProvider *clone(QObject *parent = nullptr) const override { return new LegacyAbiProvider(parent); }
};
#include "provider_legacy_abi_fixture.moc"
