#include "provider_mangabaka.h"
#include "../manga_metadata.h"
#include "providerrequest.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QCheckBox>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

ProviderMangaBaka::ProviderMangaBaka(QObject *parent) : SearchProvider(parent), m_nam(new QNetworkAccessManager(this)), m_reply(nullptr)
{
    const KConfigGroup config(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Provider-mangabaka"));
    const auto saved = config.readEntry(QStringLiteral("Formats"), QStringList());
    for (const auto &type : supportedFormats())
        if (saved.contains(type))
            m_formats.append(type);
}

ProviderMangaBaka::~ProviderMangaBaka()
{
    cancelRequests();
}

QString ProviderMangaBaka::name() const
{
    return QStringLiteral("MangaBaka");
}

QString ProviderMangaBaka::id() const
{
    return QStringLiteral("mangabaka");
}

void ProviderMangaBaka::search(const QString &query)
{
    requestPage(query, QString(), 0);
}

void ProviderMangaBaka::cancelRequests()
{
    ProviderRequest::cancel(m_reply, this);
    ProviderRequest::cancel(m_detailReply, this);
}

SearchResult ProviderMangaBaka::parseResult(const QJsonObject &obj) const
{
    SearchResult res;
    // API returns id as number but we store it as string.
    QJsonValue idVal = obj.value(QStringLiteral("id"));
    const int numericId = idVal.toInt(-1);
    if (!idVal.isDouble() || numericId <= 0 || idVal.toDouble() != numericId)
        return {}; // Stable IDs must be integral and within the API's int32 range.
    res.id = QString::number(numericId);

    QJsonArray titlesArray = obj.value(QStringLiteral("titles")).toArray();
    for (const QJsonValue &tVal : titlesArray) {
        QJsonObject tObj = tVal.toObject();
        if (tObj.value(QStringLiteral("language")).toString() == QStringLiteral("en")) {
            res.title = tObj.value(QStringLiteral("title")).toString();
            break;
        }
    }
    if (res.title.isEmpty() && !titlesArray.isEmpty()) {
        res.title = titlesArray.first().toObject().value(QStringLiteral("title")).toString();
    }
    if (res.title.isEmpty()) {
        res.title = obj.value(QStringLiteral("title")).toString();
    }

    res.description = obj.value(QStringLiteral("description")).toString();

    res.url = obj.value(QStringLiteral("canonical_url")).toString();
    const QString upstreamType = obj.value(QStringLiteral("type")).toString();
    res.type = formatLabel(upstreamType);

    QVariantMap additional;
    if (!upstreamType.isEmpty())
        additional[QStringLiteral("Upstream format")] = upstreamType;
    if (obj.contains(QStringLiteral("status")) && obj.value(QStringLiteral("status")).isString()) {
        additional[QStringLiteral("status")] = obj.value(QStringLiteral("status")).toString();
    }
    if (obj.contains(QStringLiteral("year")) && obj.value(QStringLiteral("year")).isDouble()) {
        additional[QStringLiteral("year")] = obj.value(QStringLiteral("year")).toInt();
    }
    if (!additional.isEmpty()) {
        res.additionalData = additional;
    }

    return res;
}

void ProviderMangaBaka::requestPage(const QString &query, const QString &token, quint64 requestId)
{
    bool ok = false;
    const int position = token.toInt(&ok);
    if (!token.isEmpty() && (!ok || position < 1 || position > 1200)) {
        ProviderRequest::fail(this, requestId, QStringLiteral("Invalid continuation token"));
        return;
    }
    QUrl url(QStringLiteral("https://api.mangabaka.org/v2/series/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    for (const auto &type : m_formats)
        q.addQueryItem(QStringLiteral("type"), type);
    if (!token.isEmpty())
        q.addQueryItem(QStringLiteral("page"), token);
    url.setQuery(q);
    ProviderRequest::getJson(this, m_nam, m_reply, url, requestId, [this, requestId, token](const QJsonObject &root) {
        if (!root.value(QStringLiteral("data")).isArray()) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Malformed response: missing 'data' array"));
            return;
        }
        SearchPage page;
        const auto values = root.value(QStringLiteral("data")).toArray();
        for (const auto &value : values) {
            const auto result = parseResult(value.toObject());
            if (!result.id.isEmpty())
                page.results.append(result);
        }
        const auto pagination = root.value(QStringLiteral("pagination")).toObject();
        const QUrl next(pagination.value(QStringLiteral("next")).toString(), QUrl::StrictMode);
        const auto nextQuery = QUrlQuery(next);
        bool ok = false;
        const int pageNumber = nextQuery.queryItemValue(QStringLiteral("page")).toInt(&ok);
        const int current = token.isEmpty() ? 1 : token.toInt();
        if (next.isValid() && next.scheme() == QStringLiteral("https") && next.host() == QStringLiteral("api.mangabaka.org") &&
            next.path() == QStringLiteral("/v2/series/search") && next.userInfo().isEmpty() && ok && pageNumber > current &&
            pageNumber <= 1200) {
            page.nextToken = QString::number(pageNumber);
        }

        if (requestId)
            emit pageCompleted(requestId, page);
        else
            emit searchCompleted(id(), page.results);
    });
}

void ProviderMangaBaka::requestDetails(const QString &resultId, const QString &token, quint64 requestId)
{
    if (!token.isEmpty() || !QRegularExpression(QStringLiteral("^[1-9][0-9]{0,9}$")).match(resultId).hasMatch()) {
        ProviderRequest::fail(this, requestId, QStringLiteral("Invalid result identity or detail token"));
        return;
    }
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(QStringLiteral("api.mangabaka.org"));
    url.setPath(QStringLiteral("/v2/series/") + resultId);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("schema"), QStringLiteral("full"));
    url.setQuery(q);
    ProviderRequest::getJson(this, m_nam, m_detailReply, url, requestId, [this, resultId, requestId](const QJsonObject &root) {
        const auto result = parseResult(root.value(QStringLiteral("data")).toObject());
        if (result.id != resultId) {
            ProviderRequest::fail(this, requestId, QStringLiteral("Malformed detail response: result identity mismatch"));
            return;
        }
        emit detailsCompleted(requestId, SearchDetails{result, QString(), parseMetadata(root.value(QStringLiteral("data")).toObject())});
    });
}

SearchProvider *ProviderMangaBaka::clone(QObject *parent) const
{
    auto *provider = new ProviderMangaBaka(parent);
    provider->m_formats = m_formats;
    return provider;
}

ResultMetadata ProviderMangaBaka::parseMetadata(const QJsonObject &obj) const
{
    using namespace MangaMetadata;
    ResultMetadata metadata;
    MetadataNode series{QStringLiteral("Series"), {}, {}};
    const QList<QPair<QString, QString>> fields = {{QStringLiteral("status"), QStringLiteral("Publication status")},
                                                   {QStringLiteral("type"), QStringLiteral("Format")},
                                                   {QStringLiteral("original_language"), QStringLiteral("Original language")},
                                                   {QStringLiteral("content_rating"), QStringLiteral("Content rating")},
                                                   {QStringLiteral("authors"), QStringLiteral("Authors")},
                                                   {QStringLiteral("artists"), QStringLiteral("Artists")},
                                                   {QStringLiteral("published"), QStringLiteral("Publication dates")},
                                                   {QStringLiteral("year"), QStringLiteral("Year")},
                                                   {QStringLiteral("rating"), QStringLiteral("Rating (0–100)")},
                                                   {QStringLiteral("rating_count"), QStringLiteral("Rating count")},
                                                   {QStringLiteral("popularity"), QStringLiteral("Popularity ranks")},
                                                   {QStringLiteral("is_licensed"), QStringLiteral("Licensed in English")},
                                                   {QStringLiteral("final_volume"), QStringLiteral("Final volume")},
                                                   {QStringLiteral("total_chapters"), QStringLiteral("Total chapters")},
                                                   {QStringLiteral("state"), QStringLiteral("Record state")},
                                                   {QStringLiteral("merged_with"), QStringLiteral("Merged into series")},
                                                   {QStringLiteral("last_updated_at"), QStringLiteral("Last updated")},
                                                   {QStringLiteral("anime"), QStringLiteral("Anime adaptation")}};
    for (const auto &entry : fields)
        field(series, entry.second, obj.value(entry.first));
    metadata.sections.append(series);

    MetadataNode titles{QStringLiteral("Titles"), {}, {}};
    for (const auto &value : boundedArray(obj.value(QStringLiteral("titles")))) {
        const auto title = value.toObject();
        const QString language = title.value(QStringLiteral("language")).toString(QStringLiteral("Unknown language"));
        auto node = Metadata::text(language, {title.value(QStringLiteral("title")).toString()});
        field(node, QStringLiteral("Traits"), title.value(QStringLiteral("traits")));
        field(node, QStringLiteral("Primary"), title.value(QStringLiteral("is_primary")));
        field(node, QStringLiteral("Note"), title.value(QStringLiteral("note")));
        if (!node.values.isEmpty())
            titles.children.append(node);
    }
    metadata.sections.append(titles);
    MetadataNode publishers{QStringLiteral("Publishers"), {}, {}};
    for (const auto &value : boundedArray(obj.value(QStringLiteral("publishers")))) {
        const auto publisher = value.toObject();
        MetadataNode node{publisher.value(QStringLiteral("name")).toString(QStringLiteral("Publisher")), {}, {}};
        field(node, QStringLiteral("Language"), publisher.value(QStringLiteral("language")));
        field(node, QStringLiteral("Note"), publisher.value(QStringLiteral("note")));
        link(node, QStringLiteral("Publisher page"), publisher.value(QStringLiteral("canonical_url")).toString());
        publishers.children.append(node);
    }
    metadata.sections.append(publishers);

    MetadataNode tags{QStringLiteral("Tags"), {}, {}};
    QMap<int, QJsonObject> tagsById;
    for (const auto &value : boundedArray(obj.value(QStringLiteral("tags")))) {
        const auto tag = value.toObject();
        if (tag.value(QStringLiteral("id")).toInt() > 0)
            tagsById.insert(tag.value(QStringLiteral("id")).toInt(), tag);
    }
    for (const auto &value : boundedArray(obj.value(QStringLiteral("tags")))) {
        const auto tag = value.toObject();
        QStringList path = tag.value(QStringLiteral("name_path")).toString().split(QStringLiteral(" > "), Qt::SkipEmptyParts);
        if (path.isEmpty()) {
            // Older/missing paths: follow available parents, with cycle/depth guards.
            QJsonObject current = tag;
            QSet<int> visited;
            while (!current.isEmpty() && visited.size() < 12) {
                const int id = current.value(QStringLiteral("id")).toInt();
                if (visited.contains(id))
                    break;
                visited.insert(id);
                const QString name = current.value(QStringLiteral("name")).toString();
                if (!name.isEmpty())
                    path.prepend(name);
                current = tagsById.value(current.value(QStringLiteral("parent_id")).toInt());
            }
        }
        if (path.isEmpty() || path.size() > 12)
            continue;
        MetadataNode *parent =
            &category(tags, tag.value(QStringLiteral("is_genre")).toBool() ? QStringLiteral("Genres") : QStringLiteral("Taxonomy"));
        for (const auto &part : path)
            parent = &category(*parent, part);
        field(*parent, QStringLiteral("Description"), tag.value(QStringLiteral("description")));
        field(*parent, QStringLiteral("Relevance"), tag.value(QStringLiteral("weight")));
        field(*parent, QStringLiteral("Spoiler"), tag.value(QStringLiteral("is_spoiler")));
        field(*parent, QStringLiteral("Content rating"), tag.value(QStringLiteral("content_rating")));
        field(*parent, QStringLiteral("Tag ID"), tag.value(QStringLiteral("id")));
        field(*parent, QStringLiteral("Parent ID"), tag.value(QStringLiteral("parent_id")));
        field(*parent, QStringLiteral("Level"), tag.value(QStringLiteral("level")));
        field(*parent, QStringLiteral("Assigned explicitly (is_explicit)"), tag.value(QStringLiteral("is_explicit")));
        field(*parent, QStringLiteral("Merged into tag"), tag.value(QStringLiteral("merged_with")));
        field(*parent, QStringLiteral("Implied by tags"), tag.value(QStringLiteral("implied_by_tag_ids")));
    }
    metadata.sections.append(tags);
    MetadataNode references{QStringLiteral("Sources and links"), {}, {}};
    field(references, QStringLiteral("Upstream source IDs and ratings"), obj.value(QStringLiteral("source")));
    for (const auto &value : boundedArray(obj.value(QStringLiteral("links")))) {
        const auto reference = value.toObject();
        auto &group = category(references, reference.value(QStringLiteral("type")).toString(QStringLiteral("Other")));
        link(group, reference.value(QStringLiteral("name_display")).toString(reference.value(QStringLiteral("name")).toString()),
             reference.value(QStringLiteral("url")).toString());
    }
    field(references, QStringLiteral("Related series"), obj.value(QStringLiteral("relationships")));
    const auto cover = obj.value(QStringLiteral("cover")).toObject();
    link(references, QStringLiteral("Cover image"),
         cover.value(QStringLiteral("x250")).toString(cover.value(QStringLiteral("raw")).toString()), MetadataValue::Kind::Image);
    metadata.sections.append(references);
    metadata.attribution = {QStringLiteral(
        "Data provided by MangaBaka and the upstream providers identified above. Third-party data remains subject to their terms.")};
    if (hasOversizedList(obj) || hasOversizedList(obj.value(QStringLiteral("attributes")).toObject()))
        metadata.attribution.append(QStringLiteral("Additional metadata omitted: lists are limited to 512 entries."));
    return metadata;
}

QStringList ProviderMangaBaka::supportedFormats()
{
    return {QStringLiteral("manga"),  QStringLiteral("novel"), QStringLiteral("manhwa"),
            QStringLiteral("manhua"), QStringLiteral("oel"),   QStringLiteral("other")};
}
QString ProviderMangaBaka::formatLabel(const QString &type)
{
    const QStringList labels = {QStringLiteral("Manga"),  QStringLiteral("Novel"), QStringLiteral("Manhwa"),
                                QStringLiteral("Manhua"), QStringLiteral("OEL"),   QStringLiteral("Other")};
    const int index = supportedFormats().indexOf(type);
    return index < 0 ? type : labels.at(index);
}
QWidget *ProviderMangaBaka::createOptionsWidget(QWidget *parent) const
{
    auto *widget = new QGroupBox(QStringLiteral("MangaBaka formats"), parent);
    auto *layout = new QVBoxLayout(widget);
    auto *all = new QCheckBox(QStringLiteral("All formats"), widget);
    all->setObjectName(QStringLiteral("allFormats"));
    all->setChecked(m_formats.isEmpty());
    layout->addWidget(all);
    for (const auto &type : supportedFormats()) {
        auto *check = new QCheckBox(formatLabel(type), widget);
        check->setObjectName(QStringLiteral("format_") + type);
        check->setChecked(m_formats.contains(type));
        check->setEnabled(!all->isChecked());
        connect(all, &QCheckBox::toggled, check, [check](bool checked) { check->setEnabled(!checked); });
        layout->addWidget(check);
    }
    auto *hint =
        new QLabel(QStringLiteral("Applies to new MangaBaka searches only. Selecting no specific formats also means All formats."), widget);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    return widget;
}
void ProviderMangaBaka::saveOptions(QWidget *widget)
{
    auto *all = widget ? widget->findChild<QCheckBox *>(QStringLiteral("allFormats")) : nullptr;
    if (!all)
        return;
    m_formats.clear();
    if (!all->isChecked())
        for (const auto &type : supportedFormats()) {
            auto *check = widget->findChild<QCheckBox *>(QStringLiteral("format_") + type);
            if (check && check->isChecked())
                m_formats.append(type);
        }
    KConfigGroup config(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Provider-mangabaka"));
    config.writeEntry(QStringLiteral("Formats"), m_formats);
    config.sync();
}
