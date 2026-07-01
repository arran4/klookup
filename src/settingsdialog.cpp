#include "settingsdialog.h"
#include "searchprovider.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QTabWidget>
#include <QWidget>
#include <QLabel>

#ifndef Q_OS_WASM
#include <KWallet>
#endif

SettingsDialog::SettingsDialog(const QList<SearchProvider*> &providers, QWidget *parent)
    : QDialog(parent), m_providers(providers)
{
    setWindowTitle(QStringLiteral("Configure KLookup"));

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QTabWidget *tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget);

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
    tabWidget->addTab(providersWidget, QStringLiteral("Providers"));

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

    tabWidget->addTab(generalWidget, QStringLiteral("General"));

    // Load current settings
    QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
    m_minimizeToTrayCheck->setChecked(settings.value(QStringLiteral("MinimizeToTray"), true).toBool());
    m_monitorClipboardCheck->setChecked(settings.value(QStringLiteral("MonitorClipboard"), false).toBool());
    m_autoSearchClipboardCheck->setChecked(settings.value(QStringLiteral("AutoSearchClipboard"), false).toBool());
    m_onlyFocusedClipboardCheck->setChecked(settings.value(QStringLiteral("ClipboardOnlyWhenFocused"), true).toBool());

    connect(m_monitorClipboardCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_autoSearchClipboardCheck->setEnabled(checked);
        m_onlyFocusedClipboardCheck->setEnabled(checked);
    });
    m_autoSearchClipboardCheck->setEnabled(m_monitorClipboardCheck->isChecked());
    m_onlyFocusedClipboardCheck->setEnabled(m_monitorClipboardCheck->isChecked());

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton(QStringLiteral("OK"), this);
    QPushButton *cancelButton = new QPushButton(QStringLiteral("Cancel"), this);

    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);

    mainLayout->addLayout(buttonLayout);

    connect(loginButton, &QPushButton::clicked, this, &SettingsDialog::showLoginDialog);
    connect(okButton, &QPushButton::clicked, this, &SettingsDialog::saveSettings);
    connect(cancelButton, &QPushButton::clicked, this, &SettingsDialog::reject);
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
    }
}

void SettingsDialog::saveSettings()
{
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem *item = m_listWidget->item(i);
        SearchProvider *provider = item->data(Qt::UserRole).value<SearchProvider*>();
        if (provider) {
            provider->setEnabled(item->checkState() == Qt::Checked);
        }
    }

    QSettings settings(QStringLiteral("KDE"), QStringLiteral("klookup"));
    settings.setValue(QStringLiteral("MinimizeToTray"), m_minimizeToTrayCheck->isChecked());
    settings.setValue(QStringLiteral("MonitorClipboard"), m_monitorClipboardCheck->isChecked());
    settings.setValue(QStringLiteral("AutoSearchClipboard"), m_autoSearchClipboardCheck->isChecked());
    settings.setValue(QStringLiteral("ClipboardOnlyWhenFocused"), m_onlyFocusedClipboardCheck->isChecked());

    accept();
}
