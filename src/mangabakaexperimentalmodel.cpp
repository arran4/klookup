// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mangabakaexperimentalmodel.h"
#include "languagepreferences.h"
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QBuffer>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QImage>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace
{

constexpr int kMaxArtworkBytes = 4 * 1024 * 1024;
constexpr int kMaxArtworkDimension = 2048;
constexpr int kArtworkTimeoutMs = 15000;

QHash<QString, MangaBakaExperimentalModel::DisplayPreferences> &sessionPerResultPrefs()
{
    static QHash<QString, MangaBakaExperimentalModel::DisplayPreferences> map;
    return map;
}

MangaBakaExperimentalModel::DisplayPreferences readPrefsGroup(const KConfigGroup &group,
                                                            const MangaBakaExperimentalModel::DisplayPreferences &fallback)
{
    MangaBakaExperimentalModel::DisplayPreferences prefs = fallback;
    prefs.expandAlternateTitles = group.readEntry(QStringLiteral("ExpandAlternateTitles"), fallback.expandAlternateTitles);
    prefs.showLowerTagTiers = group.readEntry(QStringLiteral("ShowLowerTagTiers"), fallback.showLowerTagTiers);
    prefs.showImpliedTags = group.readEntry(QStringLiteral("ShowImpliedTags"), fallback.showImpliedTags);
    prefs.revealSpoilers = group.readEntry(QStringLiteral("RevealSpoilers"), fallback.revealSpoilers);
    prefs.autoLoadArtwork = group.readEntry(QStringLiteral("AutoLoadArtwork"), fallback.autoLoadArtwork);
    prefs.compactLayout = group.readEntry(QStringLiteral("CompactLayout"), fallback.compactLayout);
    return prefs;
}

void writePrefsGroup(KConfigGroup &group, const MangaBakaExperimentalModel::DisplayPreferences &prefs)
{
    group.writeEntry(QStringLiteral("ExpandAlternateTitles"), prefs.expandAlternateTitles);
    group.writeEntry(QStringLiteral("ShowLowerTagTiers"), prefs.showLowerTagTiers);
    group.writeEntry(QStringLiteral("ShowImpliedTags"), prefs.showImpliedTags);
    group.writeEntry(QStringLiteral("RevealSpoilers"), prefs.revealSpoilers);
    group.writeEntry(QStringLiteral("AutoLoadArtwork"), prefs.autoLoadArtwork);
    group.writeEntry(QStringLiteral("CompactLayout"), prefs.compactLayout);
    group.sync();
}

} // namespace

MangaBakaExperimentalModel::MangaBakaExperimentalModel(QObject *parent)
    : QObject(parent)
    , m_palette(QGuiApplication::palette())
    , m_nam(new QNetworkAccessManager(this))
{
    updatePreferenceScopeNote();
}

MangaBakaExperimentalModel::~MangaBakaExperimentalModel()
{
    abortArtworkReply();
}

bool MangaBakaExperimentalModel::isExperimentalResult(const QString &providerId, const SearchResult &result)
{
    return providerId == QLatin1String("mangabaka_experimental") ||
           result.additionalData.contains(QStringLiteral("mangabaka_experimental_v2"));
}

MangaBakaExperimentalModel::DisplayPreferences MangaBakaExperimentalModel::loadEffectivePreferences(const QString &providerId)
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    const KConfigGroup globalGroup(config, QStringLiteral("DisplayPreferences"));
    DisplayPreferences prefs = readPrefsGroup(globalGroup, DisplayPreferences{});
    if (!providerId.isEmpty()) {
        const KConfigGroup provGroup(config, QStringLiteral("DisplayPreferences-") + providerId);
        prefs = readPrefsGroup(provGroup, prefs);
    }
    return prefs;
}

void MangaBakaExperimentalModel::clearSessionOverrides()
{
    sessionPerResultPrefs().clear();
}

int MangaBakaExperimentalModel::weightTierRank(const QString &weightText)
{
    const QString lower = weightText.trimmed().toLower();
    if (lower.isEmpty())
        return 1;
    if (lower == QLatin1String("defining") || lower == QLatin1String("core"))
        return 4;
    if (lower == QLatin1String("major") || lower == QLatin1String("high"))
        return 3;
    if (lower == QLatin1String("moderate") || lower == QLatin1String("medium") || lower == QLatin1String("minor"))
        return 2;
    bool ok = false;
    const double numeric = lower.toDouble(&ok);
    if (ok) {
        if (numeric >= 80.0)
            return 4;
        if (numeric >= 60.0)
            return 3;
        if (numeric >= 30.0)
            return 2;
        return 1;
    }
    return 1;
}

void MangaBakaExperimentalModel::setPalette(const QPalette &palette)
{
    m_palette = palette;
    emit themeChanged();
}

void MangaBakaExperimentalModel::setNetworkAccessManager(QNetworkAccessManager *nam)
{
    if (nam && nam != m_nam) {
        abortArtworkReply();
        m_nam = nam;
    }
}

void MangaBakaExperimentalModel::clear()
{
    abortArtworkReply();
    m_result = {};
    m_metadata = {};
    m_content = {};
    m_providerName.clear();
    m_rawSources.clear();
    m_preferredTitle.clear();
    m_originalTitle.clear();
    m_originalLanguage.clear();
    m_formatLabel.clear();
    m_status.clear();
    m_yearText.clear();
    m_publishedText.clear();
    m_contentRating.clear();
    m_licensingText.clear();
    m_volumeChapterText.clear();
    m_ratingText.clear();
    m_ratingCount = 0;
    m_synopsis.clear();
    m_canonicalUrl.clear();
    m_coverUrl.clear();
    m_artworkState = QStringLiteral("none");
    m_artworkSource.clear();
    m_artworkError.clear();
    m_authors.clear();
    m_artists.clear();
    m_publishers.clear();
    m_sourceRatings.clear();
    m_externalLinks.clear();
    m_allTitles.clear();
    m_allTags.clear();
    m_visibleTags.clear();
    m_hiddenLowerTierCount = 0;
    m_hiddenImpliedCount = 0;
    m_spoilerTagCount = 0;
    m_selectedTagName.clear();
    m_selectedTagPath.clear();
    m_selectedTagDescription.clear();
    m_statusFeedback.clear();
    emit dataChanged();
    emit artworkChanged();
    emit tagsChanged();
    emit tagSelectionChanged();
    emit feedbackChanged();
}

void MangaBakaExperimentalModel::setResult(const SearchResult &result,
                                           const ResultMetadata &metadata,
                                           const ResultContent &content,
                                           const QString &providerName)
{
    abortArtworkReply();
    m_result = result;
    m_metadata = metadata;
    m_content = content;
    m_providerName = providerName.isEmpty() ? i18n("MangaBaka Experimental") : providerName;
    m_rawSources = Content::extractRawSources(result, metadata);
    m_selectedTagName.clear();
    m_selectedTagPath.clear();
    m_selectedTagDescription.clear();
    m_statusFeedback.clear();

    const QVariantMap payload = result.additionalData.value(QStringLiteral("mangabaka_experimental_v2")).toMap();

    m_preferredTitle = result.title;
    m_originalTitle = result.additionalData.value(QStringLiteral("Original title")).toString();
    m_originalLanguage = payload.value(QStringLiteral("original_language"),
                                       result.additionalData.value(QStringLiteral("Original title language"))).toString();
    m_formatLabel = result.type.isEmpty() ? payload.value(QStringLiteral("type")).toString() : result.type;
    m_status = payload.value(QStringLiteral("status"), result.additionalData.value(QStringLiteral("status"))).toString();

    const int yearInt = payload.value(QStringLiteral("year"), result.additionalData.value(QStringLiteral("year"))).toInt();
    m_yearText = yearInt > 0 ? QString::number(yearInt) : QString();

    const QVariantMap published = payload.value(QStringLiteral("published")).toMap();
    const QString startDate = published.value(QStringLiteral("start_date")).toString();
    const QString endDate = published.value(QStringLiteral("end_date")).toString();
    if (!startDate.isEmpty() && !endDate.isEmpty())
        m_publishedText = QStringLiteral("%1 – %2").arg(startDate, endDate);
    else if (!startDate.isEmpty())
        m_publishedText = QStringLiteral("%1 – present").arg(startDate);
    else
        m_publishedText.clear();

    m_contentRating = payload.value(QStringLiteral("content_rating")).toString();
    if (payload.contains(QStringLiteral("is_licensed")))
        m_licensingText = payload.value(QStringLiteral("is_licensed")).toBool() ? i18n("Licensed") : i18n("Unlicensed / unknown");
    else
        m_licensingText.clear();

    QStringList volChapParts;
    const QString finalVol = payload.value(QStringLiteral("final_volume")).toString();
    const QString totalCh = payload.value(QStringLiteral("total_chapters")).toString();
    if (!finalVol.isEmpty())
        volChapParts.append(i18n("%1 vols", finalVol));
    if (!totalCh.isEmpty())
        volChapParts.append(i18n("%1 chs", totalCh));
    m_volumeChapterText = volChapParts.join(QStringLiteral(" · "));

    if (payload.contains(QStringLiteral("rating"))) {
        const double r = payload.value(QStringLiteral("rating")).toDouble();
        m_ratingText = QStringLiteral("%1 / 100").arg(QString::number(r, 'f', 1));
    } else {
        m_ratingText.clear();
    }
    m_ratingCount = payload.value(QStringLiteral("rating_count")).toInt();

    // Always render synopsis strictly as plain text.
    const QString desc = !content.description.isEmpty() ? content.description : payload.value(QStringLiteral("description"), result.description).toString();
    m_synopsis = desc;

    const QUrl safeCan = Metadata::safeWebUrl(payload.value(QStringLiteral("canonical_url"), result.url).toString());
    m_canonicalUrl = safeCan.toString();

    m_authors = payload.value(QStringLiteral("authors")).toStringList();
    m_artists = payload.value(QStringLiteral("artists")).toStringList();
    m_publishers = payload.value(QStringLiteral("publishers")).toList();
    m_sourceRatings = payload.value(QStringLiteral("sources")).toList();
    m_externalLinks = payload.value(QStringLiteral("links")).toList();

    // Titles list (#135, #145)
    m_allTitles.clear();
    const QVariantList rawTitles = payload.value(QStringLiteral("titles")).toList();
    for (const QVariant &tv : rawTitles) {
        const QVariantMap tm = tv.toMap();
        const QString text = tm.value(QStringLiteral("title")).toString();
        if (text.isEmpty())
            continue;
        const QString lang = tm.value(QStringLiteral("language")).toString();
        const QStringList traits = tm.value(QStringLiteral("traits")).toStringList();
        const bool isOriginal = !m_originalLanguage.isEmpty() &&
                                Languages::canonicalTag(lang) == Languages::canonicalTag(m_originalLanguage);
        if (m_originalTitle.isEmpty() && isOriginal && text != m_preferredTitle)
            m_originalTitle = text;
        QVariantMap row;
        row.insert(QStringLiteral("title"), text);
        row.insert(QStringLiteral("language"), lang.isEmpty() ? QStringLiteral("?") : lang);
        row.insert(QStringLiteral("traitsText"), traits.join(QStringLiteral(", ")));
        row.insert(QStringLiteral("isPrimary"), tm.value(QStringLiteral("is_primary")).toBool());
        row.insert(QStringLiteral("isOriginal"), isOriginal);
        row.insert(QStringLiteral("note"), tm.value(QStringLiteral("note")).toString());
        m_allTitles.append(row);
    }
    if (m_allTitles.isEmpty()) {
        const auto fallbackTitles = Languages::fromData(result.additionalData.value(QStringLiteral("localizedTitles")));
        for (const auto &ft : fallbackTitles) {
            QVariantMap row;
            row.insert(QStringLiteral("title"), ft.text);
            row.insert(QStringLiteral("language"), ft.language.isEmpty() ? QStringLiteral("?") : ft.language);
            row.insert(QStringLiteral("traitsText"), QString());
            row.insert(QStringLiteral("isPrimary"), ft.text == m_preferredTitle);
            row.insert(QStringLiteral("isOriginal"), ft.original);
            row.insert(QStringLiteral("note"), QString());
            m_allTitles.append(row);
        }
    }

    // Tags hierarchy, weights, spoilers & explicit/implied (#135)
    m_allTags.clear();
    m_spoilerTagCount = 0;
    const QVariantList rawTags = payload.value(QStringLiteral("tags")).toList();
    for (const QVariant &tv : rawTags) {
        const QVariantMap tm = tv.toMap();
        const QString tagName = tm.value(QStringLiteral("name")).toString();
        if (tagName.isEmpty())
            continue;
        ParsedTag pt;
        pt.id = tm.value(QStringLiteral("id")).toInt();
        pt.name = tagName;
        pt.namePath = tm.value(QStringLiteral("name_path")).toString();
        pt.weightLabel = tm.value(QStringLiteral("weight")).toString();
        pt.weightRank = weightTierRank(pt.weightLabel);
        pt.isGenre = tm.value(QStringLiteral("is_genre")).toBool();
        pt.isSpoiler = tm.value(QStringLiteral("is_spoiler")).toBool();
        pt.isExplicit = tm.value(QStringLiteral("is_explicit"), true).toBool();
        const QStringList implied = tm.value(QStringLiteral("implied_by_tag_ids")).toStringList();
        if (!implied.isEmpty()) {
            pt.impliedByText = implied.join(QStringLiteral(", "));
            if (!tm.contains(QStringLiteral("is_explicit")))
                pt.isExplicit = false;
        }
        pt.contentRating = tm.value(QStringLiteral("content_rating")).toString();
        pt.description = tm.value(QStringLiteral("description")).toString();
        if (pt.isSpoiler)
            ++m_spoilerTagCount;
        m_allTags.append(pt);
    }
    std::stable_sort(m_allTags.begin(), m_allTags.end(), [](const ParsedTag &a, const ParsedTag &b) {
        if (a.weightRank != b.weightRank)
            return a.weightRank > b.weightRank;
        if (a.isGenre != b.isGenre)
            return a.isGenre && !b.isGenre;
        return a.isExplicit && !b.isExplicit;
    });

    // Cover artwork URL (#146): never automatically fetched unless user explicitly opted in
    const QUrl safeCover = Metadata::safeWebUrl(payload.value(QStringLiteral("cover_url")).toString());
    m_coverUrl = safeCover.toString();
    m_artworkSource.clear();
    m_artworkError.clear();
    m_artworkState = m_coverUrl.isEmpty() ? QStringLiteral("none") : QStringLiteral("hidden");

    // Load effective display preferences (#144)
    const QString resultKey = result.id;
    const auto &overrides = sessionPerResultPrefs();
    if (!resultKey.isEmpty() && overrides.contains(resultKey)) {
        m_prefs = overrides.value(resultKey);
        m_hasPerResultOverride = true;
    } else {
        m_prefs = loadEffectivePreferences(QStringLiteral("mangabaka_experimental"));
        m_hasPerResultOverride = false;
    }
    updatePreferenceScopeNote();
    rebuildVisibleTags();

    emit dataChanged();
    emit preferencesChanged();
    emit artworkChanged();
    emit tagSelectionChanged();
    emit feedbackChanged();

    if (m_prefs.autoLoadArtwork && !m_coverUrl.isEmpty())
        loadArtwork();
}

void MangaBakaExperimentalModel::rebuildVisibleTags()
{
    m_visibleTags.clear();
    m_hiddenLowerTierCount = 0;
    m_hiddenImpliedCount = 0;

    // Determine the top 2 weight tiers among non-spoiler tags (#135)
    QList<int> distinctNonSpoilerRanks;
    for (const auto &tag : m_allTags) {
        if (tag.isSpoiler)
            continue;
        if (!distinctNonSpoilerRanks.contains(tag.weightRank))
            distinctNonSpoilerRanks.append(tag.weightRank);
    }
    std::sort(distinctNonSpoilerRanks.begin(), distinctNonSpoilerRanks.end(), std::greater<int>());
    int minVisibleRank = 1;
    if (distinctNonSpoilerRanks.size() > 2)
        minVisibleRank = distinctNonSpoilerRanks.at(1); // Top 2 non-spoiler significance tiers

    const QString needle = m_tagFilterText.trimmed().toLower();
    for (int i = 0; i < m_allTags.size(); ++i) {
        const auto &tag = m_allTags.at(i);
        if (m_tagFilterMode == QLatin1String("genres") && !tag.isGenre)
            continue;
        if (m_tagFilterMode == QLatin1String("explicit") && !tag.isExplicit)
            continue;
        if (!tag.isExplicit && !m_prefs.showImpliedTags) {
            ++m_hiddenImpliedCount;
            continue;
        }
        if (!m_prefs.showLowerTagTiers && tag.weightRank < minVisibleRank && needle.isEmpty()) {
            ++m_hiddenLowerTierCount;
            continue;
        }

        const bool revealed = !tag.isSpoiler || m_prefs.revealSpoilers || tag.userRevealed || tag.peekRevealed;
        if (!needle.isEmpty()) {
            // Do not leak hidden spoiler tag names via filter search until revealed!
            if (!revealed)
                continue;
            const bool matches = tag.name.toLower().contains(needle) || tag.namePath.toLower().contains(needle);
            if (!matches)
                continue;
        }

        QVariantMap vm;
        vm.insert(QStringLiteral("index"), i);
        vm.insert(QStringLiteral("id"), tag.id);
        vm.insert(QStringLiteral("name"), tag.name);
        vm.insert(QStringLiteral("displayText"), revealed ? tag.name : i18n("Spoiler — reveal"));
        vm.insert(QStringLiteral("namePath"), tag.namePath);
        vm.insert(QStringLiteral("weightLabel"), tag.weightLabel.isEmpty() ? QStringLiteral("subtle") : tag.weightLabel);
        vm.insert(QStringLiteral("weightRank"), tag.weightRank);
        vm.insert(QStringLiteral("isGenre"), tag.isGenre);
        vm.insert(QStringLiteral("isSpoiler"), tag.isSpoiler);
        vm.insert(QStringLiteral("isRevealed"), revealed);
        vm.insert(QStringLiteral("isExplicit"), tag.isExplicit);
        vm.insert(QStringLiteral("impliedByText"), tag.impliedByText);
        vm.insert(QStringLiteral("description"), tag.description);
        m_visibleTags.append(vm);
    }

    emit tagsChanged();
}

void MangaBakaExperimentalModel::toggleSpoilerTag(int index)
{
    if (index < 0 || index >= m_allTags.size())
        return;
    if (!m_allTags[index].isSpoiler)
        return;
    m_allTags[index].userRevealed = !m_allTags[index].userRevealed;
    rebuildVisibleTags();
}

void MangaBakaExperimentalModel::peekSpoilerTag(int index, bool active)
{
    if (index < 0 || index >= m_allTags.size())
        return;
    if (!m_allTags[index].isSpoiler)
        return;
    if (m_allTags[index].peekRevealed == active)
        return;
    m_allTags[index].peekRevealed = active;
    rebuildVisibleTags();
}

void MangaBakaExperimentalModel::selectTag(int index)
{
    if (index < 0 || index >= m_allTags.size())
        return;
    const auto &tag = m_allTags.at(index);
    const bool revealed = !tag.isSpoiler || m_prefs.revealSpoilers || tag.userRevealed || tag.peekRevealed;
    if (!revealed) {
        m_selectedTagName = i18n("Hidden spoiler tag");
        m_selectedTagPath.clear();
        m_selectedTagDescription = i18n("Reveal this spoiler tag by clicking or pressing Space to inspect its taxonomy path.");
    } else {
        m_selectedTagName = tag.name;
        m_selectedTagPath = tag.namePath;
        QStringList details;
        if (!tag.weightLabel.isEmpty())
            details.append(i18n("Significance: %1", tag.weightLabel));
        details.append(tag.isExplicit ? i18n("Assigned explicitly") : i18n("Implied tag%1", tag.impliedByText.isEmpty() ? QString() : QStringLiteral(" (by %1)").arg(tag.impliedByText)));
        if (!tag.description.isEmpty())
            details.append(tag.description);
        m_selectedTagDescription = details.join(QStringLiteral(" · "));
    }
    emit tagSelectionChanged();
}

void MangaBakaExperimentalModel::abortArtworkReply()
{
    if (m_artworkReply) {
        disconnect(m_artworkReply, nullptr, this, nullptr);
        m_artworkReply->abort();
        m_artworkReply->deleteLater();
        m_artworkReply.clear();
    }
}

void MangaBakaExperimentalModel::loadArtwork()
{
    if (m_coverUrl.isEmpty() || !m_nam)
        return;
    const QUrl url = Metadata::safeWebUrl(m_coverUrl);
    if (url.isEmpty()) {
        m_artworkState = QStringLiteral("error");
        m_artworkError = i18n("Blocked unsafe cover URL.");
        emit artworkChanged();
        return;
    }

    abortArtworkReply();
    m_artworkState = QStringLiteral("loading");
    m_artworkError.clear();
    emit artworkChanged();

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(3);
    req.setRawHeader("User-Agent", "KLookup/0.1 (MangaBaka Experimental Artwork Loader)");
    QNetworkReply *reply = m_nam->get(req);
    m_artworkReply = reply;

    auto *timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeoutTimer->start(kArtworkTimeoutMs);

    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64 total) {
        if (received > kMaxArtworkBytes || total > kMaxArtworkBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_artworkReply != reply) {
            reply->deleteLater();
            return;
        }
        m_artworkReply.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            m_artworkState = QStringLiteral("error");
            m_artworkError = reply->errorString().isEmpty() ? i18n("Artwork request failed (HTTP %1).", status) : reply->errorString();
            reply->deleteLater();
            emit artworkChanged();
            return;
        }
        const QByteArray data = reply->read(kMaxArtworkBytes + 1);
        reply->deleteLater();
        if (!setArtworkImageDataForTesting(data)) {
            emit artworkChanged();
        }
    });
}

bool MangaBakaExperimentalModel::setArtworkImageDataForTesting(const QByteArray &imageBytes)
{
    if (imageBytes.isEmpty() || imageBytes.size() > kMaxArtworkBytes) {
        m_artworkState = QStringLiteral("error");
        m_artworkError = i18n("Cover image exceeded safety size limit or was empty.");
        return false;
    }
    QImage img = QImage::fromData(imageBytes);
    if (img.isNull() || img.width() > kMaxArtworkDimension || img.height() > kMaxArtworkDimension) {
        m_artworkState = QStringLiteral("error");
        m_artworkError = i18n("Invalid or oversized image format.");
        return false;
    }
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    m_artworkSource = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(encoded.toBase64());
    m_artworkState = QStringLiteral("loaded");
    m_artworkError.clear();
    emit artworkChanged();
    return true;
}

void MangaBakaExperimentalModel::clearArtwork()
{
    abortArtworkReply();
    m_artworkSource.clear();
    m_artworkError.clear();
    m_artworkState = m_coverUrl.isEmpty() ? QStringLiteral("none") : QStringLiteral("hidden");
    emit artworkChanged();
}

void MangaBakaExperimentalModel::openCanonicalSource()
{
    const QUrl safe = Metadata::safeWebUrl(m_canonicalUrl);
    if (!safe.isEmpty())
        QDesktopServices::openUrl(safe);
}

void MangaBakaExperimentalModel::openSafeLink(const QString &url)
{
    const QUrl safe = Metadata::safeWebUrl(url);
    if (!safe.isEmpty())
        QDesktopServices::openUrl(safe);
}

void MangaBakaExperimentalModel::openRawData()
{
    if (m_rawSources.isEmpty())
        return;
    Content::showRawSourceDialog(m_rawSources.first(), nullptr);
}

void MangaBakaExperimentalModel::openAllTitlesDialog()
{
    const auto titles = Languages::fromData(m_result.additionalData.value(QStringLiteral("localizedTitles")));
    if (!titles.isEmpty())
        Languages::showAllTitles(titles, nullptr);
}

void MangaBakaExperimentalModel::applyPerResultOverride()
{
    if (!m_result.id.isEmpty())
        sessionPerResultPrefs().insert(m_result.id, m_prefs);
    m_hasPerResultOverride = true;
    updatePreferenceScopeNote();
    emit preferencesChanged();
}

void MangaBakaExperimentalModel::updatePreferenceScopeNote()
{
    m_preferenceScopeNote = m_hasPerResultOverride
                                ? i18n("Scope: This result (session override)")
                                : i18n("Scope: Provider / global default");
}

void MangaBakaExperimentalModel::setExpandAlternateTitles(bool value)
{
    if (m_prefs.expandAlternateTitles == value)
        return;
    m_prefs.expandAlternateTitles = value;
    applyPerResultOverride();
}

void MangaBakaExperimentalModel::setShowLowerTagTiers(bool value)
{
    if (m_prefs.showLowerTagTiers == value)
        return;
    m_prefs.showLowerTagTiers = value;
    applyPerResultOverride();
    rebuildVisibleTags();
}

void MangaBakaExperimentalModel::setShowImpliedTags(bool value)
{
    if (m_prefs.showImpliedTags == value)
        return;
    m_prefs.showImpliedTags = value;
    applyPerResultOverride();
    rebuildVisibleTags();
}

void MangaBakaExperimentalModel::setRevealSpoilers(bool value)
{
    if (m_prefs.revealSpoilers == value)
        return;
    m_prefs.revealSpoilers = value;
    applyPerResultOverride();
    rebuildVisibleTags();
}

void MangaBakaExperimentalModel::setAutoLoadArtwork(bool value)
{
    if (m_prefs.autoLoadArtwork == value)
        return;
    m_prefs.autoLoadArtwork = value;
    applyPerResultOverride();
    if (value && m_artworkState == QLatin1String("hidden"))
        loadArtwork();
}

void MangaBakaExperimentalModel::setCompactLayout(bool value)
{
    if (m_prefs.compactLayout == value)
        return;
    m_prefs.compactLayout = value;
    applyPerResultOverride();
}

void MangaBakaExperimentalModel::setTagFilterText(const QString &value)
{
    if (m_tagFilterText == value)
        return;
    m_tagFilterText = value;
    rebuildVisibleTags();
}

void MangaBakaExperimentalModel::setTagFilterMode(const QString &value)
{
    const QString normalized = (value == QLatin1String("genres") || value == QLatin1String("explicit")) ? value : QStringLiteral("all");
    if (m_tagFilterMode == normalized)
        return;
    m_tagFilterMode = normalized;
    rebuildVisibleTags();
}

void MangaBakaExperimentalModel::saveAsProviderDefault()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup group(config, QStringLiteral("DisplayPreferences-mangabaka_experimental"));
    writePrefsGroup(group, m_prefs);
    if (!m_result.id.isEmpty())
        sessionPerResultPrefs().remove(m_result.id);
    m_hasPerResultOverride = false;
    updatePreferenceScopeNote();
    m_statusFeedback = i18n("Saved as MangaBaka Experimental default.");
    emit preferencesChanged();
    emit feedbackChanged();
}

void MangaBakaExperimentalModel::saveAsGlobalDefault()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup group(config, QStringLiteral("DisplayPreferences"));
    writePrefsGroup(group, m_prefs);
    if (!m_result.id.isEmpty())
        sessionPerResultPrefs().remove(m_result.id);
    m_hasPerResultOverride = false;
    updatePreferenceScopeNote();
    m_statusFeedback = i18n("Saved as global display default.");
    emit preferencesChanged();
    emit feedbackChanged();
}

void MangaBakaExperimentalModel::resetPerResultOverrides()
{
    if (!m_result.id.isEmpty())
        sessionPerResultPrefs().remove(m_result.id);
    m_prefs = loadEffectivePreferences(QStringLiteral("mangabaka_experimental"));
    m_hasPerResultOverride = false;
    updatePreferenceScopeNote();
    m_statusFeedback = i18n("Reverted to provider/global defaults.");
    rebuildVisibleTags();
    emit preferencesChanged();
    emit feedbackChanged();
}

QString MangaBakaExperimentalModel::renderFallbackHtml() const
{
    QString html = QStringLiteral("<html><body>");
    html += QStringLiteral("<h2>%1").arg(m_preferredTitle.toHtmlEscaped());
    if (!m_formatLabel.isEmpty())
        html += QStringLiteral(" <small>(%1)</small>").arg(m_formatLabel.toHtmlEscaped());
    html += QStringLiteral("</h2>");
    if (!m_originalTitle.isEmpty())
        html += QStringLiteral("<p><i>%1</i></p>").arg(m_originalTitle.toHtmlEscaped());
    QStringList facts;
    if (!m_status.isEmpty()) facts.append(m_status.toHtmlEscaped());
    if (!m_ratingText.isEmpty()) facts.append(m_ratingText.toHtmlEscaped());
    if (!m_contentRating.isEmpty()) facts.append(m_contentRating.toHtmlEscaped());
    if (!m_publishedText.isEmpty()) facts.append(m_publishedText.toHtmlEscaped());
    if (!facts.isEmpty())
        html += QStringLiteral("<p><b>%1</b></p>").arg(facts.join(QStringLiteral(" · ")));
    if (!m_synopsis.isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(m_synopsis.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>")));
    if (!m_visibleTags.isEmpty()) {
        QStringList tagLabels;
        for (const QVariant &v : m_visibleTags)
            tagLabels.append(v.toMap().value(QStringLiteral("displayText")).toString().toHtmlEscaped());
        html += QStringLiteral("<p><b>Tags:</b> %1</p>").arg(tagLabels.join(QStringLiteral(", ")));
    }
    html += QStringLiteral("</body></html>");
    return html;
}
