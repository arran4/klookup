// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cataloguebrowserwidget.h"

#include <KLocalizedString>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QVBoxLayout>

CatalogueBrowserWidget::CatalogueBrowserWidget(QWidget *parent)
    : QWidget(parent)
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_installRoot = appData + QStringLiteral("/providers.d");
    m_backupRoot = appData + QStringLiteral("/providers.d-backups");

    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(
        i18n("Opt-in third-party provider catalogue. Discovery reads metadata only and never executes provider code. "
             "Installed packages remain unapproved and disabled until you review and approve them in Script packages."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("catalogueEntryList"));
    layout->addWidget(m_list, 1);

    m_details = new QTextBrowser(this);
    m_details->setObjectName(QStringLiteral("catalogueEntryDetails"));
    m_details->setMaximumHeight(130);
    layout->addWidget(m_details);

    m_status = new QLabel(i18n("No catalogue index loaded."), this);
    m_status->setObjectName(QStringLiteral("catalogueStatusLabel"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout;
    m_loadBtn = new QPushButton(i18n("Load local catalogue…"), this);
    m_loadBtn->setObjectName(QStringLiteral("loadCatalogueBtn"));
    m_installBtn = new QPushButton(i18n("Install selected…"), this);
    m_installBtn->setObjectName(QStringLiteral("installCatalogueBtn"));
    m_rollbackBtn = new QPushButton(i18n("Rollback selected"), this);
    m_rollbackBtn->setObjectName(QStringLiteral("rollbackCatalogueBtn"));
    m_installBtn->setEnabled(false);
    m_rollbackBtn->setEnabled(false);
    buttons->addWidget(m_loadBtn);
    buttons->addWidget(m_installBtn);
    buttons->addWidget(m_rollbackBtn);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { updateDetails(); });
    connect(m_loadBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, i18n("Select Catalogue Index"), QString(), QStringLiteral("JSON (*.json)"));
        if (!path.isEmpty())
            loadCatalogueFile(path, true);
    });
    connect(m_installBtn, &QPushButton::clicked, this, [this]() {
        const int row = m_list->currentRow();
        if (row < 0 || row >= m_index.entries.size())
            return;
        const CatalogueEntry &e = m_index.entries.at(row);
        QString srcDir;
        if (e.packageUrl.isLocalFile())
            srcDir = e.packageUrl.toLocalFile();
        else
            srcDir = QFileDialog::getExistingDirectory(this, i18n("Select Downloaded Package Directory for %1", e.name));
        if (!srcDir.isEmpty())
            installSelectedFromDirectory(srcDir);
    });
    connect(m_rollbackBtn, &QPushButton::clicked, this, [this]() { rollbackSelected(); });
}

void CatalogueBrowserWidget::setRootsForTesting(const QString &installRoot, const QString &backupRoot)
{
    m_installRoot = installRoot;
    m_backupRoot = backupRoot;
    updateDetails();
}

bool CatalogueBrowserWidget::loadCatalogueFile(const QString &filePath, bool allowLocalFileUrls)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        m_status->setText(i18n("Failed to open catalogue file: %1", filePath));
        return false;
    }
    CatalogueIndex parsed;
    QString err;
    if (!ProviderCatalogue::parseIndex(f.readAll(), &parsed, &err, allowLocalFileUrls)) {
        m_status->setText(i18n("Invalid catalogue index: %1", err));
        return false;
    }
    m_index = parsed;
    m_list->clear();
    for (const CatalogueEntry &e : m_index.entries) {
        QString revokeReason;
        const bool revoked = ProviderCatalogue::isRevoked(m_index, e, &revokeReason);
        const QString label = revoked
            ? QStringLiteral("%1 (%2) [REVOKED]").arg(e.name, e.version)
            : QStringLiteral("%1 (%2) — %3").arg(e.name, e.version, e.id);
        m_list->addItem(label);
    }
    if (!m_index.entries.isEmpty())
        m_list->setCurrentRow(0);
    m_status->setText(i18n("Loaded %1 catalogue entry(ies).", m_index.entries.size()));
    updateDetails();
    return true;
}

bool CatalogueBrowserWidget::installSelectedFromDirectory(const QString &sourceDir)
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_index.entries.size())
        return false;
    const CatalogueEntry &e = m_index.entries.at(row);
    QString err;
    if (!ProviderCatalogue::installLocalPackage(m_index, e, sourceDir, m_installRoot, m_backupRoot, &err)) {
        m_status->setText(i18n("Installation blocked: %1", err));
        return false;
    }
    m_status->setText(i18n("Installed %1 %2 (unapproved and disabled by default; approve in Script packages).", e.name, e.version));
    updateDetails();
    emit packageInstalled(e.id);
    return true;
}

bool CatalogueBrowserWidget::rollbackSelected()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_index.entries.size())
        return false;
    const CatalogueEntry &e = m_index.entries.at(row);
    QString err;
    if (!ProviderCatalogue::rollbackPackage(e.id, m_installRoot, m_backupRoot, &err)) {
        m_status->setText(i18n("Rollback failed: %1", err));
        return false;
    }
    m_status->setText(i18n("Rolled back %1 to previous verified version.", e.id));
    updateDetails();
    emit packageRolledBack(e.id);
    return true;
}

int CatalogueBrowserWidget::entryCount() const
{
    return m_index.entries.size();
}

QString CatalogueBrowserWidget::statusText() const
{
    return m_status->text();
}

void CatalogueBrowserWidget::updateDetails()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_index.entries.size()) {
        m_details->clear();
        m_installBtn->setEnabled(false);
        m_rollbackBtn->setEnabled(false);
        return;
    }
    const CatalogueEntry &e = m_index.entries.at(row);
    QString revokeReason;
    const bool revoked = ProviderCatalogue::isRevoked(m_index, e, &revokeReason);
    const bool compatible = ProviderCatalogue::isCompatibleWithHost(e, 1);

    QString text;
    text += QStringLiteral("ID: %1 | Version: %2 | Author: %3 | License: %4\n")
                .arg(e.id, e.version, e.author, e.license);
    text += QStringLiteral("Repository: %1\n").arg(e.repositoryUrl.toString());
    text += QStringLiteral("Declared origins: %1\n")
                .arg(e.origins.isEmpty() ? QStringLiteral("(none)") : e.origins.join(QLatin1String(", ")));
    text += QStringLiteral("Declared secrets: %1\n")
                .arg(e.secrets.isEmpty() ? QStringLiteral("(none)") : e.secrets.join(QLatin1String(", ")));
    if (revoked)
        text += QStringLiteral("Status: REVOKED (%1)\n").arg(revokeReason);
    else if (!compatible)
        text += QStringLiteral("Status: Incompatible with host API v1\n");
    else
        text += QStringLiteral("Status: Ready to inspect & install (unapproved by default)\n");
    m_details->setPlainText(text);

    m_installBtn->setEnabled(!revoked && compatible);
    m_rollbackBtn->setEnabled(QFileInfo::exists(m_backupRoot + QLatin1Char('/') + e.id));
}
