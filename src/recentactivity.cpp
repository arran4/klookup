#include "recentactivity.h"
#include "clipboardinput.h"
#include "sourceurl.h"
#include <KConfigGroup>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

namespace
{
QString bounded(const QString &value, int length)
{
    QString result;
    for (auto character : value.left(length)) {
        if (character.category() == QChar::Other_Control)
            result += QLatin1Char(' ');
        else
            result += character;
    }
    return result.simplified();
}
bool safeUrl(const QUrl &url)
{
    if (validatedSourceUrl(url.toString(QUrl::FullyEncoded)).isEmpty())
        return false;
    const QStringList secrets = {QStringLiteral("access_token"),  QStringLiteral("refresh_token"), QStringLiteral("token"),
                                 QStringLiteral("api_key"),       QStringLiteral("apikey"),        QStringLiteral("password"),
                                 QStringLiteral("authorization"), QStringLiteral("code"),          QStringLiteral("state"),
                                 QStringLiteral("secret"),        QStringLiteral("client_secret")};
    for (const auto &parameters : {QUrlQuery(url), QUrlQuery(url.fragment())})
        for (const auto &parameter : parameters.queryItems(QUrl::FullyDecoded))
            if (secrets.contains(parameter.first.toLower()))
                return false;
    return true;
}
bool safeQuery(const QString &text)
{
    const QUrl url(text, QUrl::StrictMode);
    if (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
        return safeUrl(url);
    return !text.isEmpty() && text.size() <= 2048;
}
} // namespace
RecentActivity::RecentActivity(KSharedConfig::Ptr config, QObject *parent) : QObject(parent), m_config(config)
{
    reloadPolicy();
    const auto saved = KConfigGroup(m_config, QStringLiteral("RecentActivity")).readEntry(QStringLiteral("Items"), QStringList());
    for (int i = 0; i < saved.size() && i < 50; ++i) {
        if (saved.at(i).size() > 32768)
            continue;
        const auto object = QJsonDocument::fromJson(saved.at(i).toUtf8()).object();
        const int kind = object.value(QStringLiteral("kind")).toInt(-1);
        if (kind < 0 || kind > 2 || !object.value(QStringLiteral("clipboard")).isBool())
            continue;
        RecentItem item;
        item.kind = RecentItem::Kind(kind);
        item.key = object.value(QStringLiteral("key")).toString();
        item.title = object.value(QStringLiteral("title")).toString();
        item.query = object.value(QStringLiteral("query")).toString();
        item.snippet = object.value(QStringLiteral("snippet")).toString();
        item.source = object.value(QStringLiteral("source")).toString();
        item.resultId = object.value(QStringLiteral("resultId")).toString();
        item.url = QUrl(object.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
        item.timestamp = QDateTime::fromString(object.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
        item.clipboard = object.value(QStringLiteral("clipboard")).toBool();
        if ((!item.clipboard || m_persistClipboard) && permitted(item))
            m_items.append(item);
    }
    m_loaded = true;
    expire();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { expire(); });
    timer->start(60000);
}
void RecentActivity::reloadPolicy()
{
    const KConfigGroup policy(m_config, QStringLiteral("General"));
    m_shown = policy.readEntry(QStringLiteral("ShowRecentActivity"), true);
    m_recordClipboard = policy.readEntry(QStringLiteral("RecordClipboardActivity"), false);
    m_persistClipboard = m_recordClipboard && policy.readEntry(QStringLiteral("PersistClipboardActivity"), false);
    if (!m_recordClipboard)
        m_items.removeIf([](const RecentItem &item) { return item.clipboard; });
    if (m_loaded)
        save(); // Remove disallowed persisted clipboard data immediately.
    emit changed();
}
bool RecentActivity::permitted(const RecentItem &item) const
{
    if (item.key.isEmpty() || item.key.size() > 64 || item.title.isEmpty() || item.title.size() > 2048 || item.query.size() > 2048 ||
        item.source.size() > 256 || item.resultId.size() > 512 || item.snippet.size() > 256 ||
        item.url.toString(QUrl::FullyEncoded).size() > 4096 || !item.timestamp.isValid() ||
        item.timestamp > QDateTime::currentDateTimeUtc().addSecs(300) || (item.clipboard && !m_recordClipboard))
        return false;
    if (item.kind == RecentItem::Kind::Captured && !item.clipboard)
        return false;
    if (!item.url.isEmpty() && !safeUrl(item.url))
        return false;
    return item.kind == RecentItem::Kind::Viewed || safeQuery(item.query);
}
void RecentActivity::append(RecentItem item)
{
    item.title = bounded(item.title, 2048);
    item.snippet = bounded(item.snippet, 256);
    item.source = bounded(item.source, 256);
    item.resultId = bounded(item.resultId, 512);
    item.timestamp = QDateTime::currentDateTimeUtc();
    item.key = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!permitted(item))
        return;
    for (int i = 0; i < m_items.size(); ++i) {
        const auto &old = m_items.at(i);
        const bool same = item.kind == old.kind && item.clipboard == old.clipboard && item.source == old.source &&
                          (item.kind == RecentItem::Kind::Viewed ? (item.resultId == old.resultId && item.url == old.url &&
                                                                    (!item.resultId.isEmpty() || item.title == old.title))
                                                                 : item.query == old.query);
        if (same) {
            item.key = old.key;
            m_items.removeAt(i);
            break;
        }
    }
    m_items.prepend(item);
    while (m_items.size() > 50)
        m_items.removeLast();
    expire();
}
void RecentActivity::recordSearch(const QString &query, bool clipboard)
{
    RecentItem item;
    if (query.size() > 2048)
        return;
    item.query = query.trimmed();
    item.title = query;
    item.source = clipboard ? QStringLiteral("Clipboard") : QStringLiteral("Explicit lookup");
    item.clipboard = clipboard;
    append(item);
}
void RecentActivity::recordCapture(const QString &query)
{
    if (!ClipboardInput::classifyText(query).permitsAutomaticCapture())
        return;
    RecentItem item;
    item.kind = RecentItem::Kind::Captured;
    if (query.size() > 2048)
        return;
    item.query = query.trimmed();
    item.title = query;
    item.source = QStringLiteral("Clipboard");
    item.clipboard = true;
    append(item);
}
void RecentActivity::recordViewed(const QString &providerId, const SearchResult &result)
{
    RecentItem item;
    item.kind = RecentItem::Kind::Viewed;
    item.title = result.title;
    item.snippet = result.description;
    item.source = providerId;
    item.resultId = result.id;
    item.url = QUrl(result.url, QUrl::StrictMode);
    append(item);
}
void RecentActivity::remove(const QString &key)
{
    m_items.removeIf([&](const RecentItem &item) { return item.key == key; });
    save();
    emit changed();
}
void RecentActivity::clear()
{
    m_items.clear();
    save();
    emit changed();
}
void RecentActivity::expire(const QDateTime &now)
{
    m_items.removeIf([&](const RecentItem &item) { return item.timestamp < now.addDays(item.clipboard ? -1 : -30); });
    save();
    emit changed();
}
void RecentActivity::save()
{
    QStringList saved;
    for (const auto &item : m_items) {
        if (item.clipboard && !m_persistClipboard)
            continue;
        const QJsonObject object{{QStringLiteral("key"), item.key},
                                 {QStringLiteral("kind"), int(item.kind)},
                                 {QStringLiteral("title"), item.title},
                                 {QStringLiteral("query"), item.query},
                                 {QStringLiteral("snippet"), item.snippet},
                                 {QStringLiteral("source"), item.source},
                                 {QStringLiteral("resultId"), item.resultId},
                                 {QStringLiteral("url"), item.url.toString(QUrl::FullyEncoded)},
                                 {QStringLiteral("timestamp"), item.timestamp.toString(Qt::ISODateWithMs)},
                                 {QStringLiteral("clipboard"), item.clipboard}};
        saved.append(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    }
    KConfigGroup group(m_config, QStringLiteral("RecentActivity"));
    group.deleteGroup();
    if (!saved.isEmpty())
        group.writeEntry(QStringLiteral("Items"), saved);
    m_config->sync();
}
