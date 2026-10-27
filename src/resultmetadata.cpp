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

MetadataNode Metadata::fromVariant(const QString &label, const QVariant &value, int depth)
{
    MetadataNode node;
    node.label = label;
    if (depth >= 12)
        return text(label, {QStringLiteral("Further nested data omitted")});
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        int count = 0;
        for (auto it = map.cbegin(); it != map.cend() && count++ < 200; ++it)
            node.children.append(fromVariant(it.key(), it.value(), depth + 1));
    } else if (value.metaType().id() == QMetaType::QVariantList || value.metaType().id() == QMetaType::QStringList) {
        const auto list = value.toList();
        for (int i = 0; i < list.size() && i < 200; ++i) {
            if (list[i].metaType().id() == QMetaType::QVariantMap || list[i].metaType().id() == QMetaType::QVariantList)
                node.children.append(fromVariant(QString::number(i + 1), list[i], depth + 1));
            else if (!list[i].isNull())
                node.values.append(MetadataValue{list[i].toString(), {}, MetadataValue::Kind::Text});
        }
    } else if (!value.isNull())
        node.values.append(MetadataValue{value.toString(), {}, MetadataValue::Kind::Text});
    return node;
}
