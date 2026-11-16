// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "provider_localdocs.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ProviderLocalDocs::ProviderLocalDocs(QObject *parent)
    : SearchProvider(parent)
{
}

ProviderLocalDocs::~ProviderLocalDocs()
{
    m_cancelled.store(true);
}

QString ProviderLocalDocs::name() const
{
    return i18n("Local Documentation");
}

QString ProviderLocalDocs::id() const
{
    return QStringLiteral("localdocs");
}

void ProviderLocalDocs::setRootsForTesting(const QList<LocalScanRoot> &roots)
{
    m_roots = roots;
    m_hasCustomRoots = true;
}

QList<LocalScanRoot> ProviderLocalDocs::roots() const
{
    if (m_hasCustomRoots)
        return m_roots;
    const KConfigGroup cfg(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Provider-localdocs"));
    return LocalDocScanner::readRoots(cfg);
}

void ProviderLocalDocs::search(const QString &query)
{
    m_cancelled.store(false);
    const QList<LocalScanRoot> activeRoots = roots();
    if (activeRoots.isEmpty()) {
        emit searchFailed(id(), i18n("No local documentation scan roots configured in Settings."));
        return;
    }

    bool cancelled = false;
    const QList<LocalDocMatch> matches = LocalDocScanner::search(query, activeRoots, LocalScanPolicy(), &m_cancelled, &cancelled);
    if (cancelled)
        return;

    QList<SearchResult> results;
    results.reserve(matches.size());
    for (const auto &match : matches) {
        SearchResult res;
        res.id = match.id;
        res.title = match.title;
        res.description = match.snippet;
        res.type = QStringLiteral("local-document");
        res.additionalData.insert(QStringLiteral("root"), match.rootLabel);
        res.additionalData.insert(QStringLiteral("relativePath"), match.relativePath);
        results.append(res);
    }
    emit searchCompleted(id(), results);
}

SearchProvider *ProviderLocalDocs::clone(QObject *parent) const
{
    auto *copy = new ProviderLocalDocs(parent);
    copy->setEnabled(isEnabled());
    if (m_hasCustomRoots)
        copy->setRootsForTesting(m_roots);
    return copy;
}

QWidget *ProviderLocalDocs::createOptionsWidget(QWidget *parent) const
{
    auto *widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("localDocsOptions"));
    auto *layout = new QVBoxLayout(widget);

    auto *hint = new QLabel(i18n("Opt-in offline documentation folders (.md, .markdown, .html, .htm, .txt). "
                                 "Symlinks and hidden directories are skipped, and scan roots are kept private."),
                            widget);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *list = new QListWidget(widget);
    list->setObjectName(QStringLiteral("localDocRoots"));
    layout->addWidget(list, 1);

    auto *previewLabel = new QLabel(widget);
    previewLabel->setObjectName(QStringLiteral("localDocPreview"));
    previewLabel->setWordWrap(true);
    layout->addWidget(previewLabel);

    auto currentRoots = std::make_shared<QList<LocalScanRoot>>(roots());
    auto refreshUi = [list, previewLabel, currentRoots]() {
        list->clear();
        for (const auto &r : *currentRoots) {
            auto *item = new QListWidgetItem(QStringLiteral("%1 (%2, depth %3)").arg(r.label, r.path).arg(r.maxDepth), list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        }
        const LocalScanPreview prev = LocalDocScanner::preview(*currentRoots);
        QString msg = i18n("Discovered %1 readable file(s) (%2 KiB).", prev.fileCount, (prev.totalBytes + 1023) / 1024);
        if (prev.truncated)
            msg += QLatin1Char(' ') + i18n("Scan limits reached.");
        if (!prev.warnings.isEmpty())
            msg += QLatin1Char(' ') + prev.warnings.join(QLatin1String("; "));
        previewLabel->setText(msg);
    };
    auto persist = [currentRoots]() {
        KConfigGroup cfg(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Provider-localdocs"));
        LocalDocScanner::writeRoots(cfg, *currentRoots);
        cfg.sync();
    };

    auto *controls = new QHBoxLayout;
    auto *depthSpin = new QSpinBox(widget);
    depthSpin->setRange(0, 8);
    depthSpin->setValue(3);
    depthSpin->setPrefix(i18n("Depth: "));
    auto *addBtn = new QPushButton(i18n("Add folder…"), widget);
    addBtn->setObjectName(QStringLiteral("addLocalDocRoot"));
    auto *removeBtn = new QPushButton(i18n("Remove"), widget);
    removeBtn->setObjectName(QStringLiteral("removeLocalDocRoot"));
    auto *rescanBtn = new QPushButton(i18n("Rescan"), widget);
    rescanBtn->setObjectName(QStringLiteral("rescanLocalDocs"));
    controls->addWidget(depthSpin);
    controls->addWidget(addBtn);
    controls->addWidget(removeBtn);
    controls->addWidget(rescanBtn);
    layout->addLayout(controls);

    connect(list, &QListWidget::itemChanged, widget, [list, currentRoots, persist, refreshUi](QListWidgetItem *item) {
        const int row = list->row(item);
        if (row >= 0 && row < currentRoots->size()) {
            (*currentRoots)[row].enabled = (item->checkState() == Qt::Checked);
            persist();
            refreshUi();
        }
    });
    connect(addBtn, &QPushButton::clicked, widget, [widget, depthSpin, currentRoots, persist, refreshUi] {
        const QString dir = QFileDialog::getExistingDirectory(widget, i18n("Select Local Documentation Folder"));
        if (dir.isEmpty())
            return;
        LocalScanRoot root;
        root.id = QStringLiteral("root-%1").arg(currentRoots->size());
        root.label = QFileInfo(dir).fileName();
        root.path = dir;
        root.maxDepth = depthSpin->value();
        root.enabled = true;
        currentRoots->append(root);
        persist();
        refreshUi();
    });
    connect(removeBtn, &QPushButton::clicked, widget, [list, currentRoots, persist, refreshUi] {
        const int row = list->currentRow();
        if (row >= 0 && row < currentRoots->size()) {
            currentRoots->removeAt(row);
            persist();
            refreshUi();
        }
    });
    connect(rescanBtn, &QPushButton::clicked, widget, refreshUi);

    refreshUi();
    return widget;
}

void ProviderLocalDocs::saveOptions(QWidget *)
{
    // Root changes are persisted immediately upon add/remove/toggle.
}
