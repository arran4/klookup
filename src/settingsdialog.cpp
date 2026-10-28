#include "settingsdialog.h"
#include "searchprovider.h"
#include "providerauthmanager.h"
#include <QLineEdit>
#include "clipboardpolicy.h"
#include <QComboBox>
#include <QFormLayout>
#include "provideroptions.h"
#include <QStackedWidget>
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

SettingsDialog::SettingsDialog(const QList<SearchProvider*> &providers, QWidget *parent, ProviderAuthManager *authentication)
    : KPageDialog(parent), m_providers(providers), m_authentication(authentication)
{


    if (!m_authentication) {
        for (auto *provider : providers) {
            if (authenticationDescriptor(provider).mode == AuthenticationMode::OAuth2Pkce) { m_authentication = new ProviderAuthManager(this, this); break; }
        }
    }
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
    auto *options = new QStackedWidget(providersWidget);
    options->addWidget(new QWidget(options)); // no options for unrelated providers
    for (auto *provider : m_providers) {
        if (auto *capability = qobject_cast<SearchProviderOptions *>(provider)) {
            auto *widget = capability->createOptionsWidget(options);
            if (widget) { m_providerOptions.insert(provider, widget); options->addWidget(widget); }
        }
    }
    providersLayout->addWidget(options);
    connect(m_listWidget, &QListWidget::currentRowChanged, options, [this, options](int row) {
        auto *item = m_listWidget->item(row);
        auto *provider = item ? item->data(Qt::UserRole).value<SearchProvider *>() : nullptr;
        options->setCurrentWidget(m_providerOptions.value(provider, options->widget(0)));
    });

    m_authMode = new QLabel(this);
    m_authMode->setObjectName(QStringLiteral("providerAuthMode"));
    m_authMode->setTextFormat(Qt::PlainText);
    m_authStatus = new QLabel(this);
    m_authStatus->setObjectName(QStringLiteral("providerAuthStatus"));
    m_authStatus->setTextFormat(Qt::PlainText);
    m_authStatus->setWordWrap(true);
    m_oauthDetails = new QLabel(this);
    m_oauthDetails->setTextFormat(Qt::PlainText);
    m_oauthDetails->setWordWrap(true);
    m_clientId = new QLineEdit(this);
    m_clientId->setObjectName(QStringLiteral("oauthClientId"));
    m_clientId->setPlaceholderText(QStringLiteral("Public developer application client ID"));
    m_rememberAuth = new QCheckBox(QStringLiteral("Save credentials securely in KWallet"), this);
    m_rememberAuth->setObjectName(QStringLiteral("rememberAuthentication"));
    m_rememberAuth->setChecked(true);
    auto *authForm = new QFormLayout;
    authForm->addRow(QStringLiteral("Authentication:"), m_authMode);
    authForm->addRow(QStringLiteral("OAuth client ID:"), m_clientId);
    providersLayout->addLayout(authForm);
    providersLayout->addWidget(m_oauthDetails);
    providersLayout->addWidget(m_authStatus);
    providersLayout->addWidget(m_rememberAuth);
    m_loginButton = new QPushButton(QStringLiteral("Authenticate Selected Provider…"), this);
    m_loginButton->setObjectName(QStringLiteral("connectProvider"));
    m_loginButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-password")));
    m_restoreAuth = new QPushButton(QStringLiteral("Restore saved credentials…"), this);
    m_restoreAuth->setObjectName(QStringLiteral("restoreProviderAuthentication"));
    m_disconnectAuth = new QPushButton(QStringLiteral("Disconnect / forget saved credentials…"), this);
    m_disconnectAuth->setObjectName(QStringLiteral("disconnectProvider"));
    m_cancelAuth = new QPushButton(QStringLiteral("Cancel authorization"), this);
    m_cancelAuth->setObjectName(QStringLiteral("cancelProviderAuthorization"));
    auto *authButtons = new QHBoxLayout;
    for (auto *button : {m_loginButton, m_restoreAuth, m_disconnectAuth, m_cancelAuth}) authButtons->addWidget(button);
    providersLayout->addLayout(authButtons);
    connect(m_listWidget, &QListWidget::currentRowChanged, this, &SettingsDialog::updateAuthenticationUi);
    connect(m_clientId, &QLineEdit::textEdited, this, [this](const QString &text) { if (!m_authProviderId.isEmpty()) m_clientDrafts[m_authProviderId] = text; });
    for (auto *provider : providers) {
        KConfigGroup auth(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Authentication-") + provider->id());
        m_clientDrafts.insert(provider->id(), auth.readEntry(QStringLiteral("ClientId"), QString()));
        if (m_authentication) {
            if (auto *session = m_authentication->sessionFor(provider)) connect(session, &ProviderAuthSession::changed, this, &SettingsDialog::updateAuthenticationUi);
        }
    }
    connect(m_restoreAuth, &QPushButton::clicked, this, [this] {
        auto *item = m_listWidget->currentItem();
        auto *provider = item ? item->data(Qt::UserRole).value<SearchProvider *>() : nullptr;
        if (m_authentication && provider) if (auto *session = m_authentication->sessionFor(provider)) session->restore(m_clientId->text());
    });
    connect(m_disconnectAuth, &QPushButton::clicked, this, [this] {
        auto *item = m_listWidget->currentItem();
        auto *provider = item ? item->data(Qt::UserRole).value<SearchProvider *>() : nullptr;
        if (m_authentication && provider) if (auto *session = m_authentication->sessionFor(provider)) session->disconnectProvider();
    });
    connect(m_cancelAuth, &QPushButton::clicked, this, [this] {
        auto *item = m_listWidget->currentItem();
        auto *provider = item ? item->data(Qt::UserRole).value<SearchProvider *>() : nullptr;
        if (m_authentication && provider) if (auto *session = m_authentication->sessionFor(provider)) session->cancel();
    });
    updateAuthenticationUi();
    addPage(providersWidget, QStringLiteral("Providers"));

    // General Settings Tab
    QWidget *generalWidget = new QWidget(this);
    QVBoxLayout *generalLayout = new QVBoxLayout(generalWidget);

    m_minimizeToTrayCheck = new QCheckBox(QStringLiteral("Minimize to System Tray on Close"), this);
    m_minimizeToTrayCheck->setObjectName(QStringLiteral("minimizeToTray"));
    m_captureMode = new QComboBox(this);
    m_captureMode->setObjectName(QStringLiteral("clipboardCaptureMode"));
    m_captureMode->addItems({QStringLiteral("Never"), QStringLiteral("KLookup window is visible"), QStringLiteral("KLookup window has focus"), QStringLiteral("While KLookup is running (including background)")});
    m_searchTrigger = new QComboBox(this);
    m_searchTrigger->setObjectName(QStringLiteral("clipboardSearchTrigger"));
    m_searchTrigger->addItems({QStringLiteral("Never"), QStringLiteral("When opened from tray"), QStringLiteral("When clipboard changes"), QStringLiteral("When window gains focus"), QStringLiteral("When global shortcut is used")});
    generalLayout->addWidget(m_minimizeToTrayCheck);
    auto *clipboardForm = new QFormLayout;
    clipboardForm->addRow(QStringLiteral("Capture clipboard while:"), m_captureMode);
    clipboardForm->addRow(QStringLiteral("Automatically search clipboard:"), m_searchTrigger);
    generalLayout->addLayout(clipboardForm);
    auto *hint = new QLabel(QStringLiteral("Capture permission is checked first. Capture alone quietly prepares the search field. Automatic searches never raise a window."), this);
    hint->setWordWrap(true);
    generalLayout->addWidget(hint);
    generalLayout->addStretch();

    addPage(generalWidget, QStringLiteral("General"));

    // Load current settings
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup genGroup(config, QStringLiteral("General"));
    m_minimizeToTrayCheck->setChecked(genGroup.readEntry(QStringLiteral("MinimizeToTray"), true));
    const auto policy = ClipboardPolicy::read(genGroup);
    m_captureMode->setCurrentIndex(int(policy.capture));
    m_searchTrigger->setCurrentIndex(int(policy.trigger));

    connect(m_loginButton, &QPushButton::clicked, this, &SettingsDialog::showLoginDialog);

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
    const auto descriptor = authenticationDescriptor(provider);
    if (!provider || descriptor.mode == AuthenticationMode::None) {
        QMessageBox::information(this, QStringLiteral("Not Supported"), QStringLiteral("This provider does not support authentication."));
        return;
    }

    if (descriptor.mode == AuthenticationMode::OAuth2Pkce && m_authentication) {
        const QString clientId = m_clientId->text().trimmed();
        KConfigGroup auth(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Authentication-") + provider->id());
        auth.writeEntry(QStringLiteral("ClientId"), clientId);
        auth.sync();
        m_clientDrafts[provider->id()] = clientId;
        m_authentication->sessionFor(provider)->connectOAuth(clientId, m_rememberAuth->isChecked());
        return;
    }
    bool ok;
    QString token = promptForToken(QStringLiteral("Login to %1").arg(provider->name()),
                                   QStringLiteral("Token/Password/API Key:"),
                                   &ok);
    if (ok) {
        if (m_authentication) {
            m_authentication->sessionFor(provider)->setManualToken(token, m_rememberAuth->isChecked());
            return;
        }
        provider->setCredentials(QString(), token);
        if (!m_rememberAuth->isChecked()) { displayInfo(QStringLiteral("Session only"), QStringLiteral("Credentials will only be used for this session.")); return; }
        SettingsDialog::PersistResult result = performPersistence(provider->id(), token);
        showPersistenceFeedback(result, provider->name());
    }
}

void SettingsDialog::updateAuthenticationUi()
{
    auto *item = m_listWidget->currentItem();
    auto *provider = item ? item->data(Qt::UserRole).value<SearchProvider *>() : nullptr;
    const auto descriptor = authenticationDescriptor(provider);
    const bool oauth = descriptor.mode == AuthenticationMode::OAuth2Pkce;
    const bool supported = descriptor.mode != AuthenticationMode::None;
    const QString id = provider ? provider->id() : QString();
    if (m_authProviderId != id) {
        m_authProviderId = id;
        m_clientId->setText(m_clientDrafts.value(id));
    }
    m_clientId->setEnabled(oauth);
    m_authMode->setText(oauth ? QStringLiteral("OAuth 2.0 Authorization Code + PKCE (system browser)") :
        (supported ? QStringLiteral("Manually supplied API key / token") : QStringLiteral("No authentication required")));
    auto *session = m_authentication && provider ? m_authentication->sessionFor(provider) : nullptr;
    const bool busy = session && (session->state() == ProviderAuthSession::State::Authorizing || session->state() == ProviderAuthSession::State::Refreshing);
    m_loginButton->setText(oauth ? QStringLiteral("Connect / reauthorize in browser…") : QStringLiteral("Authenticate Selected Provider…"));
    m_loginButton->setEnabled(supported && !busy);
    m_restoreAuth->setEnabled(session && !busy);
    m_disconnectAuth->setEnabled(session != nullptr);
    m_cancelAuth->setEnabled(busy);
    m_rememberAuth->setEnabled(supported);
    m_authStatus->setText(session ? session->statusText() : (supported ? QStringLiteral("Manual credentials can be saved to KWallet or used for this session only") : QString()));
    QString details;
    if (oauth) {
        const QString redirect = descriptor.oauth.callbackPort ? QStringLiteral("http://127.0.0.1:%1%2").arg(descriptor.oauth.callbackPort).arg(descriptor.oauth.callbackPath) :
            QStringLiteral("Provider-supported dynamic loopback port on 127.0.0.1") + descriptor.oauth.callbackPath;
        details = QStringLiteral("Register the callback in your developer application: %1\nRequested scopes: %2").arg(redirect, descriptor.oauth.scopes.join(QStringLiteral(", ")));
        if (session && !session->callbackUrl().isEmpty()) details += QStringLiteral("\nCurrent callback: ") + session->callbackUrl().toString();
    }
    m_oauthDetails->setText(details);
    m_oauthDetails->setVisible(oauth);
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
            if (auto *capability = qobject_cast<SearchProviderOptions *>(provider); capability && m_providerOptions.contains(provider))
                capability->saveOptions(m_providerOptions.value(provider));
            providersGroup.writeEntry(provider->id() + QStringLiteral("_Enabled"), isEnabled);
        }
    }

    if (!m_authProviderId.isEmpty()) m_clientDrafts[m_authProviderId] = m_clientId->text().trimmed();
    for (auto it = m_clientDrafts.cbegin(); it != m_clientDrafts.cend(); ++it) {
        KConfigGroup auth(config, QStringLiteral("Authentication-") + it.key());
        auth.writeEntry(QStringLiteral("ClientId"), it.value());
    }
    KConfigGroup genGroup(config, QStringLiteral("General"));
    genGroup.writeEntry(QStringLiteral("MinimizeToTray"), m_minimizeToTrayCheck->isChecked());
    ClipboardPolicy::write(genGroup, {ClipboardPolicy::Capture(m_captureMode->currentIndex()), ClipboardPolicy::Trigger(m_searchTrigger->currentIndex())});
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
