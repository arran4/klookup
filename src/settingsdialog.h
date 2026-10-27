#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <KPageDialog>
#include <QListWidget>

class SearchProvider;

namespace KWallet {
    class Wallet;
}
class QCheckBox;
class QComboBox;

class SettingsDialog : public KPageDialog
{
    Q_OBJECT

public:
    enum class PersistResult { Success, Unavailable, OpenFailed, FolderFailed, WriteFailed };

    explicit SettingsDialog(const QList<SearchProvider*> &providers, QWidget *parent = nullptr);
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

private:
    QList<SearchProvider*> m_providers;
    QListWidget *m_listWidget;

    QCheckBox *m_minimizeToTrayCheck;
    QComboBox *m_captureMode;
    QComboBox *m_searchTrigger;
    KWallet::Wallet *m_wallet = nullptr;
};

#endif // SETTINGSDIALOG_H
