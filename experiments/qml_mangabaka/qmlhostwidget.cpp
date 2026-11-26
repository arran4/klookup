// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qmlhostwidget.h"
#include "mangabakaviewmodel.h"

#include <QQmlContext>
#include <QQmlError>
#include <QQuickWidget>
#include <QTextBrowser>

QmlHostWidget::QmlHostWidget(MangaBakaViewModel *model, QWidget *parent)
    : QStackedWidget(parent)
    , m_model(model)
    , m_quickWidget(new QQuickWidget(this))
    , m_fallbackView(new QTextBrowser(this))
{
    m_quickWidget->setObjectName(QStringLiteral("qmlExperimentSurface"));
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->rootContext()->setContextProperty(QStringLiteral("mangaModel"), m_model);

    m_fallbackView->setObjectName(QStringLiteral("qmlFallbackHtmlView"));
    m_fallbackView->setOpenLinks(false);

    addWidget(m_quickWidget);
    addWidget(m_fallbackView);
    setCurrentWidget(m_fallbackView);
}

bool QmlHostWidget::isUsingFallback() const
{
    return currentWidget() == m_fallbackView;
}

bool QmlHostWidget::loadQml(const QUrl &sourceUrl, const QJsonObject &syntheticData)
{
    m_lastErrors.clear();
    if (!syntheticData.isEmpty()) {
        m_quickWidget->rootContext()->setContextProperty(QStringLiteral("rawFixture"), syntheticData.toVariantMap());
    }
    m_quickWidget->setSource(sourceUrl);
    if (m_quickWidget->status() == QQuickWidget::Error || !m_quickWidget->errors().isEmpty()) {
        for (const QQmlError &err : m_quickWidget->errors())
            m_lastErrors.append(err.toString());
        if (m_lastErrors.isEmpty())
            m_lastErrors.append(QStringLiteral("QQuickWidget failed to load %1").arg(sourceUrl.toString()));
        m_fallbackView->setHtml(m_model ? m_model->toFallbackHtml() : QStringLiteral("<p>Fallback unavailable</p>"));
        setCurrentWidget(m_fallbackView);
        return false;
    }
    setCurrentWidget(m_quickWidget);
    return true;
}
