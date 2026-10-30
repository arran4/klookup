// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "searchprovider.h"
class ObsoleteAniList : public SearchProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
    Q_INTERFACES(SearchProvider)
public:
    using SearchProvider::SearchProvider;
    QString id() const override { return QStringLiteral("anilist"); }
    QString name() const override { return QStringLiteral("Obsolete native anilist"); }
    SearchProvider *clone(QObject *parent = nullptr) const override { return new ObsoleteAniList(parent); }
    void search(const QString &) override { emit searchFailed(id(), QStringLiteral("Obsolete native must not run")); }
};
#include "provider_mock_anilist.moc"
