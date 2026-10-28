#include "searchdashboard.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
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

    QPushButton *searchButton = new QPushButton(QStringLiteral("Search"), this);
    searchButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-find")));
    layout->addWidget(searchButton);

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
        emit searchRequested(query);
    }
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
