// SPDX-FileCopyrightText: 2026 KLookup contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RESULTDETAILWINDOW_H
#define RESULTDETAILWINDOW_H

#include "resultmetadata.h"
#include "searchprovider.h"
#include <QMainWindow>

class QLabel;
class QTextBrowser;
class QTreeWidget;
class SourceLink;

class ResultDetailWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ResultDetailWindow(const SearchResult &result,
                                const QString &providerName = QString(),
                                const ResultMetadata &metadata = ResultMetadata(),
                                const QString &contentHtml = QString(),
                                QWidget *parent = nullptr);
    ~ResultDetailWindow() override = default;

    SearchResult result() const;
    QString providerName() const;
    ResultMetadata metadata() const;

signals:
    void windowClosing(ResultDetailWindow *window);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupUi(const QString &providerName, const ResultMetadata &metadata, const QString &contentHtml);
    void populateMetadata(const ResultMetadata &metadata);

    SearchResult m_result;
    QString m_providerName;
    ResultMetadata m_metadata;
    QTextBrowser *m_detailsView = nullptr;
    QTreeWidget *m_metadataView = nullptr;
    SourceLink *m_sourceLink = nullptr;
};

#endif // RESULTDETAILWINDOW_H
