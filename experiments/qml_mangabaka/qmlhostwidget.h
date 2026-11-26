// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef QML_HOST_WIDGET_H
#define QML_HOST_WIDGET_H

#include <QJsonObject>
#include <QStackedWidget>
#include <QStringList>
#include <QUrl>

class QQuickWidget;
class QTextBrowser;
class MangaBakaViewModel;

class QmlHostWidget : public QStackedWidget
{
    Q_OBJECT
public:
    explicit QmlHostWidget(MangaBakaViewModel *model, QWidget *parent = nullptr);

    bool loadQml(const QUrl &sourceUrl, const QJsonObject &syntheticData = {});
    bool isUsingFallback() const;
    QQuickWidget *quickWidget() const { return m_quickWidget; }
    QTextBrowser *fallbackView() const { return m_fallbackView; }
    QStringList qmlErrorStrings() const { return m_lastErrors; }

private:
    MangaBakaViewModel *m_model = nullptr;
    QQuickWidget *m_quickWidget = nullptr;
    QTextBrowser *m_fallbackView = nullptr;
    QStringList m_lastErrors;
};

#endif // QML_HOST_WIDGET_H
