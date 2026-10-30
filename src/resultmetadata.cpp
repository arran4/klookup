#include "resultmetadata.h"
#include "sourceurl.h"
#include <QIcon>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>
#include <functional>

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

void Metadata::populateMetadataTree(QTreeWidget *treeWidget, const ResultMetadata &metadata)
{
    if (!treeWidget) return;
    treeWidget->clear();

    int remaining = 4000;
    std::function<void(QTreeWidgetItem *, const MetadataNode &, int)> append =
        [&](QTreeWidgetItem *parent, const MetadataNode &node, int depth) {
            if (remaining-- <= 0 || depth > 16)
                return;
            if (node.values.isEmpty() && node.children.isEmpty())
                return;
            auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(treeWidget);
            item->setText(0, node.label);
            QStringList plainValues;
            int valueCount = 0;
            for (const auto &value : node.values) {
                if (++valueCount > 200 || remaining-- <= 0)
                    break;
                const auto url = Metadata::safeWebUrl(value.url.toString(QUrl::FullyEncoded));
                if (value.kind == MetadataValue::Kind::Text || url.isEmpty()) {
                    plainValues.append(value.text);
                } else {
                    auto *link = new QTreeWidgetItem(item);
                    link->setText(0, value.kind == MetadataValue::Kind::Image ? QStringLiteral("Image reference") : QStringLiteral("Source link"));
                    link->setText(1, value.text.isEmpty() ? url.toDisplayString() : value.text);
                    link->setToolTip(1, url.toDisplayString());
                    link->setData(1, Qt::UserRole, url.toString(QUrl::FullyEncoded));
                    link->setForeground(1, treeWidget->palette().link());
                    link->setIcon(0, QIcon::fromTheme(QStringLiteral("internet-web-browser")));
                }
            }
            item->setText(1, plainValues.join(QStringLiteral("; ")));
            item->setToolTip(1, QStringLiteral("<p>%1</p>").arg(plainValues.join(QLatin1Char('\n')).toHtmlEscaped()));
            for (const auto &child : node.children) {
                append(item, child, depth + 1);
            }
            item->setExpanded(depth < 2);
        };

    for (const auto &sec : metadata.sections) {
        append(nullptr, sec, 0);
    }
    if (remaining <= 0) {
        auto *notice = new QTreeWidgetItem(treeWidget);
        notice->setText(0, QStringLiteral("Additional metadata omitted at display limit"));
    }
    // Attribution stays visible even for an unusually large upstream record.
    remaining = qMax(remaining, 201);
    if (!metadata.attribution.isEmpty()) {
        append(nullptr, Metadata::text(QStringLiteral("Attribution"), metadata.attribution), 0);
    }
    treeWidget->setVisible(treeWidget->topLevelItemCount() > 0);
}
