// SPDX-FileCopyrightText: 2026 KLookup contributors
//
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
#include <functional>

ResultDetailWindow::ResultDetailWindow(const SearchResult &result,
                                       const QString &providerName,
                                       const ResultMetadata &metadata,
                                       const QString &contentHtml,
                                       QWidget *parent)
    : QMainWindow(parent)
    , m_result(result)
    , m_providerName(providerName)
    , m_metadata(metadata)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowIcon(applicationIcon());
    setWindowTitle(QStringLiteral("%1 - Details - KLookup").arg(result.title.isEmpty() ? QStringLiteral("Result") : result.title));
    resize(650, 500);

    setupUi(providerName, metadata, contentHtml);

    auto *closeShortcut = new QShortcut(QKeySequence::Close, this);
    connect(closeShortcut, &QShortcut::activated, this, &QWidget::close);
    auto *closeWindowShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
    connect(closeWindowShortcut, &QShortcut::activated, this, &QWidget::close);
    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, &QWidget::close);
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

void ResultDetailWindow::closeEvent(QCloseEvent *event)
{
    emit windowClosing(this);
    QMainWindow::closeEvent(event);
}

void ResultDetailWindow::setupUi(const QString &providerName, const ResultMetadata &metadata, const QString &contentHtml)
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    // Header info
    auto *headerLayout = new QVBoxLayout;
    auto *titleLabel = new QLabel(m_result.title, central);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleLabel->setFont(titleFont);
    titleLabel->setWordWrap(true);
    headerLayout->addWidget(titleLabel);

    QStringList subInfo;
    if (!providerName.isEmpty()) {
        subInfo.append(QStringLiteral("<b>Provider:</b> %1").arg(providerName.toHtmlEscaped()));
    }
    if (!m_result.type.isEmpty()) {
        subInfo.append(QStringLiteral("<b>Type:</b> %1").arg(m_result.type.toHtmlEscaped()));
    }
    if (!subInfo.isEmpty()) {
        auto *subLabel = new QLabel(subInfo.join(QStringLiteral(" &nbsp;|&nbsp; ")), central);
        subLabel->setTextFormat(Qt::RichText);
        headerLayout->addWidget(subLabel);
    }
    layout->addLayout(headerLayout);

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

    if (!contentHtml.isEmpty()) {
        m_detailsView->setHtml(contentHtml);
    } else if (!m_result.description.isEmpty()) {
        m_detailsView->setPlainText(m_result.description);
    } else {
        m_detailsView->setPlainText(QStringLiteral("No description provided."));
    }
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
    m_metadataView->clear();
    if (metadata.sections.isEmpty()) {
        m_metadataView->hide();
        return;
    }

    int remaining = 4000;
    std::function<void(QTreeWidgetItem *, const MetadataNode &, int)> append = [&](QTreeWidgetItem *parent, const MetadataNode &node, int depth) {
        if (remaining-- <= 0 || depth > 16)
            return;
        if (node.values.isEmpty() && node.children.isEmpty())
            return;
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_metadataView);
        item->setText(0, node.label);
        QStringList plainValues;
        int valueCount = 0;
        for (const auto &value : node.values) {
            if (++valueCount > 200 || remaining-- <= 0)
                break;
            const auto url = Metadata::safeWebUrl(value.url.toString(QUrl::FullyEncoded));
            if (value.kind == MetadataValue::Kind::Text || url.isEmpty()) {
                plainValues.append(value.text);
            } else {
                auto *link = new QTreeWidgetItem(item);
                link->setText(0, value.kind == MetadataValue::Kind::Image ? QStringLiteral("Image reference") : QStringLiteral("Source link"));
                link->setText(1, value.text.isEmpty() ? url.toDisplayString() : value.text);
                link->setToolTip(1, url.toDisplayString());
                link->setData(1, Qt::UserRole, url.toString(QUrl::FullyEncoded));
                link->setForeground(1, palette().link());
                link->setIcon(0, QIcon::fromTheme(QStringLiteral("internet-web-browser")));
            }
        }
        item->setText(1, plainValues.join(QStringLiteral("; ")));
        item->setToolTip(1, QStringLiteral("<p>%1</p>").arg(plainValues.join(QLatin1Char('\n')).toHtmlEscaped()));
        for (const auto &child : node.children) {
            append(item, child, depth + 1);
        }
        item->setExpanded(depth < 2);
    };

    for (const auto &sec : metadata.sections) {
        append(nullptr, sec, 0);
    }
    m_metadataView->show();
}
