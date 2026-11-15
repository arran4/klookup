#include "searchdashboard.h"
#include <KLocalizedString>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QIcon>
#include <QShortcut>
#include <QStringListModel>
#include <QVBoxLayout>

SearchDashboard::SearchDashboard(QWidget *parent) : QWidget(parent), m_searchBar(new QLineEdit(this)), m_completer(new QCompleter(this))
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    m_searchBar->setPlaceholderText(QStringLiteral("Enter search query..."));
    m_searchBar->setCompleter(m_completer);
    layout->addWidget(m_searchBar);

    auto *searchButtonsLayout = new QHBoxLayout;
    QPushButton *searchButton = new QPushButton(QStringLiteral("Search"), this);
    searchButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-find")));
    QPushButton *searchNewWindowButton = new QPushButton(QStringLiteral("Search in New Window"), this);
    searchNewWindowButton->setObjectName(QStringLiteral("searchNewWindow"));
    searchNewWindowButton->setIcon(QIcon::fromTheme(QStringLiteral("window-new")));
    searchButtonsLayout->addWidget(searchButton);
    searchButtonsLayout->addWidget(searchNewWindowButton);
    layout->addLayout(searchButtonsLayout);

    m_scopeWidget = new QWidget(this);
    m_scopeWidget->setObjectName(QStringLiteral("preQueryScopeSection"));
    auto *scopeBox = new QVBoxLayout(m_scopeWidget);
    scopeBox->setContentsMargins(0, 0, 0, 0);
    scopeBox->setSpacing(4);
    auto *scopeHeader = new QHBoxLayout;
    scopeHeader->setContentsMargins(0, 0, 0, 0);
    m_scopePreview = new QLabel(i18n("Scope: all enabled sources"), m_scopeWidget);
    m_scopePreview->setObjectName(QStringLiteral("preQueryScopePreview"));
    m_scopePreview->setTextFormat(Qt::PlainText);
    scopeHeader->addWidget(m_scopePreview, 1);
    m_clearScope = new QToolButton(m_scopeWidget);
    m_clearScope->setObjectName(QStringLiteral("clearPreQueryScope"));
    m_clearScope->setText(QStringLiteral("×"));
    m_clearScope->setToolTip(i18n("Reset to all enabled sources"));
    m_clearScope->setAccessibleName(i18n("Reset pre-query source scope"));
    m_clearScope->setAutoRaise(true);
    m_clearScope->setVisible(false);
    connect(m_clearScope, &QToolButton::clicked, this, &SearchDashboard::clearScopeSelection);
    scopeHeader->addWidget(m_clearScope);
    scopeBox->addLayout(scopeHeader);
    m_scopeLayout = new QHBoxLayout;
    m_scopeLayout->setContentsMargins(0, 0, 0, 0);
    m_scopeLayout->setSpacing(4);
    scopeBox->addLayout(m_scopeLayout);
    m_scopeWidget->setVisible(false);
    layout->addWidget(m_scopeWidget);

    m_recentSection = new QWidget(this);
    m_recentSection->setObjectName(QStringLiteral("recentActivitySection"));
    auto *recentLayout = new QVBoxLayout(m_recentSection);
    recentLayout->addWidget(new QLabel(QStringLiteral("Recent activity"), m_recentSection));
    m_recentList = new QListWidget(m_recentSection);
    m_recentList->setObjectName(QStringLiteral("recentActivityList"));
    m_recentList->setAccessibleName(QStringLiteral("Recent searches, viewed results and captured clipboard items"));
    recentLayout->addWidget(m_recentList);
    m_recentEmpty = new QLabel(QStringLiteral("No recent activity yet."), m_recentSection);
    recentLayout->addWidget(m_recentEmpty);
    auto *buttons = new QHBoxLayout;
    auto *open = new QPushButton(QStringLiteral("Revisit / search"), m_recentSection);
    open->setObjectName(QStringLiteral("openRecentItem"));
    m_removeRecent = new QPushButton(QStringLiteral("Remove selected"), m_recentSection);
    m_removeRecent->setObjectName(QStringLiteral("removeRecentItem"));
    m_clearRecent = new QPushButton(QStringLiteral("Clear recent activity…"), m_recentSection);
    m_clearRecent->setObjectName(QStringLiteral("clearRecentActivity"));
    for (auto *button : {open, m_removeRecent, m_clearRecent})
        buttons->addWidget(button);
    recentLayout->addLayout(buttons);
    layout->addWidget(m_recentSection);
    auto activate = [this](QListWidgetItem *item) {
        if (item)
            emit recentActivated(item->data(Qt::UserRole).value<RecentItem>());
    };
    connect(m_recentList, &QListWidget::itemActivated, this, activate);
    connect(open, &QPushButton::clicked, this, [this, activate] { activate(m_recentList->currentItem()); });
    auto remove = [this] {
        auto *item = m_recentList->currentItem();
        if (m_recent && item)
            m_recent->remove(item->data(Qt::UserRole).value<RecentItem>().key);
    };
    connect(m_removeRecent, &QPushButton::clicked, this, remove);
    auto *deleteKey = new QShortcut(QKeySequence::Delete, m_recentList);
    deleteKey->setContext(Qt::WidgetShortcut);
    connect(deleteKey, &QShortcut::activated, this, remove);
    connect(m_recentList, &QListWidget::currentRowChanged, this, [this, open](int row) {
        open->setEnabled(row >= 0);
        m_removeRecent->setEnabled(row >= 0);
    });
    connect(m_clearRecent, &QPushButton::clicked, this, [this] {
        if (m_recent &&
            QMessageBox::question(
                this, QStringLiteral("Clear recent activity"),
                QStringLiteral("Remove all recent activity, including saved entries? Search autocomplete and open tabs are kept.")) ==
                QMessageBox::Yes)
            m_recent->clear();
    });
    refreshRecent();
    layout->addStretch();

    connect(m_searchBar, &QLineEdit::returnPressed, this, &SearchDashboard::onSearchTriggered);
    connect(searchButton, &QPushButton::clicked, this, &SearchDashboard::onSearchTriggered);
    connect(searchNewWindowButton, &QPushButton::clicked, this, [this] {
        const QString query = m_searchBar->text().trimmed();
        if (!query.isEmpty()) {
            const QSet<QString> scoped = selectedSourceIds();
            emit searchInNewWindowScopedRequested(query, scoped);
            emit searchInNewWindowRequested(query);
            clearScopeSelection();
        }
    });
}

SearchDashboard::~SearchDashboard() {}

void SearchDashboard::loadHistory(const QStringList &history)
{
    m_history = history;
    m_completer->setModel(new QStringListModel(m_history, m_completer));
}

QStringList SearchDashboard::saveHistory() const
{
    return m_history;
}

void SearchDashboard::prepareQuery(const QString &query, bool focus)
{
    m_searchBar->setText(query);
    m_searchBar->selectAll();
    if (focus)
        m_searchBar->setFocus();
}

void SearchDashboard::onSearchTriggered()
{
    QString query = m_searchBar->text().trimmed();
    if (!query.isEmpty()) {
        m_history.removeAll(query);
        m_history.prepend(query);
        while (m_history.count() > 50) {
            m_history.removeLast();
        }

        m_completer->setModel(new QStringListModel(m_history, m_completer));
        const QSet<QString> scoped = selectedSourceIds();
        emit searchScopedRequested(query, scoped);
        emit searchRequested(query);
        clearScopeSelection();
    }
}

void SearchDashboard::setAvailableScopes(const QList<QPair<QString, ProviderClassification>> &sources)
{
    m_availableSources = sources;
    if (m_scopeLayout) {
        while (QLayoutItem *item = m_scopeLayout->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
    }
    m_scopeButtons.clear();
    m_selectedScopeFacets.clear();

    QMap<QString, int> facetCounts;
    QMap<QString, QString> facetLabels;
    for (const auto &source : sources) {
        for (const QString &facet : source.second.facets) {
            facetCounts[facet] = facetCounts.value(facet, 0) + 1;
            if (!facetLabels.contains(facet))
                facetLabels.insert(facet, source.second.label(facet));
        }
    }

    for (auto it = facetCounts.cbegin(); it != facetCounts.cend(); ++it) {
        const QString &key = it.key();
        const int count = it.value();
        const QString labelText = facetLabels.value(key, key);
        auto *button = new QToolButton(m_scopeWidget);
        button->setObjectName(QStringLiteral("scopeTag_") + key);
        button->setText(i18n("%1 (%2)", labelText, count));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolTip(i18n("Limit this search to %1 (%2 sources)", labelText, count));
        button->setAccessibleName(i18n("Limit search scope to %1", labelText));
        connect(button, &QToolButton::toggled, this, [this, key](bool checked) {
            if (checked)
                m_selectedScopeFacets.insert(key);
            else
                m_selectedScopeFacets.remove(key);
            updateScopePreview();
        });
        m_scopeButtons.insert(key, button);
        if (m_scopeLayout)
            m_scopeLayout->addWidget(button);
    }
    if (m_scopeLayout)
        m_scopeLayout->addStretch(1);
    if (m_scopeWidget)
        m_scopeWidget->setVisible(!m_scopeButtons.isEmpty());
    updateScopePreview();
}

QSet<QString> SearchDashboard::selectedSourceIds() const
{
    if (m_selectedScopeFacets.isEmpty())
        return {};
    QSet<QString> ids;
    for (const auto &source : m_availableSources) {
        for (const QString &facet : m_selectedScopeFacets) {
            if (source.second.facets.contains(facet)) {
                ids.insert(source.first);
                break;
            }
        }
    }
    return ids;
}

void SearchDashboard::clearScopeSelection()
{
    m_selectedScopeFacets.clear();
    for (auto *button : std::as_const(m_scopeButtons)) {
        const QSignalBlocker block(button);
        button->setChecked(false);
    }
    updateScopePreview();
}

void SearchDashboard::updateScopePreview()
{
    if (!m_scopePreview || !m_clearScope)
        return;
    const int total = m_availableSources.size();
    if (m_selectedScopeFacets.isEmpty()) {
        m_scopePreview->setText(i18n("Scope: all %1 enabled sources (default)", total));
        m_clearScope->setVisible(false);
        return;
    }
    const int selected = selectedSourceIds().size();
    m_scopePreview->setText(i18n("Scope: %1 of %2 enabled sources", selected, total));
    m_clearScope->setVisible(true);
}

void SearchDashboard::setRecentActivity(RecentActivity *activity)
{
    if (m_recent)
        m_recent->disconnect(this);
    m_recent = activity;
    if (activity) {
        connect(activity, &RecentActivity::changed, this, &SearchDashboard::refreshRecent);
        connect(activity, &QObject::destroyed, this, &SearchDashboard::refreshRecent);
    }
    refreshRecent();
}
void SearchDashboard::refreshRecent()
{
    const auto *current = m_recentList->currentItem();
    const QString selected = current ? current->data(Qt::UserRole).value<RecentItem>().key : QString();
    m_recentList->clear();
    m_recentSection->setVisible(!m_recent || m_recent->shown());
    if (m_recent) {
        int count = 0;
        for (const auto &entry : m_recent->items()) {
            if (++count > 10)
                break;
            const QString kind =
                entry.kind == RecentItem::Kind::Viewed
                    ? QStringLiteral("Viewed result")
                    : (entry.kind == RecentItem::Kind::Captured ? QStringLiteral("Captured clipboard") : QStringLiteral("Searched query"));
            auto *item = new QListWidgetItem(
                QStringLiteral("%1 · %2\n%3\n%4")
                    .arg(kind, entry.source, entry.title, entry.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"))),
                m_recentList);
            item->setData(Qt::UserRole, QVariant::fromValue(entry));
            item->setToolTip(QStringLiteral("<p>%1</p>").arg(entry.snippet.toHtmlEscaped()));
            if (entry.key == selected)
                m_recentList->setCurrentItem(item);
        }
    }
    if (m_recentList->currentRow() < 0 && m_recentList->count())
        m_recentList->setCurrentRow(0);
    const bool empty = m_recentList->count() == 0;
    m_recentEmpty->setVisible(empty);
    m_recentList->setVisible(!empty);
    m_removeRecent->setEnabled(!empty);
    m_clearRecent->setEnabled(!empty);
    findChild<QPushButton *>(QStringLiteral("openRecentItem"))->setEnabled(!empty);
}

void SearchDashboard::focusQuery()
{
    m_searchBar->selectAll();
    m_searchBar->setFocus();
}
