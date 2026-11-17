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
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QRunnable>
#include <QSpinBox>
#include <QThreadPool>
#include <QVBoxLayout>

namespace
{
QThreadPool *localDocThreadPool()
{
    static QThreadPool *pool = []() {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(2);
        return p;
    }();
    return pool;
}

QString formatPreviewMessage(const LocalScanPreview &prev)
{
    QString msg = i18n("Discovered %1 readable file(s) (%2 KiB).", prev.fileCount, (prev.totalBytes + 1023) / 1024);
    if (prev.truncated)
        msg += QLatin1Char(' ') + i18n("Scan limits reached.");
    if (!prev.warnings.isEmpty())
        msg += QLatin1Char(' ') + prev.warnings.join(QLatin1String("; "));
    return msg;
}
} // namespace

ProviderLocalDocs::ProviderLocalDocs(QObject *parent)
    : SearchProvider(parent)
{
}

ProviderLocalDocs::~ProviderLocalDocs()
{
    cancel();
}

void ProviderLocalDocs::cancel()
{
    ++m_requestSeq;
    if (m_activeCancel)
        m_activeCancel->store(true);
    m_activeCancel.reset();
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
    cancel();
    const QList<LocalScanRoot> activeRoots = roots();
    const QString providerId = id();
    if (activeRoots.isEmpty()) {
        emit searchFailed(providerId, i18n("No local documentation scan roots configured in Settings."));
        return;
    }

    const quint64 generation = m_requestSeq;
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_activeCancel = cancelFlag;
    QPointer<ProviderLocalDocs> self(this);

    localDocThreadPool()->start(QRunnable::create([self, generation, providerId, query, activeRoots, cancelFlag]() {
        if (cancelFlag->load() || !self)
            return;
        bool wasCancelled = false;
        const QList<LocalDocMatch> matches =
            LocalDocScanner::search(query, activeRoots, LocalScanPolicy(), cancelFlag.get(), &wasCancelled);
        if (wasCancelled || cancelFlag->load() || !self)
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

        QMetaObject::invokeMethod(
            self,
            [self, generation, providerId, results]() {
                if (!self || self->m_requestSeq != generation)
                    return;
                emit self->searchCompleted(providerId, results);
            },
            Qt::QueuedConnection);
    }));
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
    auto previewCancel = std::make_shared<std::shared_ptr<std::atomic_bool>>();
    auto previewSeq = std::make_shared<quint64>(0);
    QPointer<QWidget> safeWidget(widget);
    QPointer<QLabel> safePreviewLabel(previewLabel);

    connect(widget, &QObject::destroyed, [previewCancel, previewSeq]() {
        ++(*previewSeq);
        if (*previewCancel)
            (*previewCancel)->store(true);
    });

    auto refreshUi = [list, safeWidget, safePreviewLabel, currentRoots, previewCancel, previewSeq]() {
        {
            const QSignalBlocker blocker(list);
            list->clear();
            for (const auto &r : *currentRoots) {
                auto *item = new QListWidgetItem(QStringLiteral("%1 (%2, depth %3)").arg(r.label, r.path).arg(r.maxDepth), list);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
            }
        }
        if (*previewCancel)
            (*previewCancel)->store(true);
        const quint64 gen = ++(*previewSeq);
        auto cancelFlag = std::make_shared<std::atomic_bool>(false);
        *previewCancel = cancelFlag;
        const QList<LocalScanRoot> snapshot = *currentRoots;
        if (safePreviewLabel)
            safePreviewLabel->setText(i18n("Scanning local documentation…"));

        localDocThreadPool()->start(QRunnable::create([safeWidget, safePreviewLabel, previewSeq, gen, snapshot, cancelFlag]() {
            if (cancelFlag->load() || !safeWidget)
                return;
            bool wasCancelled = false;
            const LocalScanPreview prev = LocalDocScanner::preview(snapshot, LocalScanPolicy(), cancelFlag.get(), &wasCancelled);
            if (wasCancelled || cancelFlag->load() || !safeWidget)
                return;
            const QString msg = formatPreviewMessage(prev);
            QMetaObject::invokeMethod(
                safeWidget,
                [safeWidget, safePreviewLabel, previewSeq, gen, msg]() {
                    if (!safeWidget || !safePreviewLabel || *previewSeq != gen)
                        return;
                    safePreviewLabel->setText(msg);
                },
                Qt::QueuedConnection);
        }));
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
