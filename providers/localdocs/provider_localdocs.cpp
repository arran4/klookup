// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "provider_localdocs.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QCoreApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QRunnable>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QThreadPool>
#include <QVBoxLayout>

namespace
{
struct OptionsGuiSlot {
    QWidget *widget = nullptr;
    QLabel *label = nullptr;
    quint64 seq = 0;
};

QThreadPool *localDocThreadPool()
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return QThreadPool::globalInstance();
    auto *existing = app->findChild<QThreadPool *>(QStringLiteral("klookupLocalDocPool"), Qt::FindDirectChildrenOnly);
    if (existing)
        return existing;
    auto *pool = new QThreadPool(app);
    pool->setObjectName(QStringLiteral("klookupLocalDocPool"));
    pool->setMaxThreadCount(2);
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
    , m_guiSlot(std::make_shared<GuiSlot>(GuiSlot{this}))
    , m_latestSeq(std::make_shared<std::atomic<quint64>>(0))
{
}

ProviderLocalDocs::~ProviderLocalDocs()
{
    cancel();
    if (m_guiSlot)
        m_guiSlot->ptr = nullptr;
}

void ProviderLocalDocs::cancel()
{
    ++m_requestSeq;
    m_latestSeq->store(m_requestSeq);
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
    const std::weak_ptr<GuiSlot> weakSlot = m_guiSlot;
    const std::shared_ptr<std::atomic<quint64>> latestSeq = m_latestSeq;
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_activeCancel = cancelFlag;

    localDocThreadPool()->start(QRunnable::create([weakSlot, generation, latestSeq, providerId, query, activeRoots, cancelFlag]() {
        if (cancelFlag->load() || latestSeq->load() != generation)
            return;
        bool wasCancelled = false;
        const QList<LocalDocMatch> matches =
            LocalDocScanner::search(query, activeRoots, LocalScanPolicy(), cancelFlag.get(), &wasCancelled);
        if (wasCancelled || cancelFlag->load() || latestSeq->load() != generation)
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

        if (QCoreApplication *app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(
                app,
                [weakSlot, generation, providerId, results]() {
                    const auto slot = weakSlot.lock();
                    if (!slot || !slot->ptr || slot->ptr->m_requestSeq != generation)
                        return;
                    emit slot->ptr->searchCompleted(providerId, results);
                },
                Qt::QueuedConnection);
        }
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
    auto optionsSlot = std::make_shared<OptionsGuiSlot>(OptionsGuiSlot{widget, previewLabel, 0});
    auto previewSeq = std::make_shared<std::atomic<quint64>>(0);

    connect(widget, &QObject::destroyed, [previewCancel, previewSeq, optionsSlot]() {
        previewSeq->fetch_add(1);
        optionsSlot->widget = nullptr;
        optionsSlot->label = nullptr;
        if (*previewCancel)
            (*previewCancel)->store(true);
    });

    auto refreshUi = [list, previewLabel, currentRoots, previewCancel, previewSeq, optionsSlot]() {
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
        const quint64 gen = previewSeq->fetch_add(1) + 1;
        optionsSlot->seq = gen;
        auto cancelFlag = std::make_shared<std::atomic_bool>(false);
        *previewCancel = cancelFlag;
        const QList<LocalScanRoot> snapshot = *currentRoots;
        previewLabel->setText(i18n("Scanning local documentation…"));

        const std::weak_ptr<OptionsGuiSlot> weakOptions = optionsSlot;
        localDocThreadPool()->start(QRunnable::create([weakOptions, previewSeq, gen, snapshot, cancelFlag]() {
            if (cancelFlag->load() || previewSeq->load() != gen)
                return;
            bool wasCancelled = false;
            const LocalScanPreview prev = LocalDocScanner::preview(snapshot, LocalScanPolicy(), cancelFlag.get(), &wasCancelled);
            if (wasCancelled || cancelFlag->load() || previewSeq->load() != gen)
                return;
            const QString msg = formatPreviewMessage(prev);
            if (QCoreApplication *app = QCoreApplication::instance()) {
                QMetaObject::invokeMethod(
                    app,
                    [weakOptions, gen, msg]() {
                        const auto slot = weakOptions.lock();
                        if (!slot || !slot->widget || !slot->label || slot->seq != gen)
                            return;
                        slot->label->setText(msg);
                    },
                    Qt::QueuedConnection);
            }
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
