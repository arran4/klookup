// SPDX-FileCopyrightText: 2026 KLookup contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchwindow.h"
#include "appicon.h"
#include "providerauthmanager.h"
#include "searchtab.h"

#include <QKeySequence>
#include <QShortcut>

SearchWindow::SearchWindow(const QString &query,
                           const QList<SearchProvider *> &providers,
                           ProviderAuthManager *authManager,
                           QWidget *parent)
    : QMainWindow(parent)
    , m_query(query)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowIcon(applicationIcon());
    setWindowTitle(query.isEmpty() ? QStringLiteral("KLookup") : QStringLiteral("%1 - KLookup").arg(query));
    resize(850, 600);

    m_tab = new SearchTab(query, this);
    for (auto *providerTemplate : providers) {
        if (!providerTemplate || !providerTemplate->isEnabled()) {
            continue;
        }
        SearchProvider *cloned = providerTemplate->clone(m_tab);
        m_tab->startProvider(cloned, authManager ? authManager->sessionFor(providerTemplate) : nullptr);
    }
    setCentralWidget(m_tab);

    auto *closeShortcut = new QShortcut(QKeySequence::Close, this);
    connect(closeShortcut, &QShortcut::activated, this, &QWidget::close);
    auto *closeWindowShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
    connect(closeWindowShortcut, &QShortcut::activated, this, &QWidget::close);
    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, &QWidget::close);
}

QString SearchWindow::query() const
{
    return m_query;
}

SearchTab *SearchWindow::searchTab() const
{
    return m_tab;
}

void SearchWindow::closeEvent(QCloseEvent *event)
{
    emit windowClosing(this);
    QMainWindow::closeEvent(event);
}
