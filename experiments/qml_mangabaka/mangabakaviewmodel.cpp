// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mangabakaviewmodel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QVariantMap>

MangaBakaViewModel::MangaBakaViewModel(QObject *parent)
    : QObject(parent)
{
}

bool MangaBakaViewModel::loadFromJsonFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonParseError parseError = []() { return QJsonParseError{}; }();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(1024 * 1024), &err);
    Q_UNUSED(parseError);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    return loadFromJsonObject(doc.object());
}

bool MangaBakaViewModel::loadFromJsonObject(const QJsonObject &root)
{
    const QJsonObject obj = root.contains(QStringLiteral("data")) && root.value(QStringLiteral("data")).isObject()
                                ? root.value(QStringLiteral("data")).toObject()
                                : root;
    if (obj.isEmpty())
        return false;

    m_seriesId = QString::number(obj.value(QStringLiteral("id")).toInteger());
    m_primaryTitle = obj.value(QStringLiteral("title")).toString();
    m_romanizedTitle = obj.value(QStringLiteral("romanized_title")).toString();
    m_nativeTitle = obj.value(QStringLiteral("native_title")).toString();
    m_formatType = obj.value(QStringLiteral("type")).toString();
    m_status = obj.value(QStringLiteral("status")).toString();
    m_contentRating = obj.value(QStringLiteral("content_rating")).toString();
    m_originalLanguage = obj.value(QStringLiteral("original_language")).toString();
    m_rating = obj.value(QStringLiteral("rating")).toDouble();
    m_ratingCount = obj.value(QStringLiteral("rating_count")).toInt();
    m_year = obj.value(QStringLiteral("year")).toInt();
    m_summary = obj.value(QStringLiteral("description")).toString();

    m_genres.clear();
    for (const QJsonValue &v : obj.value(QStringLiteral("genres")).toArray())
        m_genres.append(v.toString());

    m_tags.clear();
    for (const QJsonValue &v : obj.value(QStringLiteral("tags")).toArray())
        m_tags.append(v.toString());

    m_creators.clear();
    for (const QJsonValue &v : obj.value(QStringLiteral("authors")).toArray())
        m_creators.append(QStringLiteral("Author: ") + v.toString());
    for (const QJsonValue &v : obj.value(QStringLiteral("artists")).toArray())
        m_creators.append(QStringLiteral("Artist: ") + v.toString());

    m_alternateTitles.clear();
    for (const QJsonValue &v : obj.value(QStringLiteral("titles")).toArray()) {
        const QJsonObject t = v.toObject();
        QVariantMap row;
        row.insert(QStringLiteral("language"), t.value(QStringLiteral("language")).toString());
        row.insert(QStringLiteral("title"), t.value(QStringLiteral("title")).toString());
        row.insert(QStringLiteral("isPrimary"), t.value(QStringLiteral("is_primary")).toBool());
        m_alternateTitles.append(row);
    }

    m_links.clear();
    for (const QJsonValue &v : obj.value(QStringLiteral("links")).toArray())
        m_links.append(v.toString());

    emit dataChanged();
    return true;
}

QString MangaBakaViewModel::toFallbackHtml() const
{
    QString html;
    html += QStringLiteral("<h2>%1</h2>").arg(m_primaryTitle.toHtmlEscaped());
    html += QStringLiteral("<p><b>Type:</b> %1 | <b>Status:</b> %2 | <b>Rating:</b> %3 (%4 votes) | <b>Content rating:</b> %5</p>")
                .arg(m_formatType.toHtmlEscaped(), m_status.toHtmlEscaped(), QString::number(m_rating, 'f', 1),
                     QString::number(m_ratingCount), m_contentRating.toHtmlEscaped());
    html += QStringLiteral("<p>%1</p>").arg(m_summary.toHtmlEscaped());
    html += QStringLiteral("<p><b>Genres:</b> %1</p>").arg(m_genres.join(QStringLiteral(", ")).toHtmlEscaped());
    return html;
}
