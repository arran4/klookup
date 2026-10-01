#include "settingsdialog.h"
#include "searchprovider.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <KSharedConfig>
#include <KConfigGroup>
#include <QTabWidget>
#include <QWidget>
#include <QLabel>

#ifndef Q_OS_WASM
#include <KWallet>
#endif

SettingsDialog::SettingsDialog(const QList<SearchProvider*> &providers, QWidget *parent)
    : KPageDialog(parent), m_providers(providers)
{
    m_inputFunc = [](QWidget *parent, const QString &title, const QString &label, bool *ok) {
        return QInputDialog::getText(parent, title, label, QLineEdit::Password, QString(), ok);
    };

    m_persistFunc = [](const QString &providerId, const QString &token, WId winId) {
#ifndef Q_OS_WASM
        KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), winId, KWallet::Wallet::Synchronous);
        if (!wallet) {
            return PersistResult::OpenFailed;
        }
        if (!wallet->hasFolder(QStringLiteral("KLookup"))) {
            if (!wallet->createFolder(QStringLiteral("KLookup"))) {
                delete wallet;
                return PersistResult::FolderFailed;
            }
        }
        if (!wallet->setFolder(QStringLiteral("KLookup"))) {
            delete wallet;
            return PersistResult::FolderFailed;
        }
        if (wallet->writePassword(providerId, token) != 0) {
            delete wallet;
            return PersistResult::WriteFailed;
        }
        delete wallet;
        return PersistResult::Success;
#else
        Q_UNUSED(providerId);
        Q_UNUSED(token);
        Q_UNUSED(winId);
        return PersistResult::Unavailable;
#endif
    };

    m_feedbackFunc = [](QWidget *parent, PersistResult result, const QString &providerName) {
        switch (result) {
        case PersistResult::Success:
            QMessageBox::information(parent, QStringLiteral("Saved"), QStringLiteral("Credentials saved for %1.").arg(providerName));
            break;
        case PersistResult::Unavailable:
            QMessageBox::warning(parent, QStringLiteral("Storage Unavailable"), QStringLiteral("Credential storage is not available on this platform. Credentials will only be used for this session."));
            break;
        case PersistResult::OpenFailed:
            QMessageBox::warning(parent, QStringLiteral("Storage Error"), QStringLiteral("Failed to open the wallet. Credentials will only be used for this session."));
            break;
        case PersistResult::FolderFailed:
            QMessageBox::warning(parent, QStringLiteral("Storage Error"), QStringLiteral("Failed to create or access the required wallet folder. Credentials will only be used for this session."));
            break;
        case PersistResult::WriteFailed:
            QMessageBox::warning(parent, QStringLiteral("Storage Error"), QStringLiteral("Failed to save credentials to the wallet. Credentials will only be used for this session."));
            break;
        }
    };

    setWindowTitle(QStringLiteral("Configure KLookup"));
    setFaceType(KPageDialog::Tabbed);
    setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);

    // Providers Tab
    QWidget *providersWidget = new QWidget(this);
    QVBoxLayout *providersLayout = new QVBoxLayout(providersWidget);
    m_listWidget = new QListWidget(this);
    for (SearchProvider *provider : m_providers) {
        QListWidgetItem *item = new QListWidgetItem(provider->name(), m_listWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(provider->isEnabled() ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, QVariant::fromValue(provider));
    }
    providersLayout->addWidget(m_listWidget);

    QPushButton *loginButton = new QPushButton(QStringLiteral("Authenticate Selected Provider..."), this);
    providersLayout->addWidget(loginButton);
    addPage(providersWidget, QStringLiteral("Providers"));

    // General Settings Tab
    QWidget *generalWidget = new QWidget(this);
    QVBoxLayout *generalLayout = new QVBoxLayout(generalWidget);

    m_minimizeToTrayCheck = new QCheckBox(QStringLiteral("Minimize to System Tray on Close"), this);
    m_monitorClipboardCheck = new QCheckBox(QStringLiteral("Monitor Clipboard for Queries"), this);
    m_autoSearchClipboardCheck = new QCheckBox(QStringLiteral("Automatically Search Clipboard Text"), this);
    m_onlyFocusedClipboardCheck = new QCheckBox(QStringLiteral("Only Capture Clipboard When App is Focused"), this);

    generalLayout->addWidget(m_minimizeToTrayCheck);
    generalLayout->addWidget(new QLabel(QStringLiteral("<b>Clipboard Integration</b>"), this));
    generalLayout->addWidget(m_monitorClipboardCheck);
    generalLayout->addWidget(m_autoSearchClipboardCheck);
    generalLayout->addWidget(m_onlyFocusedClipboardCheck);
    generalLayout->addStretch();

    addPage(generalWidget, QStringLiteral("General"));

    // Load current settings
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup genGroup(config, QStringLiteral("General"));
    m_minimizeToTrayCheck->setChecked(genGroup.readEntry(QStringLiteral("MinimizeToTray"), true));
    m_monitorClipboardCheck->setChecked(genGroup.readEntry(QStringLiteral("MonitorClipboard"), false));
    m_autoSearchClipboardCheck->setChecked(genGroup.readEntry(QStringLiteral("AutoSearchClipboard"), false));
    m_onlyFocusedClipboardCheck->setChecked(genGroup.readEntry(QStringLiteral("ClipboardOnlyWhenFocused"), true));

    connect(m_monitorClipboardCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_autoSearchClipboardCheck->setEnabled(checked);
        m_onlyFocusedClipboardCheck->setEnabled(checked);
    });
    m_autoSearchClipboardCheck->setEnabled(m_monitorClipboardCheck->isChecked());
    m_onlyFocusedClipboardCheck->setEnabled(m_monitorClipboardCheck->isChecked());

    connect(loginButton, &QPushButton::clicked, this, &SettingsDialog::showLoginDialog);

    connect(button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::saveSettings);
    connect(button(QDialogButtonBox::Ok), &QPushButton::clicked, this, &SettingsDialog::saveSettings);

}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::showLoginDialog()
{
    QListWidgetItem *item = m_listWidget->currentItem();
    if (!item) {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Please select a provider first."));
        return;
    }

    SearchProvider *provider = item->data(Qt::UserRole).value<SearchProvider*>();
    if (!provider || !provider->supportsAuthentication()) {
        QMessageBox::information(this, QStringLiteral("Not Supported"), QStringLiteral("This provider does not support authentication."));
        return;
    }

    bool ok;
    QString token = m_inputFunc(this,
                                QStringLiteral("Login to %1").arg(provider->name()),
                                QStringLiteral("Token/Password/API Key:"),
                                &ok);
    if (ok) {
        provider->setCredentials(QString(), token);
        SettingsDialog::PersistResult result = m_persistFunc(provider->id(), token, winId());
        m_feedbackFunc(this, result, provider->name());
    }
}

void SettingsDialog::saveSettings()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup providersGroup(config, QStringLiteral("Providers"));

    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem *item = m_listWidget->item(i);
        SearchProvider *provider = item->data(Qt::UserRole).value<SearchProvider*>();
        if (provider) {
            bool isEnabled = (item->checkState() == Qt::Checked);
            provider->setEnabled(isEnabled);
            providersGroup.writeEntry(provider->id() + QStringLiteral("_Enabled"), isEnabled);
        }
    }

    KConfigGroup genGroup(config, QStringLiteral("General"));
    genGroup.writeEntry(QStringLiteral("MinimizeToTray"), m_minimizeToTrayCheck->isChecked());
    genGroup.writeEntry(QStringLiteral("MonitorClipboard"), m_monitorClipboardCheck->isChecked());
    genGroup.writeEntry(QStringLiteral("AutoSearchClipboard"), m_autoSearchClipboardCheck->isChecked());
    genGroup.writeEntry(QStringLiteral("ClipboardOnlyWhenFocused"), m_onlyFocusedClipboardCheck->isChecked());
    config->sync();
}
