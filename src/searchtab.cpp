#include "searchtab.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QDesktopServices>
#include <QUrl>

SearchTab::SearchTab(const QString &query, QWidget *parent)
    : QWidget(parent)
    , m_query(query)
    , m_resultsList(new QListWidget(this))
    , m_detailsView(new QTextBrowser(this))
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);

    QVBoxLayout *leftLayout = new QVBoxLayout();
    QLabel *queryLabel = new QLabel(QStringLiteral("Results for: ") + query, this);
    leftLayout->addWidget(queryLabel);
    leftLayout->addWidget(m_resultsList);

    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addWidget(m_detailsView, 2);

    connect(m_resultsList, &QListWidget::itemClicked, this, &SearchTab::onResultClicked);

    m_detailsView->setOpenExternalLinks(true);
}

SearchTab::~SearchTab()
{
}

void SearchTab::addResults(const QString &providerName, const QList<SearchResult> &results)
{
    if (results.isEmpty()) {
        QListWidgetItem *item = new QListWidgetItem(QStringLiteral("No results from ") + providerName);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        m_resultsList->addItem(item);
        return;
    }

    for (const SearchResult &res : results) {
        QListWidgetItem *item = new QListWidgetItem(QStringLiteral("[") + providerName + QStringLiteral("] ") + res.title);
        m_resultsList->addItem(item);
        m_itemDataMap.insert(item, res);
    }
}

void SearchTab::setError(const QString &providerName, const QString &errorMsg)
{
    QListWidgetItem *item = new QListWidgetItem(QStringLiteral("Error from ") + providerName + QStringLiteral(": ") + errorMsg);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    item->setForeground(Qt::red);
    m_resultsList->addItem(item);
}

void SearchTab::onResultClicked(QListWidgetItem *item)
{
    if (!m_itemDataMap.contains(item)) {
        m_detailsView->clear();
        return;
    }

    SearchResult res = m_itemDataMap.value(item);

    QString html = QStringLiteral("<h2>%1</h2>").arg(res.title);
    html += QStringLiteral("<p><b>Type:</b> %1</p>").arg(res.type);
    html += QStringLiteral("<p>%1</p>").arg(res.description);

    if (!res.url.isEmpty()) {
        html += QStringLiteral("<p><a href=\"%1\">Open in Browser</a></p>").arg(res.url);
    }

    if (!res.additionalData.isEmpty()) {
        html += QStringLiteral("<h3>Additional Info</h3><ul>");
        for (auto it = res.additionalData.cbegin(), end = res.additionalData.cend(); it != end; ++it) {
            html += QStringLiteral("<li><b>%1:</b> %2</li>").arg(it.key(), it.value().toString());
        }
        html += QStringLiteral("</ul>");
    }

    m_detailsView->setHtml(html);
}
