// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PROVIDER_MANGABAKA_H
#define PROVIDER_MANGABAKA_H

#include "provideroptions.h"
#include "searchcontinuation.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

class ProviderMangaBaka : public SearchProvider, public SearchProviderContinuation, public SearchProviderOptions
{
    Q_OBJECT
#ifndef KLOOKUP_MANGABAKA_AS_BASE
    Q_PLUGIN_METADATA(IID "org.kde.klookup.SearchProvider")
#endif
    Q_INTERFACES(SearchProvider SearchProviderContinuation SearchProviderOptions)
public:
    explicit ProviderMangaBaka(QObject *parent = nullptr);
    virtual ~ProviderMangaBaka();

    QString name() const override;
    QString id() const override;
    void search(const QString &query) override;
    void requestPage(const QString &query, const QString &token, quint64 requestId) override;
    bool supportsDetails() const override
    {
        return true;
    }
    void requestDetails(const QString &resultId, const QString &token, quint64 requestId) override;
    void cancelRequests() override;
    SearchProvider *clone(QObject *parent = nullptr) const override;
    QWidget *createOptionsWidget(QWidget *parent) const override;
    void saveOptions(QWidget *widget) override;
    QStringList formats() const
    {
        return m_formats;
    }
    static QStringList supportedFormats();
    static QString formatLabel(const QString &type);

    static void clearSharedCache();
    static int sharedCacheNetworkRequestCount();

protected:
    SearchResult parseResult(const QJsonObject &obj) const;
    ResultMetadata parseMetadata(const QJsonObject &obj) const;
    virtual SearchResult decorateSearchResult(SearchResult result, const QJsonObject &rawObj) const;
    virtual SearchDetails decorateSearchDetails(SearchDetails details, const QJsonObject &rawObj) const;
    void cloneStateInto(ProviderMangaBaka *target) const;

    QNetworkAccessManager *m_nam;
    QNetworkAccessManager *m_defaultNam = nullptr;
    bool m_forceSharedCacheForTesting = false;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_detailReply;

private:
    void handleSearchRoot(const QJsonObject &root, const QString &token, quint64 requestId);
    void handleDetailRoot(const QJsonObject &root, const QString &resultId, quint64 requestId);

    QStringList m_formats;
};

#endif // PROVIDER_MANGABAKA_H
