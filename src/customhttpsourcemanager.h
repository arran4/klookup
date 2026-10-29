// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUSTOMHTTPSOURCEMANAGER_H
#define CUSTOMHTTPSOURCEMANAGER_H

#include "customhttpsource.h"
#include <QObject>
#include <QList>

class SearchProvider;

class CustomHttpSourceManager : public QObject
{
    Q_OBJECT

public:
    explicit CustomHttpSourceManager(QObject *parent = nullptr);
    ~CustomHttpSourceManager() override = default;

    void loadSources();
    void saveSources();

    QList<CustomHttpSourceDefinition> sources() const;
    CustomHttpSourceDefinition source(const QString &id) const;

    void addOrUpdateSource(const CustomHttpSourceDefinition &definition);
    void removeSource(const QString &id);
    void setSourceEnabled(const QString &id, bool enabled);
    void setSourceTrusted(const QString &id, bool trusted);

    QList<SearchProvider *> createProviders(QObject *parent = nullptr) const;

    QString exportJson() const;
    bool importJson(const QString &jsonText, QString *errorMsg = nullptr);

signals:
    void sourcesChanged();

private:
    QList<CustomHttpSourceDefinition> m_sources;
};

#endif // CUSTOMHTTPSOURCEMANAGER_H
