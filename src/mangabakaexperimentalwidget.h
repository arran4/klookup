// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MANGABAKAEXPERIMENTALWIDGET_H
#define MANGABAKAEXPERIMENTALWIDGET_H

#include "mangabakaexperimentalmodel.h"
#include <QStackedWidget>

class QQuickWidget;
class QTextBrowser;

class MangaBakaExperimentalWidget : public QStackedWidget
{
    Q_OBJECT
public:
    explicit MangaBakaExperimentalWidget(QWidget *parent = nullptr);

    void setResult(const SearchResult &result,
                   const ResultMetadata &metadata = {},
                   const ResultContent &content = {},
                   const QString &providerName = {});
    void clear();

    MangaBakaExperimentalModel *model() const { return m_model; }
    bool isUsingQml() const;
    QStringList qmlErrors() const { return m_qmlErrors; }

protected:
    void changeEvent(QEvent *event) override;

private:
    void ensureSurfaceLoaded();
    void updateFallbackView();

    MangaBakaExperimentalModel *m_model = nullptr;
    QQuickWidget *m_quickWidget = nullptr;
    QTextBrowser *m_fallbackBrowser = nullptr;
    bool m_qmlAttempted = false;
    bool m_qmlActive = false;
    QStringList m_qmlErrors;
};

#endif // MANGABAKAEXPERIMENTALWIDGET_H
