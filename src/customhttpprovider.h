// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUSTOMHTTPPROVIDER_H
#define CUSTOMHTTPPROVIDER_H

#include "customhttpsource.h"
#include "searchcontinuation.h"
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

class CustomHttpProvider : public SearchProvider, public SearchProviderContinuation
{
    Q_OBJECT
    Q_INTERFACES(SearchProvider SearchProviderContinuation)

public:
    explicit CustomHttpProvider(const CustomHttpSourceDefinition &definition,
                                QObject *parent = nullptr,
                                QNetworkAccessManager *nam = nullptr);
    ~CustomHttpProvider() override;

    QString id() const override;
    QString name() const override;
    SearchProvider *clone(QObject *parent = nullptr) const override;

    void search(const QString &query) override;
    void requestPage(const QString &query, const QString &token, quint64 requestId) override;
    void cancelRequests() override;

    CustomHttpSourceDefinition definition() const;
    void setDefinition(const CustomHttpSourceDefinition &definition);
    void setSecretToken(const QString &token);

private:
    void executeRequest(const QString &query, quint64 requestId);

    CustomHttpSourceDefinition m_definition;
    QString m_secretToken;
    QNetworkAccessManager *m_nam = nullptr;
    bool m_ownsNam = false;
    QPointer<QNetworkReply> m_currentReply;
    quint64 m_currentRequestId = 0;
};

#endif // CUSTOMHTTPPROVIDER_H
