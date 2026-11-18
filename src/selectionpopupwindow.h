// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SELECTIONPOPUPWINDOW_H
#define SELECTIONPOPUPWINDOW_H

#include "selectedtextpolicy.h"
#include "searchprovider.h"
#include <QFrame>
#include <QList>
#include <QPointer>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QVBoxLayout;

class SelectionPopupWindow : public QFrame
{
    Q_OBJECT
public:
    explicit SelectionPopupWindow(QWidget *parent = nullptr);
    ~SelectionPopupWindow() override;

    void lookupText(const QString &query,
                    const QList<SearchProvider *> &providers,
                    const SelectedTextPolicy &policy,
                    bool isAutomatic);

    void openFallbackPrompt(const QString &reason,
                            const QList<SearchProvider *> &providers,
                            const SelectedTextPolicy &policy);

    QString statusText() const;
    int resultCount() const;

signals:
    void lookupDispatched(const QString &providerId, const QString &query);

private:
    void clearActiveRunners();
    void dispatchToProviders(const QString &query,
                             const QList<SearchProvider *> &providers,
                             const SelectedTextPolicy &policy,
                             bool isAutomatic,
                             const QString &oneTimeApprovedRemoteId = QString());

    QLabel *m_headerLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QWidget *m_fallbackRow = nullptr;
    QLineEdit *m_fallbackInput = nullptr;
    QPushButton *m_fallbackSearchBtn = nullptr;
    QPushButton *m_oneTimeRemoteBtn = nullptr;
    QListWidget *m_resultsList = nullptr;
    QList<QPointer<SearchProvider>> m_activeRunners;
    QList<SearchProvider *> m_lastProviders;
    SelectedTextPolicy m_lastPolicy;
    QString m_pendingRemoteId;
    QString m_pendingQuery;
};

#endif // SELECTIONPOPUPWINDOW_H
