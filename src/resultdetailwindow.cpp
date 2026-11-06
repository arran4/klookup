// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "resultdetailwindow.h"
#include "splitterkeyboard.h"
#include "appicon.h"
#include "sourcelink.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QSplitterHandle>
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

    m_splitter = new QSplitter(Qt::Vertical, central);
    m_splitter->setObjectName(QStringLiteral("detailWindowSplitter"));

    // Description / content view
    m_detailsView = new QTextBrowser(m_splitter);
    m_detailsView->setObjectName(QStringLiteral("detailsView"));
    m_detailsView->setMinimumHeight(60);
    m_detailsView->setOpenExternalLinks(false);
    m_detailsView->setOpenLinks(false);
    m_detailsView->document()->setResourceProvider([](const QUrl &) { return QVariant(QByteArray()); });
    connect(m_detailsView, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        const auto safe = Metadata::safeWebUrl(url.toString(QUrl::FullyEncoded));
        if (!safe.isEmpty()) {
            QDesktopServices::openUrl(safe);
        }
    });

    m_detailsView->setHtml(Content::renderResultHtml(m_result, content, providerName));
    m_splitter->addWidget(m_detailsView);

    // Structured metadata tree
    m_metadataView = new QTreeWidget(m_splitter);
    m_metadataView->setObjectName(QStringLiteral("structuredMetadata"));
    m_metadataView->setHeaderLabels({QStringLiteral("Field / Category"), QStringLiteral("Value")});
    m_metadataView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_metadataView->setWordWrap(true);
    m_metadataView->setMinimumHeight(50);
    connect(m_metadataView, &QTreeWidget::itemActivated, this, [](QTreeWidgetItem *item, int) {
        const auto url = Metadata::safeWebUrl(item->data(1, Qt::UserRole).toString());
        if (!url.isEmpty()) {
            QDesktopServices::openUrl(url);
        }
    });
    populateMetadata(metadata);
    m_splitter->addWidget(m_metadataView);

    m_splitter->setChildrenCollapsible(true);
    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, true);

    layout->addWidget(m_splitter, 1);

    if (auto *handle = m_splitter->handle(1)) {
        handle->setObjectName(QStringLiteral("detailWindowSplitterHandle"));
        handle->setFocusPolicy(Qt::StrongFocus);
        handle->setAccessibleName(QStringLiteral("Detail window summary and metadata splitter"));
        handle->setAccessibleDescription(QStringLiteral("Use up and down arrow keys to resize the summary and metadata panes. Double-click or press Enter to reset."));
        handle->installEventFilter(this);
    }

    connect(m_splitter, &QSplitter::splitterMoved, this, &ResultDetailWindow::saveLayout);

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

    restoreLayout();
}

void ResultDetailWindow::populateMetadata(const ResultMetadata &metadata)
{
    Metadata::populateMetadataTree(m_metadataView, metadata);
}

QSplitter *ResultDetailWindow::splitter() const
{
    return m_splitter;
}

void ResultDetailWindow::saveLayout()
{
    if (!m_splitter || (m_metadataView && !m_metadataView->isVisible()))
        return;
    const auto sizes = m_splitter->sizes();
    if (sizes.size() == 2 && sizes[0] > 0) {
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
        group.writeEntry(QStringLiteral("DetailWindowSplitter"), sizes);
        group.sync();
    }
}

void ResultDetailWindow::restoreLayout()
{
    if (!m_splitter)
        return;
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
    const QList<int> defaultSizes = {250, 200};
    QList<int> sizes = group.readEntry(QStringLiteral("DetailWindowSplitter"), defaultSizes);
    if (sizes.size() == 2 && sizes[0] > 0 && sizes[1] >= 0) {
        m_splitter->setSizes(sizes);
    } else {
        m_splitter->setSizes(defaultSizes);
    }
}

void ResultDetailWindow::resetSplitterSizes()
{
    if (!m_splitter)
        return;
    const int total = m_splitter->height();
    if (total > 0) {
        const int top = qMax(60, total / 2);
        const int bottom = qMax(50, total - top);
        m_splitter->setSizes({top, bottom});
    } else {
        m_splitter->setSizes({250, 200});
    }
    saveLayout();
}

bool ResultDetailWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_splitter && watched == m_splitter->handle(1)) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            resetSplitterSizes();
            return true;
        } else if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (resizeSplitterWithKey(m_splitter, keyEvent)) return true;
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Space) {
                resetSplitterSizes();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
