// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "customhttpsourcemanager.h"
#include "customhttpprovider.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QJsonArray>
#include <QJsonDocument>

CustomHttpSourceManager::CustomHttpSourceManager(QObject *parent)
    : QObject(parent)
{
    loadSources();
}

void CustomHttpSourceManager::loadSources()
{
    m_sources.clear();
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup group(config, QStringLiteral("CustomSources"));

    const QStringList ids = group.readEntry("SourceIds", QStringList());
    for (const QString &id : ids) {
        const QString jsonStr = group.readEntry(QStringLiteral("Source_%1").arg(id), QString());
        if (!jsonStr.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (doc.isObject()) {
                const auto def = CustomHttpSourceDefinition::fromJson(doc.object());
                if (!def.id.isEmpty()) {
                    m_sources.append(def);
                }
            }
        }
    }
}

void CustomHttpSourceManager::saveSources()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup group(config, QStringLiteral("CustomSources"));
    group.deleteGroup();

    QStringList ids;
    for (const auto &def : m_sources) {
        if (def.id.isEmpty()) continue;
        ids.append(def.id);
        const QByteArray json = QJsonDocument(def.toJson(false)).toJson(QJsonDocument::Compact);
        group.writeEntry(QStringLiteral("Source_%1").arg(def.id), QString::fromUtf8(json));
    }
    group.writeEntry("SourceIds", ids);
    config->sync();
}

QList<CustomHttpSourceDefinition> CustomHttpSourceManager::sources() const
{
    return m_sources;
}

CustomHttpSourceDefinition CustomHttpSourceManager::source(const QString &id) const
{
    for (const auto &def : m_sources) {
        if (def.id == id) {
            return def;
        }
    }
    return CustomHttpSourceDefinition();
}

void CustomHttpSourceManager::addOrUpdateSource(const CustomHttpSourceDefinition &definition)
{
    for (int i = 0; i < m_sources.size(); ++i) {
        if (m_sources.at(i).id == definition.id) {
            m_sources[i] = definition;
            saveSources();
            emit sourcesChanged();
            return;
        }
    }
    m_sources.append(definition);
    saveSources();
    emit sourcesChanged();
}

void CustomHttpSourceManager::removeSource(const QString &id)
{
    const int initialCount = m_sources.size();
    m_sources.removeIf([id](const CustomHttpSourceDefinition &def) {
        return def.id == id;
    });
    if (m_sources.size() != initialCount) {
        saveSources();
        emit sourcesChanged();
    }
}

void CustomHttpSourceManager::setSourceEnabled(const QString &id, bool enabled)
{
    for (auto &def : m_sources) {
        if (def.id == id) {
            if (def.enabled != enabled) {
                def.enabled = enabled;
                saveSources();
                emit sourcesChanged();
            }
            return;
        }
    }
}

QList<SearchProvider *> CustomHttpSourceManager::createProviders(QObject *parent) const
{
    QList<SearchProvider *> providers;
    for (const auto &def : m_sources) {
        providers.append(new CustomHttpProvider(def, parent));
    }
    return providers;
}

QString CustomHttpSourceManager::exportJson() const
{
    QJsonArray arr;
    for (const auto &def : m_sources) {
        // Never export secrets
        arr.append(def.toJson(false));
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

bool CustomHttpSourceManager::importJson(const QString &jsonText, QString *errorMsg)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMsg) *errorMsg = QStringLiteral("JSON parsing error: %1").arg(parseError.errorString());
        return false;
    }

    QList<CustomHttpSourceDefinition> imported;
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const auto &val : arr) {
            if (val.isObject()) {
                const auto def = CustomHttpSourceDefinition::fromJson(val.toObject());
                QString valError;
                if (!def.isValid(&valError)) {
                    if (errorMsg) *errorMsg = QStringLiteral("Invalid source '%1': %2").arg(def.name, valError);
                    return false;
                }
                imported.append(def);
            }
        }
    } else if (doc.isObject()) {
        const auto def = CustomHttpSourceDefinition::fromJson(doc.object());
        QString valError;
        if (!def.isValid(&valError)) {
            if (errorMsg) *errorMsg = QStringLiteral("Invalid source '%1': %2").arg(def.name, valError);
            return false;
        }
        imported.append(def);
    } else {
        if (errorMsg) *errorMsg = QStringLiteral("Import root must be a JSON array or object.");
        return false;
    }

    for (const auto &def : imported) {
        addOrUpdateSource(def);
    }
    return true;
}
