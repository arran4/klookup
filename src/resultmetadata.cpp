#include "resultmetadata.h"
#include "sourceurl.h"
#include <QVariant>

QUrl Metadata::safeWebUrl(const QString &text)
{
    return validatedSourceUrl(text);
}

MetadataNode Metadata::text(const QString &label, const QStringList &values)
{
    MetadataNode node;
    node.label = label;
    for (const auto &value : values) {
        if (!value.isEmpty())
            node.values.append(MetadataValue{value, {}, MetadataValue::Kind::Text});
    }
    return node;
}

namespace
{
QString boundedText(const QString &text)
{
    return text.size() > 16384 ? text.left(16384) + QStringLiteral(" [metadata display limit]") : text;
}
MetadataNode convertVariant(const QString &label, const QVariant &value, int depth, int &remaining)
{
    MetadataNode node;
    node.label = label.left(1024);
    if (remaining-- <= 0 || depth >= 12)
        return Metadata::text(node.label, {QStringLiteral("Further nested data omitted at metadata limit")});
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        int count = 0;
        for (auto it = map.cbegin(); it != map.cend() && count++ < 200 && remaining > 0; ++it)
            node.children.append(convertVariant(it.key(), it.value(), depth + 1, remaining));
        if (map.size() > 200 || remaining <= 0)
            node.children.append(Metadata::text(QStringLiteral("Display limit"), {QStringLiteral("Additional metadata omitted")}));
    } else if (value.metaType().id() == QMetaType::QVariantList || value.metaType().id() == QMetaType::QStringList) {
        const auto list = value.toList();
        for (int i = 0; i < list.size() && i < 200 && remaining > 0; ++i) {
            if (list[i].metaType().id() == QMetaType::QVariantMap || list[i].metaType().id() == QMetaType::QVariantList ||
                list[i].metaType().id() == QMetaType::QStringList)
                node.children.append(convertVariant(QString::number(i + 1), list[i], depth + 1, remaining));
            else if (!list[i].isNull()) {
                --remaining;
                node.values.append(MetadataValue{boundedText(list[i].toString()), {}, MetadataValue::Kind::Text});
            }
        }
        if (list.size() > 200 || remaining <= 0)
            node.children.append(Metadata::text(QStringLiteral("Display limit"), {QStringLiteral("Additional metadata omitted")}));
    } else if (!value.isNull())
        node.values.append(MetadataValue{boundedText(value.toString()), {}, MetadataValue::Kind::Text});
    return node;
}
} // namespace
MetadataNode Metadata::fromVariant(const QString &label, const QVariant &value, int depth)
{
    int remaining = 4000;
    return convertVariant(label, value, depth, remaining);
}
