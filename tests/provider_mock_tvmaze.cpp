// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchprovider.h"

// Obsolete third-party installation fixture, never shipped as a provider.
class ObsoleteTvMaze : public SearchProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SearchProvider_iid)
    Q_INTERFACES(SearchProvider)
public:
    using SearchProvider::SearchProvider;
    QString id() const override { return QStringLiteral("tvmaze"); }
    QString name() const override { return QStringLiteral("Obsolete native TVMaze"); }
    void search(const QString &) override { emit searchFailed(id(), QStringLiteral("Obsolete provider executed")); }
    SearchProvider *clone(QObject *parent) const override { return new ObsoleteTvMaze(parent); }
};

#include "provider_mock_tvmaze.moc"
