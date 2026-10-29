// SPDX-FileCopyrightText: 2026 KLookup contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SEARCHWINDOW_H
#define SEARCHWINDOW_H

#include "searchprovider.h"
#include <QMainWindow>
#include <QPointer>

class SearchTab;
class ProviderAuthManager;

class SearchWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SearchWindow(const QString &query,
                          const QList<SearchProvider *> &providers,
                          ProviderAuthManager *authManager = nullptr,
                          QWidget *parent = nullptr);
    ~SearchWindow() override = default;

    QString query() const;
    SearchTab *searchTab() const;

signals:
    void windowClosing(SearchWindow *window);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QString m_query;
    QPointer<SearchTab> m_tab;
};

#endif // SEARCHWINDOW_H
