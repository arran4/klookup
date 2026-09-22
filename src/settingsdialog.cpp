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
    connect(this, &QDialog::rejected, this, &SettingsDialog::reject);
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
    QString token = QInputDialog::getText(this,
                                          QStringLiteral("Login to %1").arg(provider->name()),
                                          QStringLiteral("Token/Password/API Key:"),
                                          QLineEdit::Password,
                                          QString(),
                                          &ok);
    if (ok) {
        provider->setCredentials(QString(), token);
#ifndef Q_OS_WASM
        KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), winId(), KWallet::Wallet::Synchronous);
        if (wallet) {
            if (!wallet->hasFolder(QStringLiteral("KLookup"))) {
                wallet->createFolder(QStringLiteral("KLookup"));
            }
            wallet->setFolder(QStringLiteral("KLookup"));
            wallet->writePassword(provider->id(), token);
            delete wallet;
        }
#endif
        QMessageBox::information(this, QStringLiteral("Saved"), QStringLiteral("Credentials saved for %1.").arg(provider->name()));
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
