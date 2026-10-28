#include "credentialstore.h"
#include <KWallet>
#include <QTimer>

WalletCredentialStore::WalletCredentialStore(QWidget *window, QObject *parent) : CredentialStore(parent), m_window(window) {}

CredentialStoreResult WalletCredentialStore::execute(KWallet::Wallet *wallet, Operation operation, const QString &key, const QString &value)
{
    const QString folder = QStringLiteral("KLookup");
    if (!wallet->hasFolder(folder)) {
        if (operation == Operation::Remove)
            return {true, {}, {}};
        if (operation == Operation::Read)
            return {false, {}, QStringLiteral("Saved credentials are unavailable in KWallet"), CredentialStoreResult::Code::NotFound};
        if (!wallet->createFolder(folder))
            return {false, {}, QStringLiteral("Cannot create the KLookup wallet folder")};
    }
    if (!wallet->setFolder(folder))
        return {false, {}, QStringLiteral("Cannot access the KLookup wallet folder")};
    if (operation == Operation::Read) {
        QString secret;
        if (wallet->readPassword(key, secret) != 0 || secret.isEmpty())
            return {false, {}, QStringLiteral("No saved credentials for this provider"), CredentialStoreResult::Code::NotFound};
        return {true, secret, {}};
    }
    const int result =
        operation == Operation::Write ? wallet->writePassword(key, value) : (wallet->hasEntry(key) ? wallet->removeEntry(key) : 0);
    return result == 0 ? CredentialStoreResult{true, {}, {}}
                       : CredentialStoreResult{false, {}, QStringLiteral("KWallet could not update the saved credentials")};
}

void WalletCredentialStore::perform(Operation operation, const QString &key, const QString &value, bool interactive, Callback callback)
{
    if (m_wallet && m_wallet->isOpen()) {
        callback(execute(m_wallet, operation, key, value));
        return;
    }
    // Only explicit Settings actions may open/authorize/unlock a wallet. In
    // particular, background refresh never opens even an already-unlocked wallet.
    if (!interactive) {
        callback({false, {}, QStringLiteral("Wallet is not available without user interaction; using this session only")});
        return;
    }
    auto *request = new QObject(this);
    auto &pending = m_pending[key];
    pending.removeIf([](const QPointer<QObject> &item) { return item.isNull(); });
    pending.append(request);
    auto *timer = new QTimer(request);
    timer->setSingleShot(true);
    auto *wallet =
        KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), m_window ? m_window->winId() : 0, KWallet::Wallet::Asynchronous);
    if (!wallet) {
        request->deleteLater();
        callback({false, {}, QStringLiteral("KWallet is unavailable or access was declined")});
        return;
    }
    wallet->setParent(request);
    connect(timer, &QTimer::timeout, request, [request, callback] {
        request->setProperty("completed", true);
        request->deleteLater();
        callback({false, {}, QStringLiteral("KWallet operation timed out")});
    });
    connect(wallet, &KWallet::Wallet::walletOpened, request, [this, request, timer, wallet, operation, key, value, callback](bool opened) {
        if (request->property("completed").toBool())
            return;
        request->setProperty("completed", true);
        timer->stop();
        if (!opened) {
            request->deleteLater();
            callback({false, {}, QStringLiteral("KWallet could not be opened; credentials are session only")});
            return;
        }
        if (m_wallet && m_wallet != wallet)
            m_wallet->deleteLater();
        wallet->setParent(this);
        m_wallet = wallet;
        const auto result = execute(wallet, operation, key, value);
        request->deleteLater();
        callback(result);
    });
    timer->start(30000);
}

void WalletCredentialStore::read(const QString &key, bool interactive, Callback callback)
{
    perform(Operation::Read, key, {}, interactive, std::move(callback));
}
void WalletCredentialStore::write(const QString &key, const QString &value, bool interactive, Callback callback)
{
    perform(Operation::Write, key, value, interactive, std::move(callback));
}
void WalletCredentialStore::remove(const QString &key, bool interactive, Callback callback)
{
    perform(Operation::Remove, key, {}, interactive, std::move(callback));
}

void WalletCredentialStore::cancelPending(const QString &key)
{
    const auto pending = m_pending.take(key);
    for (const auto &request : pending)
        if (request)
            delete request;
}
