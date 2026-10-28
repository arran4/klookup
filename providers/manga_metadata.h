#ifndef MANGA_METADATA_H
#define MANGA_METADATA_H

#include "resultmetadata.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

namespace MangaMetadata
{
// The transport is bounded, but thousands of sibling categories still cause
// quadratic category scans and excessive model allocation before GUI limits.
inline QJsonArray boundedArray(const QJsonValue &value)
{
    const auto input = value.toArray();
    QJsonArray output;
    for (int i = 0; i < input.size() && i < 512; ++i)
        output.append(input.at(i));
    return output;
}
inline bool hasOversizedList(const QJsonObject &object)
{
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.value().isArray() && it.value().toArray().size() > 512)
            return true;
    }
    return false;
}
inline QString scalar(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 15);
    if (value.isBool())
        return value.toBool() ? QStringLiteral("Yes") : QStringLiteral("No");
    return {};
}
inline void field(MetadataNode &parent, const QString &label, const QJsonValue &value)
{
    auto node = Metadata::fromVariant(label, value.toVariant());
    if (!node.values.isEmpty() || !node.children.isEmpty())
        parent.children.append(node);
}
inline void link(MetadataNode &parent, const QString &label, const QString &url, MetadataValue::Kind kind = MetadataValue::Kind::Link)
{
    const auto safe = Metadata::safeWebUrl(url);
    if (!safe.isEmpty())
        parent.values.append(MetadataValue{label, safe, kind});
}
inline MetadataNode &category(MetadataNode &parent, const QString &label)
{
    for (auto &child : parent.children)
        if (child.label == label)
            return child;
    parent.children.append(MetadataNode{label, {}, {}});
    return parent.children.last();
}
inline void localized(MetadataNode &parent, const QJsonObject &object)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        field(parent, it.key(), it.value());
}
} // namespace MangaMetadata
#endif
