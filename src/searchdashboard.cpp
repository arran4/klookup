#include "searchdashboard.h"
#include <QVBoxLayout>
#include <QPushButton>
#include <QIcon>
#include <QStringListModel>

SearchDashboard::SearchDashboard(QWidget *parent)
    : QWidget(parent)
    , m_searchBar(new QLineEdit(this))
    , m_completer(new QCompleter(this))
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    m_searchBar->setPlaceholderText(QStringLiteral("Enter search query..."));
    m_searchBar->setCompleter(m_completer);
    layout->addWidget(m_searchBar);

    QPushButton *searchButton = new QPushButton(QStringLiteral("Search"), this);
    searchButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-find")));
    layout->addWidget(searchButton);

    layout->addStretch();

    connect(m_searchBar, &QLineEdit::returnPressed, this, &SearchDashboard::onSearchTriggered);
    connect(searchButton, &QPushButton::clicked, this, &SearchDashboard::onSearchTriggered);
}

SearchDashboard::~SearchDashboard()
{
}

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
    if (focus) m_searchBar->setFocus();
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

void SearchDashboard::focusQuery()
{
    m_searchBar->selectAll();
    m_searchBar->setFocus();
}
