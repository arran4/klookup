// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROVIDER_LOCALDOCS_H
#define PROVIDER_LOCALDOCS_H

#include "localdocscanner.h"
#include "provideroptions.h"
#include "searchprovider.h"
#include <atomic>
#include <memory>

class ProviderLocalDocs : public SearchProvider, public SearchProviderOptions
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
    Q_INTERFACES(SearchProvider SearchProviderOptions)

public:
    explicit ProviderLocalDocs(QObject *parent = nullptr);
    ~ProviderLocalDocs() override;

    QString name() const override;
    QString id() const override;
    void search(const QString &query) override;
    SearchProvider *clone(QObject *parent = nullptr) const override;
    QWidget *createOptionsWidget(QWidget *parent = nullptr) const override;
    void saveOptions(QWidget *widget) override;

    void setRootsForTesting(const QList<LocalScanRoot> &roots);
    QList<LocalScanRoot> roots() const;
    void cancel();

private:
    struct GuiSlot {
        ProviderLocalDocs *ptr = nullptr;
    };
    QList<LocalScanRoot> m_roots;
    bool m_hasCustomRoots = false;
    quint64 m_requestSeq = 0;
    std::shared_ptr<GuiSlot> m_guiSlot;
    std::shared_ptr<std::atomic<quint64>> m_latestSeq;
    std::shared_ptr<std::atomic_bool> m_activeCancel;
};

#endif // PROVIDER_LOCALDOCS_H
