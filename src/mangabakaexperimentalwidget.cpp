// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mangabakaexperimentalwidget.h"
#include <KLocalizedString>
#include <QEvent>
#include <QTextBrowser>
#ifdef KLOOKUP_HAS_QML_MANGABAKA
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWidget>
#endif

MangaBakaExperimentalWidget::MangaBakaExperimentalWidget(QWidget *parent)
    : QStackedWidget(parent)
    , m_model(new MangaBakaExperimentalModel(this))
    , m_fallbackBrowser(new QTextBrowser(this))
{
    setObjectName(QStringLiteral("mangaBakaExperimentalSurface"));
    m_fallbackBrowser->setObjectName(QStringLiteral("mangaBakaExperimentalFallbackView"));
    m_fallbackBrowser->setOpenExternalLinks(false);
    m_fallbackBrowser->setOpenLinks(false);
    addWidget(m_fallbackBrowser);

#ifdef KLOOKUP_HAS_QML_MANGABAKA
    m_quickWidget = new QQuickWidget(this);
    m_quickWidget->setObjectName(QStringLiteral("mangaBakaExperimentalQuickView"));
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->rootContext()->setContextProperty(QStringLiteral("mangaModel"), m_model);
    addWidget(m_quickWidget);
#endif

    setCurrentWidget(m_fallbackBrowser);
}

bool MangaBakaExperimentalWidget::isUsingQml() const
{
#ifdef KLOOKUP_HAS_QML_MANGABAKA
    return m_qmlActive && m_quickWidget && currentWidget() == m_quickWidget;
#else
    return false;
#endif
}

void MangaBakaExperimentalWidget::changeEvent(QEvent *event)
{
    QStackedWidget::changeEvent(event);
    if (event && (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange ||
                  event->type() == QEvent::StyleChange)) {
        if (m_model)
            m_model->setPalette(palette());
    }
}

void MangaBakaExperimentalWidget::ensureSurfaceLoaded()
{
#ifdef KLOOKUP_HAS_QML_MANGABAKA
    if (m_qmlAttempted)
        return;
    m_qmlAttempted = true;
    m_qmlErrors.clear();
    m_model->setPalette(palette());
    m_quickWidget->setSource(QUrl(QStringLiteral("qrc:/klookup/qml/MangaBakaExperimentalView.qml")));
    if (m_quickWidget->status() == QQuickWidget::Error || !m_quickWidget->errors().isEmpty()) {
        for (const QQmlError &err : m_quickWidget->errors())
            m_qmlErrors.append(err.toString());
        if (m_qmlErrors.isEmpty())
            m_qmlErrors.append(i18n("Failed to initialize host-controlled QML presentation."));
        m_qmlActive = false;
        updateFallbackView();
        setCurrentWidget(m_fallbackBrowser);
        return;
    }
    m_qmlActive = true;
    setCurrentWidget(m_quickWidget);
#else
    m_qmlAttempted = true;
    m_qmlActive = false;
    updateFallbackView();
    setCurrentWidget(m_fallbackBrowser);
#endif
}

void MangaBakaExperimentalWidget::updateFallbackView()
{
    QString html = m_model->renderFallbackHtml();
#ifndef KLOOKUP_HAS_QML_MANGABAKA
    html += QStringLiteral("<p><small><i>%1</i></small></p>")
                .arg(i18n("Qt Quick QML support was disabled at build time; showing fallback HTML view."));
#endif
    m_fallbackBrowser->setHtml(html);
}

void MangaBakaExperimentalWidget::setResult(const SearchResult &result,
                                            const ResultMetadata &metadata,
                                            const ResultContent &content,
                                            const QString &providerName)
{
    m_model->setPalette(palette());
    m_model->setResult(result, metadata, content, providerName);
    ensureSurfaceLoaded();
    if (!isUsingQml())
        updateFallbackView();
}

void MangaBakaExperimentalWidget::clear()
{
    m_model->clear();
    m_fallbackBrowser->clear();
}
