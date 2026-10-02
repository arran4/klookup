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
    QString token = promptForToken(QStringLiteral("Login to %1").arg(provider->name()),
                                   QStringLiteral("Token/Password/API Key:"),
                                   &ok);
    if (ok) {
        provider->setCredentials(QString(), token);
        SettingsDialog::PersistResult result = performPersistence(provider->id(), token);
        showPersistenceFeedback(result, provider->name());
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

SettingsDialog::PersistResult SettingsDialog::performPersistence(const QString &providerId, const QString &token)
{
    if (!isWalletAvailable()) {
        return PersistResult::Unavailable;
    }
    if (!openWallet()) {
        return PersistResult::OpenFailed;
    }
    if (!walletHasFolder(QStringLiteral("KLookup"))) {
        if (!walletCreateFolder(QStringLiteral("KLookup"))) {
            closeWallet();
            return PersistResult::FolderFailed;
        }
    }
    if (!walletSetFolder(QStringLiteral("KLookup"))) {
        closeWallet();
        return PersistResult::FolderFailed;
    }
    if (walletWritePassword(providerId, token) != 0) {
        closeWallet();
        return PersistResult::WriteFailed;
    }
    closeWallet();
    return PersistResult::Success;
}

void SettingsDialog::showPersistenceFeedback(PersistResult result, const QString &providerName)
{
    switch (result) {
    case PersistResult::Success:
        displayInfo(QStringLiteral("Saved"), QStringLiteral("Credentials saved for %1.").arg(providerName));
        break;
    case PersistResult::Unavailable:
        displayWarning(QStringLiteral("Storage Unavailable"), QStringLiteral("Credential storage is not available on this platform. Credentials will only be used for this session."));
        break;
    case PersistResult::OpenFailed:
        displayWarning(QStringLiteral("Storage Error"), QStringLiteral("Failed to open the wallet. Credentials will only be used for this session."));
        break;
    case PersistResult::FolderFailed:
        displayWarning(QStringLiteral("Storage Error"), QStringLiteral("Failed to create or access the required wallet folder. Credentials will only be used for this session."));
        break;
    case PersistResult::WriteFailed:
        displayWarning(QStringLiteral("Storage Error"), QStringLiteral("Failed to save credentials to the wallet. Credentials will only be used for this session."));
        break;
    }
}

bool SettingsDialog::isWalletAvailable() const {
#ifndef Q_OS_WASM
    return true;
#else
    return false;
#endif
}

bool SettingsDialog::openWallet() {
#ifndef Q_OS_WASM
    m_wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), winId(), KWallet::Wallet::Synchronous);
    return m_wallet != nullptr;
#else
    return false;
#endif
}

bool SettingsDialog::walletHasFolder(const QString& folder) {
#ifndef Q_OS_WASM
    return m_wallet && m_wallet->hasFolder(folder);
#else
    Q_UNUSED(folder);
    return false;
#endif
}

bool SettingsDialog::walletCreateFolder(const QString& folder) {
#ifndef Q_OS_WASM
    return m_wallet && m_wallet->createFolder(folder);
#else
    Q_UNUSED(folder);
    return false;
#endif
}

bool SettingsDialog::walletSetFolder(const QString& folder) {
#ifndef Q_OS_WASM
    return m_wallet && m_wallet->setFolder(folder);
#else
    Q_UNUSED(folder);
    return false;
#endif
}

int SettingsDialog::walletWritePassword(const QString& key, const QString& password) {
#ifndef Q_OS_WASM
    if (!m_wallet) return -1;
    return m_wallet->writePassword(key, password);
#else
    Q_UNUSED(key);
    Q_UNUSED(password);
    return -1;
#endif
}

void SettingsDialog::closeWallet() {
#ifndef Q_OS_WASM
    delete m_wallet;
    m_wallet = nullptr;
#endif
}

void SettingsDialog::displayWarning(const QString& title, const QString& text) {
    QMessageBox::warning(this, title, text);
}

void SettingsDialog::displayInfo(const QString& title, const QString& text) {
    QMessageBox::information(this, title, text);
}

QString SettingsDialog::promptForToken(const QString& title, const QString& label, bool *ok) {
    return QInputDialog::getText(this, title, label, QLineEdit::Password, QString(), ok);
}
