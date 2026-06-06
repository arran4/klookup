#include "searchdashboard.h"
#include <QVBoxLayout>
#include <QPushButton>

SearchDashboard::SearchDashboard(QWidget *parent)
    : QWidget(parent)
    , m_searchBar(new QLineEdit(this))
    , m_historyList(new QListWidget(this))
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    m_searchBar->setPlaceholderText(QStringLiteral("Enter search query..."));
    layout->addWidget(m_searchBar);

    QPushButton *searchButton = new QPushButton(QStringLiteral("Search"), this);
    layout->addWidget(searchButton);

    layout->addWidget(m_historyList);

    connect(m_searchBar, &QLineEdit::returnPressed, this, &SearchDashboard::onSearchTriggered);
    connect(searchButton, &QPushButton::clicked, this, &SearchDashboard::onSearchTriggered);
    connect(m_historyList, &QListWidget::itemClicked, this, &SearchDashboard::onHistoryItemClicked);
}

SearchDashboard::~SearchDashboard()
{
}

void SearchDashboard::loadHistory(const QStringList &history)
{
    m_historyList->clear();
    m_historyList->addItems(history);
}

QStringList SearchDashboard::saveHistory() const
{
    QStringList history;
    for (int i = 0; i < m_historyList->count(); ++i) {
        history.append(m_historyList->item(i)->text());
    }
    return history;
}

void SearchDashboard::onSearchTriggered()
{
    QString query = m_searchBar->text().trimmed();
    if (!query.isEmpty()) {
        for (int i = 0; i < m_historyList->count(); ++i) {
            if (m_historyList->item(i)->text() == query) {
                delete m_historyList->takeItem(i);
                break;
            }
        }
        m_historyList->insertItem(0, query);
        while (m_historyList->count() > 50) {
            delete m_historyList->takeItem(m_historyList->count() - 1);
        }
        emit searchRequested(query);
    }
}

void SearchDashboard::onHistoryItemClicked(QListWidgetItem *item)
{
    m_searchBar->setText(item->text());
    onSearchTriggered();
}
