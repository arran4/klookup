// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CATALOGUEBROWSERWIDGET_H
#define CATALOGUEBROWSERWIDGET_H

#include "providercatalogue.h"
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QTextBrowser;

class CatalogueBrowserWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CatalogueBrowserWidget(QWidget *parent = nullptr);

    void setRootsForTesting(const QString &installRoot, const QString &backupRoot);
    bool loadCatalogueFile(const QString &filePath, bool allowLocalFileUrls = false);
    bool installSelectedFromDirectory(const QString &sourceDir);
    bool rollbackSelected();
    int entryCount() const;
    QString statusText() const;

signals:
    void packageInstalled(const QString &id);
    void packageRolledBack(const QString &id);

private:
    void updateDetails();

    CatalogueIndex m_index;
    QString m_installRoot;
    QString m_backupRoot;
    QListWidget *m_list = nullptr;
    QTextBrowser *m_details = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_loadBtn = nullptr;
    QPushButton *m_installBtn = nullptr;
    QPushButton *m_rollbackBtn = nullptr;
};

#endif // CATALOGUEBROWSERWIDGET_H
