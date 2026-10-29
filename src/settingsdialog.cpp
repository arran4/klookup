#include "settingsdialog.h"
#include "autostartmanager.h"
#include "searchprovider.h"
#include "providerauthmanager.h"
#include "customhttpsource.h"
#include "customhttpsourcemanager.h"
#include "customsourceeditdialog.h"
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
#include <QTextBrowser>
#include <QTreeWidget>
#include <QHeaderView>

#ifndef Q_OS_WASM
#include <KWallet>
#endif

SettingsDialog::SettingsDialog(const QList<SearchProvider*> &providers,
                               QWidget *parent,
                               ProviderAuthManager *authentication,
                               CustomHttpSourceManager *sourceManager)
    : KPageDialog(parent), m_providers(providers), m_authentication(authentication)
    , m_autostartManager(std::make_unique<AutostartManager>())
    , m_sourceManager(sourceManager)
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

    // Custom Sources Tab
    if (!m_sourceManager) {
        m_sourceManager = new CustomHttpSourceManager(this);
    }

    QWidget *customWidget = new QWidget(this);
    auto *customLayout = new QHBoxLayout(customWidget);
    m_customSourcesList = new QTreeWidget(customWidget);
    m_customSourcesList->setObjectName(QStringLiteral("customSourcesList"));
    m_customSourcesList->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Trust Status"), QStringLiteral("Method"), QStringLiteral("URL")});
    m_customSourcesList->setRootIsDecorated(false);
    m_customSourcesList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_customSourcesList->header()->setStretchLastSection(true);
    customLayout->addWidget(m_customSourcesList, 1);

    auto *customBtnsLayout = new QVBoxLayout;
    auto *addBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), QStringLiteral("Add..."), customWidget);
    addBtn->setObjectName(QStringLiteral("addCustomSource"));
    auto *editBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), QStringLiteral("Edit..."), customWidget);
    editBtn->setObjectName(QStringLiteral("editCustomSource"));
    auto *dupBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Duplicate"), customWidget);
    dupBtn->setObjectName(QStringLiteral("duplicateCustomSource"));
    auto *delBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), QStringLiteral("Delete"), customWidget);
    delBtn->setObjectName(QStringLiteral("deleteCustomSource"));
    auto *importBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-import")), QStringLiteral("Import..."), customWidget);
    importBtn->setObjectName(QStringLiteral("importCustomSources"));
    auto *exportBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-export")), QStringLiteral("Export..."), customWidget);
    exportBtn->setObjectName(QStringLiteral("exportCustomSources"));

    customBtnsLayout->addWidget(addBtn);
    customBtnsLayout->addWidget(editBtn);
    customBtnsLayout->addWidget(dupBtn);
    customBtnsLayout->addWidget(delBtn);
    customBtnsLayout->addSpacing(15);
    customBtnsLayout->addWidget(importBtn);
    customBtnsLayout->addWidget(exportBtn);
    customBtnsLayout->addStretch();
    customLayout->addLayout(customBtnsLayout);

    connect(addBtn, &QPushButton::clicked, this, [this] {
        CustomSourceEditDialog dlg(CustomHttpSourceDefinition(), this);
        if (dlg.exec() == QDialog::Accepted) {
            m_sourceManager->addOrUpdateSource(dlg.definition());
            refreshCustomSourcesList();
        }
    });

    connect(editBtn, &QPushButton::clicked, this, [this] {
        auto *item = m_customSourcesList->currentItem();
        if (!item) return;
        const QString id = item->data(0, Qt::UserRole).toString();
        const auto def = m_sourceManager->source(id);
        if (def.id.isEmpty()) return;
        CustomSourceEditDialog dlg(def, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_sourceManager->addOrUpdateSource(dlg.definition());
            refreshCustomSourcesList();
        }
    });

    connect(dupBtn, &QPushButton::clicked, this, [this] {
        auto *item = m_customSourcesList->currentItem();
        if (!item) return;
        const QString id = item->data(0, Qt::UserRole).toString();
        auto def = m_sourceManager->source(id);
        if (def.id.isEmpty()) return;
        def.id = QStringLiteral("%1-copy").arg(def.id);
        def.name = QStringLiteral("%1 (Copy)").arg(def.name);
        def.trusted = false;
        def.enabled = false;
        m_sourceManager->addOrUpdateSource(def);
        refreshCustomSourcesList();
    });

    connect(delBtn, &QPushButton::clicked, this, [this] {
        auto *item = m_customSourcesList->currentItem();
        if (!item) return;
        const QString id = item->data(0, Qt::UserRole).toString();
        if (QMessageBox::question(this, QStringLiteral("Confirm Deletion"),
                                  QStringLiteral("Delete custom lookup source '%1'?").arg(item->text(0))) == QMessageBox::Yes) {
            m_sourceManager->removeSource(id);
            refreshCustomSourcesList();
        }
    });

    connect(importBtn, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString json = QInputDialog::getMultiLineText(this, QStringLiteral("Import Custom Sources"),
                                                            QStringLiteral("Paste JSON source definitions:"),
                                                            QString(), &ok);
        if (ok && !json.trimmed().isEmpty()) {
            QString err;
            if (!m_sourceManager->importJson(json, &err)) {
                QMessageBox::warning(this, QStringLiteral("Import Failed"), err);
            } else {
                refreshCustomSourcesList();
                QMessageBox::information(this, QStringLiteral("Sources Imported"),
                                         QStringLiteral("Imported custom search sources successfully.\n\nAll imported sources are untrusted and disabled by default. Please review and trust them before enabling."));
            }
        }
    });

    connect(exportBtn, &QPushButton::clicked, this, [this] {
        const QString json = m_sourceManager->exportJson();
        auto *dlg = new QDialog(this);
        dlg->setWindowTitle(QStringLiteral("Exported Custom Sources"));
        dlg->resize(600, 450);
        auto *layout = new QVBoxLayout(dlg);
        auto *browser = new QTextBrowser(dlg);
        browser->setPlainText(json);
        layout->addWidget(browser);
        auto *box = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
        connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::accept);
        layout->addWidget(box);
        dlg->exec();
    });

    connect(m_customSourcesList, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (!item || column != 0) return;
        const QString id = item->data(0, Qt::UserRole).toString();
        const auto def = m_sourceManager->source(id);
        if (def.id.isEmpty()) return;

        if (item->checkState(0) == Qt::Checked) {
            if (!def.trusted) {
                QMessageBox reviewBox(this);
                reviewBox.setIcon(QMessageBox::Warning);
                reviewBox.setWindowTitle(QStringLiteral("Review & Trust Custom Source"));
                reviewBox.setText(QStringLiteral("Review External Request: %1").arg(def.name));
                reviewBox.setInformativeText(QStringLiteral(
                    "<b>Destination Origin:</b> %1<br/>"
                    "<b>HTTP Method:</b> %2<br/>"
                    "<b>URL Template:</b> %3<br/>"
                    "<b>Headers:</b> %4<br/>"
                    "<b>Requires Credentials:</b> %5<br/><br/>"
                    "<b>Warning:</b> Enabling this source will transmit search queries to an external third-party destination. "
                    "Do you trust this source and want to enable it?"
                ).arg(def.originString().toHtmlEscaped(),
                     def.method == CustomHttpSourceDefinition::Method::Post ? QStringLiteral("POST") : QStringLiteral("GET"),
                     def.urlTemplate.toHtmlEscaped(),
                     def.headers.isEmpty() ? QStringLiteral("None") : QStringLiteral("%1 custom header(s)").arg(def.headers.size()),
                     def.requiresSecret() ? QStringLiteral("Yes") : QStringLiteral("No")));
                reviewBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
                reviewBox.button(QMessageBox::Yes)->setText(QStringLiteral("Trust and Enable"));
                if (reviewBox.exec() == QMessageBox::Yes) {
                    m_sourceManager->setSourceTrusted(id, true);
                    m_sourceManager->setSourceEnabled(id, true);
                } else {
                    QSignalBlocker blocker(m_customSourcesList);
                    item->setCheckState(0, Qt::Unchecked);
                    m_sourceManager->setSourceEnabled(id, false);
                }
                refreshCustomSourcesList();
                return;
            }
            m_sourceManager->setSourceEnabled(id, true);
        } else {
            m_sourceManager->setSourceEnabled(id, false);
        }
        refreshCustomSourcesList();
    });

    refreshCustomSourcesList();
    addPage(customWidget, QStringLiteral("Custom Sources"));

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
    genGroup.writeEntry(QStringLiteral("ClearTabsOnClose"), m_clearTabsOnCloseCheck->isChecked());
    genGroup.writeEntry(QStringLiteral("ShowRecentActivity"), m_showRecent->isChecked());
    genGroup.writeEntry(QStringLiteral("RecordClipboardActivity"), m_recordClipboardRecent->isChecked());
    genGroup.writeEntry(QStringLiteral("PersistClipboardActivity"),
                        m_recordClipboardRecent->isChecked() && m_persistClipboardRecent->isChecked());
    ClipboardPolicy::write(genGroup, {ClipboardPolicy::Capture(m_captureMode->currentIndex()), ClipboardPolicy::Trigger(m_searchTrigger->currentIndex())});
    if (m_sourceManager) {
        m_sourceManager->saveSources();
    }
    config->sync();
    if (m_autostartManager) {
        m_autostartManager->setAutostartEnabled(m_autostartCheck->isChecked());
    }
    emit settingsApplied();
}

void SettingsDialog::refreshCustomSourcesList()
{
    if (!m_customSourcesList || !m_sourceManager) return;
    QSignalBlocker blocker(m_customSourcesList);
    m_customSourcesList->clear();
    for (const auto &def : m_sourceManager->sources()) {
        auto *item = new QTreeWidgetItem(m_customSourcesList);
        item->setText(0, def.name);
        item->setText(1, !def.trusted ? QStringLiteral("Untrusted (Review Required)") : (def.enabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled")));
        item->setText(2, def.method == CustomHttpSourceDefinition::Method::Post ? QStringLiteral("POST") : QStringLiteral("GET"));
        item->setText(3, def.urlTemplate);
        item->setData(0, Qt::UserRole, def.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, def.enabled ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(0, def.description.isEmpty() ? def.urlTemplate : def.description);
        if (!def.trusted) {
            item->setForeground(1, QBrush(Qt::red));
        } else {
            item->setForeground(1, QBrush(Qt::darkGreen));
        }
    }
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
