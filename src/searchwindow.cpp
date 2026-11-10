// SPDX-FileCopyrightText: 2026 KLookup contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchwindow.h"
#include "appicon.h"
#include "providerauthmanager.h"
#include "searchtab.h"

#include <QKeySequence>
#include <QShortcut>
#include <QAction>
#include <QToolBar>

SearchWindow::SearchWindow(const QString &query,
                           const QList<SearchProvider *> &providers,
                           ProviderAuthManager *authManager,
                           QWidget *parent)
    : SearchWindow(new SearchTab(query), parent)
{
    for (auto *providerTemplate : providers) {
        if (!providerTemplate || !providerTemplate->isEnabled()) continue;
        m_tab->startProvider(providerTemplate->cloneForSearch(m_tab), authManager ? authManager->sessionFor(providerTemplate) : nullptr);
    }
}

SearchWindow::SearchWindow(SearchTab *tab, QWidget *parent)
    : QMainWindow(parent, Qt::Window), m_query(tab ? tab->query() : QString()), m_tab(tab)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowIcon(applicationIcon());
    setWindowTitle(m_query.isEmpty() ? QStringLiteral("KLookup") : QStringLiteral("%1 - KLookup").arg(m_query));
    resize(850, 600);
    if (tab) { setCentralWidget(tab); tab->setPoppedOut(true); }
    auto *toolbar = addToolBar(QStringLiteral("Search window"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *returnAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("view-restore")), QStringLiteral("Return to Main Window"));
    returnAction->setObjectName(QStringLiteral("returnSearchToMainWindow"));
    connect(returnAction, &QAction::triggered, this, [this] { emit returnToMainRequested(this); });

    auto *closeShortcut = new QShortcut(QKeySequence::Close, this);
    connect(closeShortcut, &QShortcut::activated, this, &QWidget::close);
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

SearchTab *SearchWindow::takeSearchTab()
{
    auto *tab = qobject_cast<SearchTab *>(takeCentralWidget());
    m_tab = nullptr;
    if (tab) tab->setPoppedOut(false);
    return tab;
}
