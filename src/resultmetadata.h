#ifndef RESULTMETADATA_H
#define RESULTMETADATA_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <QUrl>
#include <QVariant>

struct MetadataValue {
    enum class Kind { Text, Link, Image };
    QString text;
    QUrl url;
    Kind kind = Kind::Text;
};

// A section, field or nested category. Values remain separate; links and image
// references have explicit types. Labels and values are always untrusted text.
struct MetadataNode {
    QString label;
    QList<MetadataValue> values;
    QList<MetadataNode> children;
};

struct ResultMetadata {
    QList<MetadataNode> sections;
    QStringList attribution;
};
Q_DECLARE_METATYPE(ResultMetadata)

class QTreeWidget;

namespace Metadata
{
QUrl safeWebUrl(const QString &text);
MetadataNode text(const QString &label, const QStringList &values);
// Legacy QVariant containers get a bounded recursive fallback, never toString()
// on a list/map. Provider-specific mapping should prefer labelled typed nodes.
MetadataNode fromVariant(const QString &label, const QVariant &value, int depth = 0);
// Shared renderer to populate metadata tree widgets identically in SearchTab and ResultDetailWindow
void populateMetadataTree(QTreeWidget *treeWidget, const ResultMetadata &metadata);
} // namespace Metadata
#endif
