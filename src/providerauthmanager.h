#ifndef PROVIDERAUTHMANAGER_H
#define PROVIDERAUTHMANAGER_H

#include "providerauthsession.h"
#include <QMap>

class ProviderAuthManager : public QObject
{
    Q_OBJECT
public:
    explicit ProviderAuthManager(QWidget *window, QObject *parent = nullptr);
    ProviderAuthSession *sessionFor(SearchProvider *provider);

private:
    WalletCredentialStore *m_store;
    QMap<QString, QPointer<ProviderAuthSession>> m_sessions;
};
#endif
