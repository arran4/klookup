// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "selectionpopupwindow.h"

#include <KLocalizedString>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

SelectionPopupWindow::SelectionPopupWindow(QWidget *parent)
    : QFrame(parent, Qt::Tool)
{
    setObjectName(QStringLiteral("selectionPopupWindow"));
    setFrameShape(QFrame::StyledPanel);
    setMinimumWidth(360);
    resize(420, 300);

    auto *layout = new QVBoxLayout(this);
    m_headerLabel = new QLabel(i18n("Selected-Text Lookup"), this);
    m_headerLabel->setObjectName(QStringLiteral("selectionPopupHeader"));
    m_headerLabel->setWordWrap(true);
    layout->addWidget(m_headerLabel);

    m_fallbackRow = new QWidget(this);
    m_fallbackRow->setObjectName(QStringLiteral("selectionFallbackRow"));
    auto *fbLayout = new QHBoxLayout(m_fallbackRow);
    fbLayout->setContentsMargins(0, 0, 0, 0);
    m_fallbackInput = new QLineEdit(m_fallbackRow);
    m_fallbackInput->setObjectName(QStringLiteral("selectionFallbackInput"));
    m_fallbackInput->setPlaceholderText(i18n("Enter or paste term to look up…"));
    m_fallbackSearchBtn = new QPushButton(i18n("Look up"), m_fallbackRow);
    m_fallbackSearchBtn->setObjectName(QStringLiteral("selectionFallbackSearchBtn"));
    fbLayout->addWidget(m_fallbackInput, 1);
    fbLayout->addWidget(m_fallbackSearchBtn);
    layout->addWidget(m_fallbackRow);
    m_fallbackRow->hide();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("selectionPopupStatus"));
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_oneTimeRemoteBtn = new QPushButton(this);
    m_oneTimeRemoteBtn->setObjectName(QStringLiteral("selectionOneTimeRemoteBtn"));
    m_oneTimeRemoteBtn->hide();
    layout->addWidget(m_oneTimeRemoteBtn);

    m_resultsList = new QListWidget(this);
    m_resultsList->setObjectName(QStringLiteral("selectionPopupResults"));
    layout->addWidget(m_resultsList, 1);

    auto *closeBtn = new QPushButton(i18n("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::hide);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);

    auto triggerFallback = [this]() {
        const QString text = m_fallbackInput->text().trimmed();
        if (text.isEmpty())
            return;
        dispatchToProviders(text, m_lastProviders, m_lastPolicy, false);
    };
    connect(m_fallbackSearchBtn, &QPushButton::clicked, this, triggerFallback);
    connect(m_fallbackInput, &QLineEdit::returnPressed, this, triggerFallback);
    connect(m_oneTimeRemoteBtn, &QPushButton::clicked, this, [this]() {
        if (m_pendingRemoteId.isEmpty() || m_pendingQuery.isEmpty())
            return;
        const QString remoteId = m_pendingRemoteId;
        const QString q = m_pendingQuery;
        m_oneTimeRemoteBtn->hide();
        dispatchToProviders(q, m_lastProviders, m_lastPolicy, false, remoteId);
    });
}

SelectionPopupWindow::~SelectionPopupWindow()
{
    clearActiveRunners();
}

void SelectionPopupWindow::clearActiveRunners()
{
    for (const QPointer<SearchProvider> &runner : m_activeRunners) {
        if (runner)
            runner->deleteLater();
    }
    m_activeRunners.clear();
}

QString SelectionPopupWindow::statusText() const
{
    return m_statusLabel->text();
}

int SelectionPopupWindow::resultCount() const
{
    return m_resultsList->count();
}

void SelectionPopupWindow::lookupText(const QString &query,
                                      const QList<SearchProvider *> &providers,
                                      const SelectedTextPolicy &policy,
                                      bool isAutomatic)
{
    setAttribute(Qt::WA_ShowWithoutActivating, isAutomatic);
    m_fallbackRow->hide();
    dispatchToProviders(query.trimmed(), providers, policy, isAutomatic);
}

void SelectionPopupWindow::openFallbackPrompt(const QString &reason,
                                                const QList<SearchProvider *> &providers,
                                                const SelectedTextPolicy &policy)
{
    clearActiveRunners();
    m_lastProviders = providers;
    m_lastPolicy = policy;
    m_resultsList->clear();
    m_oneTimeRemoteBtn->hide();
    m_headerLabel->setText(i18n("Selected-Text Lookup (Manual Fallback)"));
    m_statusLabel->setText(reason);
    m_fallbackInput->clear();
    m_fallbackRow->show();
    setAttribute(Qt::WA_ShowWithoutActivating, false);
}

void SelectionPopupWindow::dispatchToProviders(const QString &query,
                                               const QList<SearchProvider *> &providers,
                                               const SelectedTextPolicy &policy,
                                               bool isAutomatic,
                                               const QString &oneTimeApprovedRemoteId)
{
    clearActiveRunners();
    m_lastProviders = providers;
    m_lastPolicy = policy;
    m_resultsList->clear();
    m_oneTimeRemoteBtn->hide();
    m_pendingRemoteId.clear();
    m_pendingQuery.clear();

    m_headerLabel->setText(i18n("Lookup: %1", query));

    if (!policy.enabled) {
        m_statusLabel->setText(i18n("Selected-text lookup is disabled in Settings."));
        return;
    }

    QList<SearchProvider *> eligible;
    SearchProvider *firstUnapprovedRemote = nullptr;
    for (SearchProvider *p : providers) {
        if (!p || !p->isEnabled())
            continue;
        const bool local = SelectedTextPolicy::isLocalProvider(p);
        const bool oneTime = (!isAutomatic && !oneTimeApprovedRemoteId.isEmpty() && p->id() == oneTimeApprovedRemoteId);
        if (policy.isProviderAllowed(p->id(), local, isAutomatic, oneTime)) {
            eligible.append(p);
        } else if (!isAutomatic && !local && !firstUnapprovedRemote) {
            firstUnapprovedRemote = p;
        }
    }

    if (eligible.isEmpty()) {
        if (isAutomatic) {
            m_statusLabel->setText(i18n("No local lookup source is enabled or configured. Automatic selected-text lookup never sends text to remote providers."));
        } else if (firstUnapprovedRemote) {
            m_pendingRemoteId = firstUnapprovedRemote->id();
            m_pendingQuery = query;
            m_statusLabel->setText(i18n("No local lookup source is enabled, and remote selection disclosure is disabled by default."));
            m_oneTimeRemoteBtn->setText(i18n("Send once to %1", firstUnapprovedRemote->name()));
            m_oneTimeRemoteBtn->show();
        } else {
            m_statusLabel->setText(i18n("No local or approved lookup source is enabled."));
        }
        return;
    }

    m_statusLabel->setText(i18n("Searching %1 eligible source(s)…", eligible.size()));
    for (SearchProvider *p : eligible) {
        SearchProvider *runner = p->cloneForSearch(this);
        if (!runner)
            continue;
        m_activeRunners.append(runner);
        connect(runner, &SearchProvider::searchCompleted, this, [this](const QString &pid, const QList<SearchResult> &results) {
            for (const SearchResult &res : results) {
                const QString line = res.description.isEmpty()
                    ? QStringLiteral("[%1] %2").arg(pid, res.title)
                    : QStringLiteral("[%1] %2 — %3").arg(pid, res.title, res.description);
                m_resultsList->addItem(line);
            }
            m_statusLabel->setText(i18n("Found %1 result(s).", m_resultsList->count()));
        });
        connect(runner, &SearchProvider::searchFailed, this, [this](const QString &pid, const QString &err) {
            if (m_resultsList->count() == 0)
                m_statusLabel->setText(i18n("%1: %2", pid, err));
        });
        emit lookupDispatched(p->id(), query);
        runner->search(query);
    }
}
