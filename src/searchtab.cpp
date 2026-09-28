#include "searchtab.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QDesktopServices>
#include <QUrl>

SearchTab::SearchTab(const QString &query, QWidget *parent)
    : QWidget(parent)
    , m_query(query)
    , m_resultsTree(new QTreeWidget(this))
    , m_detailsView(new QTextBrowser(this))
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);

    QVBoxLayout *leftLayout = new QVBoxLayout();
    QLabel *queryLabel = new QLabel(QStringLiteral("Results for: ") + query, this);
    leftLayout->addWidget(queryLabel);

    m_resultsTree->setHeaderHidden(true);
    leftLayout->addWidget(m_resultsTree);

    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addWidget(m_detailsView, 2);

    connect(m_resultsTree, &QTreeWidget::itemClicked, this, &SearchTab::onResultClicked);

    m_detailsView->setOpenExternalLinks(true);
}

SearchTab::~SearchTab()
{
}

void SearchTab::addResults(const QString &providerName, const QList<SearchResult> &results)
{
    QList<QTreeWidgetItem*> items = m_resultsTree->findItems(providerName, Qt::MatchExactly, 0);
    QTreeWidgetItem *providerItem;
    if (items.isEmpty()) {
        providerItem = new QTreeWidgetItem(m_resultsTree);
        providerItem->setText(0, providerName);
        providerItem->setFlags(providerItem->flags() & ~Qt::ItemIsSelectable);
        m_resultsTree->addTopLevelItem(providerItem);
    } else {
        providerItem = items.first();
    }

    if (results.isEmpty()) {
        QTreeWidgetItem *item = new QTreeWidgetItem(providerItem);
        item->setText(0, QStringLiteral("No results"));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        return;
    }

    for (const SearchResult &res : results) {
        QTreeWidgetItem *item = new QTreeWidgetItem(providerItem);
        item->setText(0, res.title);
        item->setData(0, Qt::UserRole, QVariant::fromValue(res));
    }

    providerItem->setExpanded(true);
}

void SearchTab::setError(const QString &providerName, const QString &errorMsg)
{
    QList<QTreeWidgetItem*> items = m_resultsTree->findItems(providerName, Qt::MatchExactly, 0);
    QTreeWidgetItem *providerItem;
    if (items.isEmpty()) {
        providerItem = new QTreeWidgetItem(m_resultsTree);
        providerItem->setText(0, providerName);
        providerItem->setFlags(providerItem->flags() & ~Qt::ItemIsSelectable);
        m_resultsTree->addTopLevelItem(providerItem);
    } else {
        providerItem = items.first();
    }

    QTreeWidgetItem *item = new QTreeWidgetItem(providerItem);
    item->setText(0, QStringLiteral("Error: ") + errorMsg);
    item->setForeground(0, Qt::red);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
}

void SearchTab::onResultClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    QVariant data = item->data(0, Qt::UserRole);
    if (!data.isValid()) {
        m_detailsView->clear();
        return;
    }

    SearchResult res = data.value<SearchResult>();

    QString html = QStringLiteral("<h2>%1</h2>").arg(res.title.toHtmlEscaped());
    if (!res.type.isEmpty()) {
        html += QStringLiteral("<p><b>Type:</b> %1</p>").arg(res.type.toHtmlEscaped());
    }
    if (!res.description.isEmpty()) {
        html += QStringLiteral("<p>%1</p>").arg(res.description.toHtmlEscaped());
    }

    if (!res.url.isEmpty()) {
        QUrl qurl(res.url);
        if (qurl.isValid() && (qurl.scheme() == QStringLiteral("http") || qurl.scheme() == QStringLiteral("https"))) {
            html += QStringLiteral("<p><a href=\"%1\">Open in Browser</a></p>").arg(res.url.toHtmlEscaped());
        } else {
            html += QStringLiteral("<p>Invalid or unsupported URL: %1</p>").arg(res.url.toHtmlEscaped());
        }
    }

    if (!res.additionalData.isEmpty()) {
        html += QStringLiteral("<h3>Additional Info</h3><ul>");
        for (auto it = res.additionalData.cbegin(), end = res.additionalData.cend(); it != end; ++it) {
            html += QStringLiteral("<li><b>%1:</b> %2</li>").arg(it.key().toHtmlEscaped(), it.value().toString().toHtmlEscaped());
        }
        html += QStringLiteral("</ul>");
    }

    m_detailsView->setHtml(html);
}

