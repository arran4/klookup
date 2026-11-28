// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "provider_mangabaka_experimental.h"
#include "../manga_metadata.h"
#include "resultmetadata.h"
#include <KLocalizedString>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{

QVariantMap sanitizeSeriesPayload(const QJsonObject &obj)
{
    using namespace MangaMetadata;
    QVariantMap payload;
    payload.insert(QStringLiteral("id"), obj.value(QStringLiteral("id")).toInt());
    payload.insert(QStringLiteral("title"), scalar(obj.value(QStringLiteral("title"))));
    payload.insert(QStringLiteral("description"), obj.value(QStringLiteral("description")).toString());

    const QUrl safeCanonical = Metadata::safeWebUrl(obj.value(QStringLiteral("canonical_url")).toString());
    if (!safeCanonical.isEmpty())
        payload.insert(QStringLiteral("canonical_url"), safeCanonical.toString());

    const QString upstreamType = obj.value(QStringLiteral("type")).toString();
    if (!upstreamType.isEmpty())
        payload.insert(QStringLiteral("type"), upstreamType);
    if (obj.value(QStringLiteral("status")).isString())
        payload.insert(QStringLiteral("status"), obj.value(QStringLiteral("status")).toString());
    if (obj.value(QStringLiteral("original_language")).isString())
        payload.insert(QStringLiteral("original_language"), obj.value(QStringLiteral("original_language")).toString());
    if (obj.value(QStringLiteral("content_rating")).isString())
        payload.insert(QStringLiteral("content_rating"), obj.value(QStringLiteral("content_rating")).toString());
    if (obj.value(QStringLiteral("rating")).isDouble())
        payload.insert(QStringLiteral("rating"), obj.value(QStringLiteral("rating")).toDouble());
    if (obj.value(QStringLiteral("rating_count")).isDouble())
        payload.insert(QStringLiteral("rating_count"), obj.value(QStringLiteral("rating_count")).toInt());
    if (obj.value(QStringLiteral("year")).isDouble())
        payload.insert(QStringLiteral("year"), obj.value(QStringLiteral("year")).toInt());
    if (obj.contains(QStringLiteral("is_licensed")) && obj.value(QStringLiteral("is_licensed")).isBool())
        payload.insert(QStringLiteral("is_licensed"), obj.value(QStringLiteral("is_licensed")).toBool());
    if (!scalar(obj.value(QStringLiteral("final_volume"))).isEmpty())
        payload.insert(QStringLiteral("final_volume"), scalar(obj.value(QStringLiteral("final_volume"))));
    if (!scalar(obj.value(QStringLiteral("total_chapters"))).isEmpty())
        payload.insert(QStringLiteral("total_chapters"), scalar(obj.value(QStringLiteral("total_chapters"))));

    const QJsonObject published = obj.value(QStringLiteral("published")).toObject();
    if (!published.isEmpty()) {
        QVariantMap pubMap;
        if (published.value(QStringLiteral("start_date")).isString())
            pubMap.insert(QStringLiteral("start_date"), published.value(QStringLiteral("start_date")).toString());
        if (published.value(QStringLiteral("end_date")).isString())
            pubMap.insert(QStringLiteral("end_date"), published.value(QStringLiteral("end_date")).toString());
        if (!pubMap.isEmpty())
            payload.insert(QStringLiteral("published"), pubMap);
    }

    auto readStringList = [&obj](const QString &key) {
        QStringList list;
        for (const auto &v : boundedArray(obj.value(key))) {
            const QString s = scalar(v).trimmed();
            if (!s.isEmpty())
                list.append(s);
        }
        return list;
    };
    const QStringList authors = readStringList(QStringLiteral("authors"));
    if (!authors.isEmpty())
        payload.insert(QStringLiteral("authors"), authors);
    const QStringList artists = readStringList(QStringLiteral("artists"));
    if (!artists.isEmpty())
        payload.insert(QStringLiteral("artists"), artists);

    QVariantList titleList;
    for (const auto &v : boundedArray(obj.value(QStringLiteral("titles")))) {
        const auto t = v.toObject();
        const QString text = t.value(QStringLiteral("title")).toString();
        if (text.isEmpty())
            continue;
        QVariantMap tm;
        tm.insert(QStringLiteral("language"), t.value(QStringLiteral("language")).toString());
        tm.insert(QStringLiteral("title"), text);
        tm.insert(QStringLiteral("is_primary"), t.value(QStringLiteral("is_primary")).toBool(false));
        QStringList traits;
        for (const auto &tr : boundedArray(t.value(QStringLiteral("traits")))) {
            if (tr.isString())
                traits.append(tr.toString());
        }
        if (!traits.isEmpty())
            tm.insert(QStringLiteral("traits"), traits);
        if (t.value(QStringLiteral("note")).isString())
            tm.insert(QStringLiteral("note"), t.value(QStringLiteral("note")).toString());
        titleList.append(tm);
        if (titleList.size() >= 128)
            break;
    }
    if (!titleList.isEmpty())
        payload.insert(QStringLiteral("titles"), titleList);

    QVariantList pubList;
    for (const auto &v : boundedArray(obj.value(QStringLiteral("publishers")))) {
        const auto p = v.toObject();
        const QString pName = p.value(QStringLiteral("name")).toString();
        if (pName.isEmpty())
            continue;
        QVariantMap pm;
        pm.insert(QStringLiteral("name"), pName);
        if (p.value(QStringLiteral("language")).isString())
            pm.insert(QStringLiteral("language"), p.value(QStringLiteral("language")).toString());
        if (p.value(QStringLiteral("note")).isString())
            pm.insert(QStringLiteral("note"), p.value(QStringLiteral("note")).toString());
        const QUrl pUrl = Metadata::safeWebUrl(p.value(QStringLiteral("canonical_url")).toString());
        if (!pUrl.isEmpty())
            pm.insert(QStringLiteral("url"), pUrl.toString());
        pubList.append(pm);
    }
    if (!pubList.isEmpty())
        payload.insert(QStringLiteral("publishers"), pubList);

    QVariantList tagList;
    for (const auto &v : boundedArray(obj.value(QStringLiteral("tags")))) {
        const auto t = v.toObject();
        const QString tagName = t.value(QStringLiteral("name")).toString();
        if (tagName.isEmpty())
            continue;
        QVariantMap tm;
        tm.insert(QStringLiteral("id"), t.value(QStringLiteral("id")).toInt());
        tm.insert(QStringLiteral("parent_id"), t.value(QStringLiteral("parent_id")).toInt());
        tm.insert(QStringLiteral("name"), tagName);
        if (t.value(QStringLiteral("name_path")).isString())
            tm.insert(QStringLiteral("name_path"), t.value(QStringLiteral("name_path")).toString());
        if (!scalar(t.value(QStringLiteral("weight"))).isEmpty())
            tm.insert(QStringLiteral("weight"), scalar(t.value(QStringLiteral("weight"))));
        tm.insert(QStringLiteral("is_genre"), t.value(QStringLiteral("is_genre")).toBool(false));
        tm.insert(QStringLiteral("is_spoiler"), t.value(QStringLiteral("is_spoiler")).toBool(false));
        const bool hasExplicitFlag = t.contains(QStringLiteral("is_explicit")) && t.value(QStringLiteral("is_explicit")).isBool();
        const QJsonArray impliedBy = boundedArray(t.value(QStringLiteral("implied_by_tag_ids")));
        const bool isExplicit = hasExplicitFlag ? t.value(QStringLiteral("is_explicit")).toBool() : impliedBy.isEmpty();
        tm.insert(QStringLiteral("is_explicit"), isExplicit);
        if (!impliedBy.isEmpty()) {
            QStringList impliedIds;
            for (const auto &iv : impliedBy) {
                const QString s = scalar(iv);
                if (!s.isEmpty())
                    impliedIds.append(s);
            }
            tm.insert(QStringLiteral("implied_by_tag_ids"), impliedIds);
        }
        if (t.value(QStringLiteral("content_rating")).isString())
            tm.insert(QStringLiteral("content_rating"), t.value(QStringLiteral("content_rating")).toString());
        if (t.value(QStringLiteral("description")).isString())
            tm.insert(QStringLiteral("description"), t.value(QStringLiteral("description")).toString());
        tagList.append(tm);
    }
    if (!tagList.isEmpty())
        payload.insert(QStringLiteral("tags"), tagList);

    const QJsonObject sourceObj = obj.value(QStringLiteral("source")).toObject();
    if (!sourceObj.isEmpty()) {
        QVariantList sourceList;
        for (auto it = sourceObj.begin(); it != sourceObj.end(); ++it) {
            if (!it.value().isObject())
                continue;
            const auto src = it.value().toObject();
            QVariantMap sm;
            sm.insert(QStringLiteral("source"), it.key());
            if (!scalar(src.value(QStringLiteral("id"))).isEmpty())
                sm.insert(QStringLiteral("id"), scalar(src.value(QStringLiteral("id"))));
            if (!scalar(src.value(QStringLiteral("rating"))).isEmpty())
                sm.insert(QStringLiteral("rating"), scalar(src.value(QStringLiteral("rating"))));
            sourceList.append(sm);
        }
        if (!sourceList.isEmpty())
            payload.insert(QStringLiteral("sources"), sourceList);
    }

    QVariantList linkList;
    for (const auto &v : boundedArray(obj.value(QStringLiteral("links")))) {
        const auto l = v.toObject();
        const QUrl safe = Metadata::safeWebUrl(l.value(QStringLiteral("url")).toString());
        if (safe.isEmpty())
            continue;
        QVariantMap lm;
        lm.insert(QStringLiteral("label"),
                  l.value(QStringLiteral("name_display")).toString(l.value(QStringLiteral("name")).toString(safe.host())));
        lm.insert(QStringLiteral("url"), safe.toString());
        lm.insert(QStringLiteral("type"), l.value(QStringLiteral("type")).toString());
        linkList.append(lm);
    }
    if (!linkList.isEmpty())
        payload.insert(QStringLiteral("links"), linkList);

    const QJsonObject coverObj = obj.value(QStringLiteral("cover")).toObject();
    const QString rawCover = coverObj.value(QStringLiteral("x250")).toString(coverObj.value(QStringLiteral("raw")).toString());
    const QUrl safeCover = Metadata::safeWebUrl(rawCover);
    if (!safeCover.isEmpty())
        payload.insert(QStringLiteral("cover_url"), safeCover.toString());

    return payload;
}

} // namespace

ProviderMangaBakaExperimental::ProviderMangaBakaExperimental(QObject *parent)
    : ProviderMangaBaka(parent)
{
}

QString ProviderMangaBakaExperimental::name() const
{
    return i18n("MangaBaka Experimental");
}

QString ProviderMangaBakaExperimental::id() const
{
    return QStringLiteral("mangabaka_experimental");
}

SearchProvider *ProviderMangaBakaExperimental::clone(QObject *parent) const
{
    auto *copy = new ProviderMangaBakaExperimental(parent);
    cloneStateInto(copy);
    return copy;
}

SearchResult ProviderMangaBakaExperimental::decorateSearchResult(SearchResult result, const QJsonObject &rawObj) const
{
    const QVariantMap structured = sanitizeSeriesPayload(rawObj);
    if (!structured.isEmpty()) {
        result.additionalData.insert(QStringLiteral("mangabaka_experimental_v2"), structured);
        result.additionalData.insert(QStringLiteral("rawSources"), QVariantList{
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("mangabaka_v2_json")},
                {QStringLiteral("label"), QStringLiteral("MangaBaka API v2 JSON")},
                {QStringLiteral("mimeType"), QStringLiteral("application/json")},
                {QStringLiteral("provenance"), result.url},
                {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(rawObj).toJson(QJsonDocument::Indented))}
            }
        });
    }
    return result;
}

SearchDetails ProviderMangaBakaExperimental::decorateSearchDetails(SearchDetails details, const QJsonObject &rawObj) const
{
    details.result = decorateSearchResult(std::move(details.result), rawObj);
    return details;
}
