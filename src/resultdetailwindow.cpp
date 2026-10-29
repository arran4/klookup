// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "resultdetailwindow.h"
#include "appicon.h"
#include "sourcelink.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

ResultDetailWindow::ResultDetailWindow(const SearchResult &result,
                                       const QString &providerName,
                                       const ResultMetadata &metadata,
                                       const ResultContent &content,
                                       QWidget *parent)
    : QMainWindow(parent)
    , m_result(result)
    , m_providerName(providerName)
    , m_metadata(metadata)
    , m_content(content)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowIcon(applicationIcon());
    setWindowTitle(QStringLiteral("%1 - Details - KLookup").arg(result.title.isEmpty() ? QStringLiteral("Result") : result.title));
    resize(650, 500);

    setupUi(providerName, metadata, content);

    auto *closeShortcut = new QShortcut(QKeySequence::Close, this);
    connect(closeShortcut, &QShortcut::activated, this, &QWidget::close);
    auto *closeWindowShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
    connect(closeWindowShortcut, &QShortcut::activated, this, &QWidget::close);
    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, &QWidget::close);
}

ResultDetailWindow::ResultDetailWindow(const SearchResult &result,
                                       const QString &providerName,
                                       const ResultMetadata &metadata,
                                       const QString &contentHtml,
                                       QWidget *parent)
    : ResultDetailWindow(result, providerName, metadata,
                         ResultContent{contentHtml, ResultContent::Format::LimitedHtml}, parent)
{
}

SearchResult ResultDetailWindow::result() const
{
    return m_result;
}

QString ResultDetailWindow::providerName() const
{
    return m_providerName;
}

ResultMetadata ResultDetailWindow::metadata() const
{
    return m_metadata;
}

ResultContent ResultDetailWindow::content() const
{
    return m_content;
}

void ResultDetailWindow::closeEvent(QCloseEvent *event)
{
    emit windowClosing(this);
    QMainWindow::closeEvent(event);
}

void ResultDetailWindow::setupUi(const QString &providerName, const ResultMetadata &metadata, const ResultContent &content)
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    // Description / content view
    m_detailsView = new QTextBrowser(central);
    m_detailsView->setOpenExternalLinks(false);
    m_detailsView->setOpenLinks(false);
    connect(m_detailsView, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        const auto safe = Metadata::safeWebUrl(url.toString(QUrl::FullyEncoded));
        if (!safe.isEmpty()) {
            QDesktopServices::openUrl(safe);
        }
    });

    m_detailsView->setHtml(Content::renderResultHtml(m_result, content, providerName));
    layout->addWidget(m_detailsView, 2);

    // Structured metadata tree
    m_metadataView = new QTreeWidget(central);
    m_metadataView->setHeaderLabels({QStringLiteral("Field / Category"), QStringLiteral("Value")});
    m_metadataView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_metadataView->setWordWrap(true);
    connect(m_metadataView, &QTreeWidget::itemActivated, this, [](QTreeWidgetItem *item, int) {
        const auto url = Metadata::safeWebUrl(item->data(1, Qt::UserRole).toString());
        if (!url.isEmpty()) {
            QDesktopServices::openUrl(url);
        }
    });
    populateMetadata(metadata);
    layout->addWidget(m_metadataView, 1);

    // Source link and bottom action buttons
    auto *bottomLayout = new QHBoxLayout;
    m_sourceLink = new SourceLink(central);
    m_sourceLink->setResult(m_result);
    bottomLayout->addWidget(m_sourceLink);
    bottomLayout->addStretch();

    auto *closeButton = new QPushButton(QStringLiteral("Close"), central);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    bottomLayout->addWidget(closeButton);

    layout->addLayout(bottomLayout);
    setCentralWidget(central);
}

void ResultDetailWindow::populateMetadata(const ResultMetadata &metadata)
{
    Metadata::populateMetadataTree(m_metadataView, metadata);
}
