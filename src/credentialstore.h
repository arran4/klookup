#ifndef CREDENTIALSTORE_H
#define CREDENTIALSTORE_H

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QWidget>
#include <functional>

struct CredentialStoreResult {
    enum class Code { None, NotFound, Failed };
    bool ok = false;
    QString value;
    QString error;
    Code code = Code::Failed;
};
class CredentialStore : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const CredentialStoreResult &)>;
    using QObject::QObject;
    virtual void cancelPending(const QString &key) = 0;
    virtual void read(const QString &key, bool interactive, Callback callback) = 0;
    virtual void write(const QString &key, const QString &value, bool interactive, Callback callback) = 0;
    virtual void remove(const QString &key, bool interactive, Callback callback) = 0;
};

namespace KWallet
{
class Wallet;
}
class WalletCredentialStore : public CredentialStore
{
    Q_OBJECT
public:
    explicit WalletCredentialStore(QWidget *window, QObject *parent = nullptr);
    void cancelPending(const QString &key) override;
    void read(const QString &key, bool interactive, Callback callback) override;
    void write(const QString &key, const QString &value, bool interactive, Callback callback) override;
    void remove(const QString &key, bool interactive, Callback callback) override;

private:
    enum class Operation { Read, Write, Remove };
    void perform(Operation operation, const QString &key, const QString &value, bool interactive, Callback callback);
    CredentialStoreResult execute(KWallet::Wallet *wallet, Operation operation, const QString &key, const QString &value);
    QPointer<QWidget> m_window;
    QPointer<KWallet::Wallet> m_wallet;
    QMap<QString, QList<QPointer<QObject>>> m_pending;
};
#endif
