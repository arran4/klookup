#include "settingsdialog.h"
#include "searchprovider.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QInputDialog>
#include <QMessageBox>

#ifndef Q_OS_WASM
// #include <KWallet>
#endif

SettingsDialog::SettingsDialog(const QList<SearchProvider*> &providers, QWidget *parent)
    : QDialog(parent), m_providers(providers)
{
    setWindowTitle(QStringLiteral("Settings"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    m_listWidget = new QListWidget(this);

    for (SearchProvider *provider : m_providers) {
        QListWidgetItem *item = new QListWidgetItem(provider->name(), m_listWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(provider->isEnabled() ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, QVariant::fromValue(provider));
    }

    layout->addWidget(m_listWidget);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *loginButton = new QPushButton(QStringLiteral("Login..."), this);
    QPushButton *okButton = new QPushButton(QStringLiteral("OK"), this);
    QPushButton *cancelButton = new QPushButton(QStringLiteral("Cancel"), this);

    buttonLayout->addWidget(loginButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);

    layout->addLayout(buttonLayout);

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
                                          QStringLiteral("Token/Password:"),
                                          QLineEdit::Password,
                                          QString(),
                                          &ok);
    if (ok) {
        provider->setCredentials(QString(), token);

        /*
        KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), winId());
        if (wallet) {
            if (!wallet->hasFolder(KWallet::Wallet::PasswordFolder())) {
                wallet->createFolder(KWallet::Wallet::PasswordFolder());
            }
            wallet->setFolder(KWallet::Wallet::PasswordFolder());
            wallet->writePassword(QStringLiteral("KLookup_") + provider->id(), token);
            delete wallet;
        }
        */
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
    accept();
}
