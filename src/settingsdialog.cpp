#include "providerdefaults.h"
#include "settingssplitter.h"
#include <QScrollArea>
#include <QGridLayout>
#include "settingsdialog.h"
#include "languagepreferences.h"
#include "providerdiagnostics.h"
#include <QPlainTextEdit>
#include <QJsonDocument>
#include "autostartmanager.h"
#include "searchprovider.h"
#include "providerauthmanager.h"
#include <QLineEdit>
#include "clipboardpolicy.h"
#include <QComboBox>
#include <QFormLayout>
#include "provideroptions.h"
#include "providerapiv1.h"
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

SettingsDialog::SettingsDialog(const QList<SearchProvider*> &providers, QWidget *parent, ProviderAuthManager *authentication, const QList<ProviderPackageStatus> &packages)
    : KPageDialog(parent), m_providers(providers), m_packages(packages), m_authentication(authentication)
    , m_autostartManager(std::make_unique<AutostartManager>())
{


    if (!m_authentication) {
        for (auto *provider : providers) {
            if (authenticationDescriptorV2(provider).mode == AuthenticationMode::OAuth2Pkce) { m_authentication = new ProviderAuthManager(this, this); break; }
        }
    }
    setWindowTitle(QStringLiteral("Configure KLookup"));
    setFaceType(KPageDialog::Tabbed);
    setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);

    // Providers Tab
    QWidget *providersWidget = new QWidget(this);
    QVBoxLayout *providersLayout = new QVBoxLayout(providersWidget);
    m_listWidget = new QListWidget(this);
    m_listWidget->setObjectName(QStringLiteral("nativeProviders"));
    for (SearchProvider *provider : m_providers) {
        if (qobject_cast<ProviderApiV1::ProviderV1ToLegacyAdapter *>(provider)) continue;
        QListWidgetItem *item = new QListWidgetItem(provider->name(), m_listWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(provider->isEnabled() ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole + 1, provider->isEnabled());
        item->setToolTip(ProviderDefaults::reason(provider->id()));
        item->setData(Qt::UserRole, QVariant::fromValue(provider));
        item->setToolTip(provider->classification().description().toHtmlEscaped());
    }
    auto *providerDetails = new QWidget(providersWidget);
    auto *providerDetailsLayout = new QVBoxLayout(providerDetails);
    auto *providerScroll = new QScrollArea(providersWidget);
    providerScroll->setObjectName(QStringLiteral("providerDetailsScroll"));
    providerScroll->setWidgetResizable(true); providerScroll->setWidget(providerDetails);
    providersLayout->addWidget(new SettingsSplitter(QStringLiteral("SettingsProvidersSplitter"),
        i18n("Provider list and details separator"), m_listWidget, providerScroll, providersWidget));
    auto *options = new QStackedWidget(providersWidget);
    options->addWidget(new QWidget(options)); // no options for unrelated providers
    for (auto *provider : m_providers) {
        if (auto *capability = qobject_cast<SearchProviderOptions *>(provider)) {
            auto *widget = capability->createOptionsWidget(options);
            if (widget) { m_providerOptions.insert(provider, widget); options->addWidget(widget); }
        }
    }
    providerDetailsLayout->addWidget(options);
    auto *classification = new QLabel(providerDetails);
    classification->setObjectName(QStringLiteral("providerClassification"));
    classification->setTextFormat(Qt::PlainText);
    classification->setWordWrap(true);
    providerDetailsLayout->addWidget(classification);
    connect(m_listWidget, &QListWidget::currentRowChanged, this, [this, classification](int row) {
        const auto *item = m_listWidget->item(row);
        auto *provider = item ? item->data(Qt::UserRole).value<SearchProvider *>() : nullptr;
        classification->setText(provider ? provider->classification().description() : QString());
    });
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
    m_clientIdLabel = new QLabel(QStringLiteral("OAuth client ID:"), this);
    m_clientIdLabel->setBuddy(m_clientId);
    authForm->addRow(m_clientIdLabel, m_clientId);
    providerDetailsLayout->addLayout(authForm);
    providerDetailsLayout->addWidget(m_oauthDetails);
    providerDetailsLayout->addWidget(m_authStatus);
    providerDetailsLayout->addWidget(m_rememberAuth);
    m_loginButton = new QPushButton(QStringLiteral("Authenticate Selected Provider…"), this);
    m_loginButton->setObjectName(QStringLiteral("connectProvider"));
    m_loginButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-password")));
    m_restoreAuth = new QPushButton(QStringLiteral("Restore saved credentials…"), this);
    m_restoreAuth->setObjectName(QStringLiteral("restoreProviderAuthentication"));
    m_disconnectAuth = new QPushButton(QStringLiteral("Disconnect / forget saved credentials…"), this);
    m_disconnectAuth->setObjectName(QStringLiteral("disconnectProvider"));
    m_cancelAuth = new QPushButton(QStringLiteral("Cancel authorization"), this);
    m_cancelAuth->setObjectName(QStringLiteral("cancelProviderAuthorization"));
    auto *authButtons = new QGridLayout;
    int authButtonIndex = 0;
    for (auto *button : {m_loginButton, m_restoreAuth, m_disconnectAuth, m_cancelAuth}) authButtons->addWidget(button, authButtonIndex / 2, authButtonIndex % 2), ++authButtonIndex;
    providerDetailsLayout->addLayout(authButtons);
    connect(m_listWidget, &QListWidget::currentRowChanged, this, &SettingsDialog::updateAuthenticationUi);
    connect(m_clientId, &QLineEdit::textEdited, this, [this](const QString &text) { if (!m_authProviderId.isEmpty()) m_clientDrafts[m_authProviderId] = text; });
    for (auto *provider : providers) {
        KConfigGroup auth(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Authentication-") + provider->id());
        const auto descriptor = authenticationDescriptorV2(provider);
        if (descriptor.usable() && descriptor.mode == AuthenticationMode::OAuth2Pkce && descriptor.publicClientId.isEmpty())
            m_clientDrafts.insert(provider->id(), auth.readEntry(QStringLiteral("ClientId"), QString()));
        if (m_authentication) {
            if (auto *session = m_authentication->sessionFor(provider)) connect(session, &ProviderAuthSession::changed, this, &SettingsDialog::updateAuthenticationUi);
        }
    }
    connect(m_restoreAuth, &QPushButton::clicked, this, [this] {
        auto *item = m_listWidget->currentItem();
        auto *provider = item ? item->data(Qt::UserRole).value<SearchProvider *>() : nullptr;
        const auto descriptor = authenticationDescriptorV2(provider);
        const QString clientId = descriptor.publicClientId.isEmpty() ? m_clientId->text() : descriptor.publicClientId;
        if (m_authentication && provider) if (auto *session = m_authentication->sessionFor(provider)) session->restore(clientId);
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

    // Package metadata remains available even when no provider can be created.
    auto *packagePage = new QWidget(this);
    auto *packageLayout = new QVBoxLayout(packagePage);
    m_packageList = new QListWidget(packagePage);
    m_packageList->setObjectName(QStringLiteral("scriptedPackages"));
    for (const auto &record : m_packages) {
        auto *item = new QListWidgetItem(record.package.name() + QStringLiteral(" — ") + record.status, m_packageList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(record.package.isEnabled() ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(ProviderDefaults::reason(record.package.id()));
    }
    auto *packageDetails = new QWidget(packagePage);
    auto *packageDetailsLayout = new QVBoxLayout(packageDetails);
    auto *packageScroll = new QScrollArea(packagePage);
    packageScroll->setObjectName(QStringLiteral("packageDetailsScroll"));
    packageScroll->setWidgetResizable(true); packageScroll->setWidget(packageDetails);
    packageLayout->addWidget(new SettingsSplitter(QStringLiteral("SettingsPackagesSplitter"),
        i18n("Script package list and details separator"), m_packageList, packageScroll, packagePage));
    auto *identity = new QLabel(packagePage);
    identity->setObjectName(QStringLiteral("packageIdentity"));
    identity->setTextFormat(Qt::PlainText);
    identity->setWordWrap(true);
    identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    packageDetailsLayout->addWidget(identity);
    auto *approve = new QPushButton(QStringLiteral("Review and approve…"), packagePage);
    approve->setObjectName(QStringLiteral("approvePackage"));
    auto *revoke = new QPushButton(QStringLiteral("Revoke approval"), packagePage);
    revoke->setObjectName(QStringLiteral("revokePackage"));
    auto *retry = new QPushButton(QStringLiteral("Apply and retry discovery"), packagePage);
    retry->setObjectName(QStringLiteral("retryPackages"));
    auto *packageButtons = new QGridLayout;
    int packageButtonIndex = 0;
    for (auto *b : {approve, revoke, retry}) packageButtons->addWidget(b, packageButtonIndex / 2, packageButtonIndex % 2), ++packageButtonIndex;
    packageDetailsLayout->addLayout(packageButtons);
    auto updatePackage = [this, identity, approve, revoke](int row) {
        const bool selected = row >= 0 && row < m_packages.size();
        approve->setEnabled(selected && m_packages[row].package.isValid());
        revoke->setEnabled(selected);
        if (!selected) { identity->clear(); return; }
        const auto &record = m_packages[row];
        const auto &pkg = record.package;
        identity->setText(pkg.manifest().classification.description() + QLatin1Char('\n') + ProviderDefaults::reason(pkg.id()) + QLatin1Char('\n') + (pkg.manifest().authentication.mode == AuthenticationMode::None ? QStringLiteral("No authentication required") : QStringLiteral("Declared authentication: %1 — script credential execution is unavailable (issue #85)").arg(pkg.manifest().authentication.methods().join(QStringLiteral(", ")))) + QLatin1Char('\n') + QStringLiteral("%1 (%2)\nVersion: %3\nSource: %4 (%5)\nOrigins: %6\nSecret capabilities: %7\nStatus: %8\nApproval fingerprint: %9")
            .arg(pkg.name(), pkg.id(), pkg.manifest().version, pkg.packageDir(), pkg.provenance(),
                 pkg.manifest().permissions.origins.join(QStringLiteral(", ")),
                 pkg.manifest().permissions.secrets.join(QStringLiteral(", ")), record.status,
                 QString::fromLatin1(pkg.approvalFingerprint())));
    };
    connect(m_packageList, &QListWidget::currentRowChanged, this, updatePackage);
    connect(m_packageList, &QListWidget::itemChanged, this, [this] { m_packagesChanged = true; });
    connect(approve, &QPushButton::clicked, this, [this, identity] {
        const int row = m_packageList->currentRow();
        if (row < 0) return;
        if (QMessageBox::question(this, QStringLiteral("Approve trusted in-process code"),
            identity->text() + QStringLiteral("\n\nThis code runs inside KLookup. Approve these exact contents and permissions?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
        m_approvalDrafts[m_packages[row].package.id()] = true;
        m_packagesChanged = true;
        m_packageList->item(row)->setText(m_packages[row].package.name() + QStringLiteral(" — Approval pending Apply"));
    });
    connect(revoke, &QPushButton::clicked, this, [this] {
        const int row = m_packageList->currentRow();
        if (row < 0) return;
        m_approvalDrafts[m_packages[row].package.id()] = false;
        m_packagesChanged = true;
        m_packageList->item(row)->setText(m_packages[row].package.name() + QStringLiteral(" — Revocation pending Apply"));
    });
    connect(retry, &QPushButton::clicked, this, [this] { m_packagesChanged = true; m_retryPackages = true; saveSettings(); });
    updatePackage(-1);
    addPage(packagePage, QStringLiteral("Script packages"));

    auto *health = new QWidget(this);
    auto *healthLayout = new QVBoxLayout(health);
    auto *healthList = new QListWidget(health);
    healthList->setObjectName(QStringLiteral("diagnosticProviders"));
    const auto diagnosticSources = ProviderDiagnostics::forSettings(m_providers, m_packages);
    for (const auto &source : diagnosticSources) healthList->addItem(source.id + QStringLiteral(" (") + source.type + QStringLiteral(")"));
    auto *healthDetails = new QWidget(health);
    auto *healthDetailsLayout = new QVBoxLayout(healthDetails);
    auto *healthScroll = new QScrollArea(health);
    healthScroll->setObjectName(QStringLiteral("healthDetailsScroll"));
    healthScroll->setWidgetResizable(true); healthScroll->setWidget(healthDetails);
    healthLayout->addWidget(new SettingsSplitter(QStringLiteral("SettingsHealthSplitter"),
        i18n("Provider health list and details separator"), healthList, healthScroll, health));
    auto *mode = new QComboBox(health);
    mode->addItems({QStringLiteral("Passive preflight"), QStringLiteral("Offline fixtures (execute approved code)"), QStringLiteral("Live probes (allow network, no credentials)")});
    mode->setObjectName(QStringLiteral("diagnosticMode"));
    healthDetailsLayout->addWidget(mode);
    auto *output = new QPlainTextEdit(health); output->setReadOnly(true);
    output->setObjectName(QStringLiteral("diagnosticOutput")); healthDetailsLayout->addWidget(output);
    auto *buttons = new QGridLayout;
    auto *one = new QPushButton(QStringLiteral("Check selected / Retry"), health);
    auto *all = new QPushButton(QStringLiteral("Check all providers"), health);
    auto *stop = new QPushButton(QStringLiteral("Cancel checks"), health);
    one->setObjectName(QStringLiteral("checkProvider")); all->setObjectName(QStringLiteral("checkAllProviders"));
    buttons->addWidget(one, 0, 0); buttons->addWidget(all, 0, 1); buttons->addWidget(stop, 1, 0); healthDetailsLayout->addLayout(buttons);
    auto *diagnostics = new ProviderDiagnostics(health);
    connect(one, &QPushButton::clicked, this, [=] {
        const int row = healthList->currentRow();
        if (row >= 0 && row < diagnosticSources.size()) { output->clear(); diagnostics->run({diagnosticSources[row]}, static_cast<ProviderDiagnostics::Mode>(mode->currentIndex())); }
    });
    connect(all, &QPushButton::clicked, this, [=] { output->clear(); diagnostics->run(diagnosticSources, static_cast<ProviderDiagnostics::Mode>(mode->currentIndex())); });
    connect(stop, &QPushButton::clicked, diagnostics, &ProviderDiagnostics::cancel);
    connect(diagnostics, &ProviderDiagnostics::resultReady, health, [output](quint64, const QJsonObject &row) { output->appendPlainText(QString::fromUtf8(QJsonDocument(row).toJson(QJsonDocument::Compact))); });
    addPage(health, QStringLiteral("Provider health"));


    // General Settings Tab
    QWidget *generalWidget = new QWidget(this);
    QVBoxLayout *generalLayout = new QVBoxLayout(generalWidget);

    m_minimizeToTrayCheck = new QCheckBox(QStringLiteral("Minimize to System Tray on Close"), this);
    m_minimizeToTrayCheck->setObjectName(QStringLiteral("minimizeToTray"));
    m_clearTabsOnCloseCheck = new QCheckBox(QStringLiteral("Clear search tabs when closing the window"), this);
    m_clearTabsOnCloseCheck->setObjectName(QStringLiteral("clearTabsOnClose"));
    m_autostartCheck = new QCheckBox(QStringLiteral("Start KLookup when logging in"), this);
    m_autostartCheck->setObjectName(QStringLiteral("autostartOnLogin"));
    m_captureMode = new QComboBox(this);
    m_captureMode->setObjectName(QStringLiteral("clipboardCaptureMode"));
    m_captureMode->addItems({QStringLiteral("Never"), QStringLiteral("KLookup window is visible"), QStringLiteral("KLookup window has focus"), QStringLiteral("While KLookup is running (including background)")});
    m_searchTrigger = new QComboBox(this);
    m_searchTrigger->setObjectName(QStringLiteral("clipboardSearchTrigger"));
    m_searchTrigger->addItems({QStringLiteral("Never"), QStringLiteral("When opened from tray"), QStringLiteral("When clipboard changes"), QStringLiteral("When window gains focus"), QStringLiteral("When global shortcut is used")});
    generalLayout->addWidget(m_minimizeToTrayCheck);
    generalLayout->addWidget(m_clearTabsOnCloseCheck);
    generalLayout->addWidget(m_autostartCheck);
    auto *clipboardForm = new QFormLayout;
    clipboardForm->addRow(QStringLiteral("Capture clipboard while:"), m_captureMode);
    clipboardForm->addRow(QStringLiteral("Automatically search clipboard:"), m_searchTrigger);
    generalLayout->addLayout(clipboardForm);
    auto *hint = new QLabel(QStringLiteral("Capture permission is checked first. Capture alone quietly prepares the search field. Automatic searches never raise a window."), this);
    hint->setWordWrap(true);
    generalLayout->addWidget(hint);
    m_showRecent = new QCheckBox(QStringLiteral("Show recent activity"), this);
    m_showRecent->setObjectName(QStringLiteral("showRecentActivity"));
    m_recordClipboardRecent = new QCheckBox(QStringLiteral("Record clipboard activity (turn off to remove clipboard entries)"), this);
    m_recordClipboardRecent->setObjectName(QStringLiteral("recordClipboardActivity"));
    m_persistClipboardRecent = new QCheckBox(QStringLiteral("Save recorded clipboard activity between sessions (24 hours)"), this);
    m_persistClipboardRecent->setObjectName(QStringLiteral("persistClipboardActivity"));
    generalLayout->addWidget(m_showRecent);
    generalLayout->addWidget(m_recordClipboardRecent);
    generalLayout->addWidget(m_persistClipboardRecent);
    auto *recentHint = new QLabel(QStringLiteral("Hiding recent activity keeps saved history. Clipboard recording defaults off; enabling "
                                                 "it alone keeps items in memory. Automatic clipboard search tabs are session-only."),
                                  this);
    recentHint->setWordWrap(true);
    generalLayout->addWidget(recentHint);
    generalLayout->addStretch();

    addPage(generalWidget, QStringLiteral("General"));

    m_languages = new LanguagePreferencesWidget(this);
    addPage(m_languages, QStringLiteral("Languages"));

    // Load current settings
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup genGroup(config, QStringLiteral("General"));
    m_minimizeToTrayCheck->setChecked(genGroup.readEntry(QStringLiteral("MinimizeToTray"), true));
    m_clearTabsOnCloseCheck->setChecked(genGroup.readEntry(QStringLiteral("ClearTabsOnClose"), false));
    m_autostartCheck->setChecked(m_autostartManager ? m_autostartManager->isAutostartEnabled() : false);
    const auto policy = ClipboardPolicy::read(genGroup);
    m_captureMode->setCurrentIndex(int(policy.capture));
    m_searchTrigger->setCurrentIndex(int(policy.trigger));
    m_showRecent->setChecked(genGroup.readEntry(QStringLiteral("ShowRecentActivity"), true));
    m_recordClipboardRecent->setChecked(genGroup.readEntry(QStringLiteral("RecordClipboardActivity"), false));
    m_persistClipboardRecent->setChecked(genGroup.readEntry(QStringLiteral("PersistClipboardActivity"), false));
    m_persistClipboardRecent->setEnabled(m_recordClipboardRecent->isChecked());
    connect(m_recordClipboardRecent, &QCheckBox::toggled, m_persistClipboardRecent, &QWidget::setEnabled);

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
    const auto descriptor = authenticationDescriptorV2(provider);
    if (!provider || !descriptor.usable()) {
        QMessageBox::information(this, QStringLiteral("Not Supported"), QStringLiteral("This provider does not support authentication."));
        return;
    }

    if (descriptor.mode == AuthenticationMode::OAuth2Pkce && m_authentication) {
        const QString clientId = descriptor.publicClientId.isEmpty() ? m_clientId->text().trimmed() : descriptor.publicClientId;
        KConfigGroup auth(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Authentication-") + provider->id());
        if (descriptor.publicClientId.isEmpty()) {
            auth.writeEntry(QStringLiteral("ClientId"), clientId);
            auth.sync(); m_clientDrafts[provider->id()] = clientId;
        }
        m_authentication->sessionFor(provider)->connectOAuth(clientId, (m_rememberAuth->isChecked() && descriptor.walletSupported));
        return;
    }
    bool ok;
    QString token = promptForToken(QStringLiteral("Login to %1").arg(provider->name()),
                                   descriptor.tokenLabel.toHtmlEscaped() + QLatin1Char(':'),
                                   &ok);
    if (ok) {
        if (m_authentication) {
            m_authentication->sessionFor(provider)->setManualToken(token, (m_rememberAuth->isChecked() && descriptor.walletSupported));
            return;
        }
        provider->setCredentials(QString(), token);
        if (!(m_rememberAuth->isChecked() && descriptor.walletSupported)) { displayInfo(QStringLiteral("Session only"), QStringLiteral("Credentials will only be used for this session.")); return; }
        SettingsDialog::PersistResult result = performPersistence(provider->id(), token);
        showPersistenceFeedback(result, provider->name());
    }
}

void SettingsDialog::updateAuthenticationUi()
{
    auto *item = m_listWidget->currentItem();
    auto *provider = item ? item->data(Qt::UserRole).value<SearchProvider *>() : nullptr;
    const auto descriptor = authenticationDescriptorV2(provider);
    const bool supported = descriptor.usable();
    const bool oauth = supported && descriptor.mode == AuthenticationMode::OAuth2Pkce;
    const bool editableClient = oauth && descriptor.publicClientId.isEmpty();
    const QString id = provider ? provider->id() : QString();
    if (m_authProviderId != id) {
        m_authProviderId = id;
        m_clientId->setText(m_clientDrafts.value(id));
    }
    m_clientId->setVisible(editableClient); m_clientId->setEnabled(editableClient);
    m_clientIdLabel->setVisible(editableClient);
    if (!editableClient) m_clientId->clear();
    for (auto *button : {m_loginButton, m_restoreAuth, m_disconnectAuth}) button->setVisible(supported);
    m_rememberAuth->setVisible(supported && descriptor.walletSupported && isWalletAvailable());
    m_authMode->setText(oauth ? QStringLiteral("OAuth 2.0 Authorization Code + PKCE (system browser)") :
        (supported ? QStringLiteral("Manually supplied %1").arg(descriptor.tokenLabel) : (descriptor.mode == AuthenticationMode::None ? QStringLiteral("No authentication required") : QStringLiteral("Declared authentication is not implemented"))));
    auto *session = m_authentication && provider ? m_authentication->sessionFor(provider) : nullptr;
    const bool busy = session && (session->state() == ProviderAuthSession::State::Authorizing || session->state() == ProviderAuthSession::State::Refreshing);
    m_loginButton->setText(oauth ? QStringLiteral("Connect / reauthorize in browser…") : QStringLiteral("Set %1…").arg(descriptor.tokenLabel));
    m_loginButton->setEnabled(supported && !busy);
    m_restoreAuth->setVisible(supported && session && descriptor.walletSupported && isWalletAvailable());
    m_restoreAuth->setEnabled(session && !busy);
    m_disconnectAuth->setVisible(supported && session);
    m_disconnectAuth->setEnabled(session != nullptr);
    m_cancelAuth->setEnabled(busy); m_cancelAuth->setVisible(supported && busy);
    m_rememberAuth->setEnabled(supported);
    m_authStatus->setVisible(supported);
    m_authStatus->setText(session ? session->statusText() : (supported ? QStringLiteral("Manual credentials can be saved to KWallet or used for this session only") : QString()));
    QString details;
    if (oauth) {
        const QString redirect = descriptor.oauth.callbackPort ? QStringLiteral("http://127.0.0.1:%1%2").arg(descriptor.oauth.callbackPort).arg(descriptor.oauth.callbackPath) :
            QStringLiteral("Provider-supported dynamic loopback port on 127.0.0.1") + descriptor.oauth.callbackPath;
        details = QStringLiteral("Register the callback in your developer application: %1\nRequested scopes: %2").arg(redirect, descriptor.oauth.scopes.join(QStringLiteral(", ")));
        if (session && !session->callbackUrl().isEmpty()) details += QStringLiteral("\nCurrent callback: ") + session->callbackUrl().toString();
    }
    if (!descriptor.guidance.isEmpty()) details += QLatin1Char('\n') + descriptor.guidance;
    m_oauthDetails->setText(details);
    m_oauthDetails->setVisible(!details.isEmpty());
}

void SettingsDialog::saveSettings()
{
    m_languages->saveSettings();
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
            if (providersGroup.hasKey(provider->id() + QStringLiteral("_Enabled")) ||
                isEnabled != item->data(Qt::UserRole + 1).toBool())
                providersGroup.writeEntry(provider->id() + QStringLiteral("_Enabled"), isEnabled);
            item->setData(Qt::UserRole + 1, isEnabled);
        }
    }

    QStringList changedPackages;
    for (int row = 0; row < m_packages.size(); ++row) {
        const auto &pkg = m_packages[row].package;
        const bool enabled = m_packageList->item(row)->checkState() == Qt::Checked;
        const bool changedEnablement = enabled != ProviderDefaults::enabled(providersGroup, pkg.id());
        const QByteArray oldApproval = providersGroup.readEntry(pkg.id() + QStringLiteral("_Approval"), QByteArray());
        const bool oldRevoked = providersGroup.readEntry(pkg.id() + QStringLiteral("_Revoked"), false);
        if (providersGroup.hasKey(pkg.id() + QStringLiteral("_Enabled")) || changedEnablement)
            providersGroup.writeEntry(pkg.id() + QStringLiteral("_Enabled"), enabled);
        if (m_approvalDrafts.contains(pkg.id())) {
            const bool approve = m_approvalDrafts.value(pkg.id());
            const auto current = ProviderPackage::fromDirectory(pkg.packageDir(), pkg.locationType());
            if (approve && (!current.isValid() || current.approvalFingerprint() != pkg.approvalFingerprint())) {
                displayWarning(QStringLiteral("Package changed"), QStringLiteral("The package changed during review. Retry discovery and review it again."));
                return;
            }
            pkg.saveApproval(providersGroup, approve);
        }
        if (m_retryPackages || changedEnablement ||
            oldApproval != providersGroup.readEntry(pkg.id() + QStringLiteral("_Approval"), QByteArray()) ||
            oldRevoked != providersGroup.readEntry(pkg.id() + QStringLiteral("_Revoked"), false))
            changedPackages.append(pkg.id());
        for (auto *provider : m_providers) if (provider->id() == pkg.id())
            provider->setEnabled(enabled && (!m_approvalDrafts.contains(pkg.id()) || m_approvalDrafts.value(pkg.id())));
    }

    if (m_clientDrafts.contains(m_authProviderId)) m_clientDrafts[m_authProviderId] = m_clientId->text().trimmed();
    for (auto it = m_clientDrafts.cbegin(); it != m_clientDrafts.cend(); ++it) {
        KConfigGroup auth(config, QStringLiteral("Authentication-") + it.key());
        auth.writeEntry(QStringLiteral("ClientId"), it.value());
    }
    KConfigGroup genGroup(config, QStringLiteral("General"));
    genGroup.writeEntry(QStringLiteral("MinimizeToTray"), m_minimizeToTrayCheck->isChecked());
    genGroup.writeEntry(QStringLiteral("ClearTabsOnClose"), m_clearTabsOnCloseCheck->isChecked());
    genGroup.writeEntry(QStringLiteral("ShowRecentActivity"), m_showRecent->isChecked());
    genGroup.writeEntry(QStringLiteral("RecordClipboardActivity"), m_recordClipboardRecent->isChecked());
    genGroup.writeEntry(QStringLiteral("PersistClipboardActivity"),
                        m_recordClipboardRecent->isChecked() && m_persistClipboardRecent->isChecked());
    ClipboardPolicy::write(genGroup, {ClipboardPolicy::Capture(m_captureMode->currentIndex()), ClipboardPolicy::Trigger(m_searchTrigger->currentIndex())});
    config->sync();
    if (m_autostartManager) {
        m_autostartManager->setAutostartEnabled(m_autostartCheck->isChecked());
    }
    emit settingsApplied();
    if (m_retryPackages || !changedPackages.isEmpty()) emit packageChangesApplied(changedPackages);
    if (m_packagesChanged) accept(); // Recreate packages only after widgets release their provider references.
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

AutostartManager *SettingsDialog::autostartManager() const
{
    return m_autostartManager.get();
}

void SettingsDialog::setAutostartManager(std::unique_ptr<AutostartManager> manager)
{
    m_autostartManager = std::move(manager);
    if (m_autostartManager && m_autostartCheck) {
        m_autostartCheck->setChecked(m_autostartManager->isAutostartEnabled());
    }
}
