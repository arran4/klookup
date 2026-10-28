#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <KPageDialog>
#include <QListWidget>
#include <QMap>

class SearchProvider;
class ProviderAuthManager;
class QLabel;
class QLineEdit;
class QPushButton;

namespace KWallet {
    class Wallet;
}
class QCheckBox;

class SettingsDialog : public KPageDialog
{
    Q_OBJECT

public:
    enum class PersistResult { Success, Unavailable, OpenFailed, FolderFailed, WriteFailed };

    explicit SettingsDialog(const QList<SearchProvider*> &providers, QWidget *parent = nullptr, ProviderAuthManager *authentication = nullptr);
    ~SettingsDialog();

protected:
    virtual bool isWalletAvailable() const;
    virtual bool openWallet();
    virtual bool walletHasFolder(const QString& folder);
    virtual bool walletCreateFolder(const QString& folder);
    virtual bool walletSetFolder(const QString& folder);
    virtual int walletWritePassword(const QString& key, const QString& password);
    virtual void closeWallet();

    virtual void displayWarning(const QString& title, const QString& text);
    virtual void displayInfo(const QString& title, const QString& text);
    virtual QString promptForToken(const QString& title, const QString& label, bool *ok);

    PersistResult performPersistence(const QString &providerId, const QString &token);
    void showPersistenceFeedback(PersistResult result, const QString &providerName);

private slots:
    void saveSettings();
    void showLoginDialog();
    void updateAuthenticationUi();

private:
    QList<SearchProvider*> m_providers;
    ProviderAuthManager *m_authentication = nullptr;
    QMap<QString, QString> m_clientDrafts;
    QString m_authProviderId;
    QLineEdit *m_clientId;
    QLabel *m_authStatus;
    QLabel *m_authMode;
    QLabel *m_oauthDetails;
    QCheckBox *m_rememberAuth;
    QPushButton *m_loginButton;
    QPushButton *m_restoreAuth;
    QPushButton *m_disconnectAuth;
    QPushButton *m_cancelAuth;
    QListWidget *m_listWidget;

    QCheckBox *m_minimizeToTrayCheck;
    QCheckBox *m_monitorClipboardCheck;
    QCheckBox *m_autoSearchClipboardCheck;
    QCheckBox *m_onlyFocusedClipboardCheck;
    KWallet::Wallet *m_wallet = nullptr;
};

#endif // SETTINGSDIALOG_H
