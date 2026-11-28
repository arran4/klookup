// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MANGABAKAEXPERIMENTALMODEL_H
#define MANGABAKAEXPERIMENTALMODEL_H

#include "resultcontent.h"
#include "resultmetadata.h"
#include "searchprovider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPalette>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

class MangaBakaExperimentalModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString providerName READ providerName NOTIFY dataChanged)
    Q_PROPERTY(QString seriesId READ seriesId NOTIFY dataChanged)
    Q_PROPERTY(QString preferredTitle READ preferredTitle NOTIFY dataChanged)
    Q_PROPERTY(QString originalTitle READ originalTitle NOTIFY dataChanged)
    Q_PROPERTY(QString originalLanguage READ originalLanguage NOTIFY dataChanged)
    Q_PROPERTY(QString formatLabel READ formatLabel NOTIFY dataChanged)
    Q_PROPERTY(QString status READ status NOTIFY dataChanged)
    Q_PROPERTY(QString yearText READ yearText NOTIFY dataChanged)
    Q_PROPERTY(QString publishedText READ publishedText NOTIFY dataChanged)
    Q_PROPERTY(QString contentRating READ contentRating NOTIFY dataChanged)
    Q_PROPERTY(QString licensingText READ licensingText NOTIFY dataChanged)
    Q_PROPERTY(QString volumeChapterText READ volumeChapterText NOTIFY dataChanged)
    Q_PROPERTY(QString ratingText READ ratingText NOTIFY dataChanged)
    Q_PROPERTY(int ratingCount READ ratingCount NOTIFY dataChanged)
    Q_PROPERTY(QString synopsis READ synopsis NOTIFY dataChanged)
    Q_PROPERTY(QString canonicalUrl READ canonicalUrl NOTIFY dataChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY dataChanged)
    Q_PROPERTY(QString artworkState READ artworkState NOTIFY artworkChanged)
    Q_PROPERTY(QString artworkSource READ artworkSource NOTIFY artworkChanged)
    Q_PROPERTY(QString artworkError READ artworkError NOTIFY artworkChanged)
    Q_PROPERTY(QStringList authors READ authors NOTIFY dataChanged)
    Q_PROPERTY(QStringList artists READ artists NOTIFY dataChanged)
    Q_PROPERTY(QVariantList publishers READ publishers NOTIFY dataChanged)
    Q_PROPERTY(QVariantList sourceRatings READ sourceRatings NOTIFY dataChanged)
    Q_PROPERTY(QVariantList externalLinks READ externalLinks NOTIFY dataChanged)
    Q_PROPERTY(QVariantList allTitles READ allTitles NOTIFY dataChanged)
    Q_PROPERTY(bool hasMoreTitles READ hasMoreTitles NOTIFY dataChanged)
    Q_PROPERTY(QVariantList visibleTags READ visibleTags NOTIFY tagsChanged)
    Q_PROPERTY(int totalTagCount READ totalTagCount NOTIFY dataChanged)
    Q_PROPERTY(int hiddenLowerTierCount READ hiddenLowerTierCount NOTIFY tagsChanged)
    Q_PROPERTY(int hiddenImpliedCount READ hiddenImpliedCount NOTIFY tagsChanged)
    Q_PROPERTY(int spoilerTagCount READ spoilerTagCount NOTIFY dataChanged)
    Q_PROPERTY(QString selectedTagName READ selectedTagName NOTIFY tagSelectionChanged)
    Q_PROPERTY(QString selectedTagPath READ selectedTagPath NOTIFY tagSelectionChanged)
    Q_PROPERTY(QString selectedTagDescription READ selectedTagDescription NOTIFY tagSelectionChanged)
    Q_PROPERTY(bool hasRawSources READ hasRawSources NOTIFY dataChanged)

    // Interactive filter & display preferences (#144)
    Q_PROPERTY(bool expandAlternateTitles READ expandAlternateTitles WRITE setExpandAlternateTitles NOTIFY preferencesChanged)
    Q_PROPERTY(bool showLowerTagTiers READ showLowerTagTiers WRITE setShowLowerTagTiers NOTIFY preferencesChanged)
    Q_PROPERTY(bool showImpliedTags READ showImpliedTags WRITE setShowImpliedTags NOTIFY preferencesChanged)
    Q_PROPERTY(bool revealSpoilers READ revealSpoilers WRITE setRevealSpoilers NOTIFY preferencesChanged)
    Q_PROPERTY(bool autoLoadArtwork READ autoLoadArtwork WRITE setAutoLoadArtwork NOTIFY preferencesChanged)
    Q_PROPERTY(bool compactLayout READ compactLayout WRITE setCompactLayout NOTIFY preferencesChanged)
    Q_PROPERTY(QString preferenceScopeNote READ preferenceScopeNote NOTIFY preferencesChanged)
    Q_PROPERTY(QString tagFilterText READ tagFilterText WRITE setTagFilterText NOTIFY tagsChanged)
    Q_PROPERTY(QString tagFilterMode READ tagFilterMode WRITE setTagFilterMode NOTIFY tagsChanged)
    Q_PROPERTY(QString statusFeedback READ statusFeedback NOTIFY feedbackChanged)

    // Host theme palette colors so QML blends natively in Light and Dark schemes
    Q_PROPERTY(QString colorWindow READ colorWindow NOTIFY themeChanged)
    Q_PROPERTY(QString colorBase READ colorBase NOTIFY themeChanged)
    Q_PROPERTY(QString colorAlternateBase READ colorAlternateBase NOTIFY themeChanged)
    Q_PROPERTY(QString colorText READ colorText NOTIFY themeChanged)
    Q_PROPERTY(QString colorMutedText READ colorMutedText NOTIFY themeChanged)
    Q_PROPERTY(QString colorButton READ colorButton NOTIFY themeChanged)
    Q_PROPERTY(QString colorButtonText READ colorButtonText NOTIFY themeChanged)
    Q_PROPERTY(QString colorHighlight READ colorHighlight NOTIFY themeChanged)
    Q_PROPERTY(QString colorHighlightedText READ colorHighlightedText NOTIFY themeChanged)
    Q_PROPERTY(QString colorLink READ colorLink NOTIFY themeChanged)
    Q_PROPERTY(QString colorMid READ colorMid NOTIFY themeChanged)

public:
    struct DisplayPreferences {
        bool expandAlternateTitles = false;
        bool showLowerTagTiers = false;
        bool showImpliedTags = true;
        bool revealSpoilers = false;
        bool autoLoadArtwork = false;
        bool compactLayout = false;
    };

    explicit MangaBakaExperimentalModel(QObject *parent = nullptr);
    ~MangaBakaExperimentalModel() override;

    void setResult(const SearchResult &result,
                   const ResultMetadata &metadata = {},
                   const ResultContent &content = {},
                   const QString &providerName = {});
    void clear();
    void setPalette(const QPalette &palette);
    void setNetworkAccessManager(QNetworkAccessManager *nam);

    static bool isExperimentalResult(const QString &providerId, const SearchResult &result);
    static DisplayPreferences loadEffectivePreferences(const QString &providerId);
    static void clearSessionOverrides();

    QString providerName() const { return m_providerName; }
    QString seriesId() const { return m_result.id; }
    QString preferredTitle() const { return m_preferredTitle; }
    QString originalTitle() const { return m_originalTitle; }
    QString originalLanguage() const { return m_originalLanguage; }
    QString formatLabel() const { return m_formatLabel; }
    QString status() const { return m_status; }
    QString yearText() const { return m_yearText; }
    QString publishedText() const { return m_publishedText; }
    QString contentRating() const { return m_contentRating; }
    QString licensingText() const { return m_licensingText; }
    QString volumeChapterText() const { return m_volumeChapterText; }
    QString ratingText() const { return m_ratingText; }
    int ratingCount() const { return m_ratingCount; }
    QString synopsis() const { return m_synopsis; }
    QString canonicalUrl() const { return m_canonicalUrl; }
    QString coverUrl() const { return m_coverUrl; }
    QString artworkState() const { return m_artworkState; }
    QString artworkSource() const { return m_artworkSource; }
    QString artworkError() const { return m_artworkError; }
    QStringList authors() const { return m_authors; }
    QStringList artists() const { return m_artists; }
    QVariantList publishers() const { return m_publishers; }
    QVariantList sourceRatings() const { return m_sourceRatings; }
    QVariantList externalLinks() const { return m_externalLinks; }
    QVariantList allTitles() const { return m_allTitles; }
    bool hasMoreTitles() const { return m_allTitles.size() > 1; }
    QVariantList visibleTags() const { return m_visibleTags; }
    int totalTagCount() const { return m_allTags.size(); }
    int hiddenLowerTierCount() const { return m_hiddenLowerTierCount; }
    int hiddenImpliedCount() const { return m_hiddenImpliedCount; }
    int spoilerTagCount() const { return m_spoilerTagCount; }
    QString selectedTagName() const { return m_selectedTagName; }
    QString selectedTagPath() const { return m_selectedTagPath; }
    QString selectedTagDescription() const { return m_selectedTagDescription; }
    bool hasRawSources() const { return !m_rawSources.isEmpty(); }

    bool expandAlternateTitles() const { return m_prefs.expandAlternateTitles; }
    void setExpandAlternateTitles(bool value);
    bool showLowerTagTiers() const { return m_prefs.showLowerTagTiers; }
    void setShowLowerTagTiers(bool value);
    bool showImpliedTags() const { return m_prefs.showImpliedTags; }
    void setShowImpliedTags(bool value);
    bool revealSpoilers() const { return m_prefs.revealSpoilers; }
    void setRevealSpoilers(bool value);
    bool autoLoadArtwork() const { return m_prefs.autoLoadArtwork; }
    void setAutoLoadArtwork(bool value);
    bool compactLayout() const { return m_prefs.compactLayout; }
    void setCompactLayout(bool value);
    QString preferenceScopeNote() const { return m_preferenceScopeNote; }
    QString tagFilterText() const { return m_tagFilterText; }
    void setTagFilterText(const QString &value);
    QString tagFilterMode() const { return m_tagFilterMode; }
    void setTagFilterMode(const QString &value);
    QString statusFeedback() const { return m_statusFeedback; }

    QString colorWindow() const { return m_palette.color(QPalette::Window).name(); }
    QString colorBase() const { return m_palette.color(QPalette::Base).name(); }
    QString colorAlternateBase() const { return m_palette.color(QPalette::AlternateBase).name(); }
    QString colorText() const { return m_palette.color(QPalette::Text).name(); }
    QString colorMutedText() const { return m_palette.color(QPalette::PlaceholderText).name(); }
    QString colorButton() const { return m_palette.color(QPalette::Button).name(); }
    QString colorButtonText() const { return m_palette.color(QPalette::ButtonText).name(); }
    QString colorHighlight() const { return m_palette.color(QPalette::Highlight).name(); }
    QString colorHighlightedText() const { return m_palette.color(QPalette::HighlightedText).name(); }
    QString colorLink() const { return m_palette.color(QPalette::Link).name(); }
    QString colorMid() const { return m_palette.color(QPalette::Mid).name(); }

    Q_INVOKABLE void toggleSpoilerTag(int index);
    Q_INVOKABLE void peekSpoilerTag(int index, bool active);
    Q_INVOKABLE void selectTag(int index);
    Q_INVOKABLE void loadArtwork();
    Q_INVOKABLE void clearArtwork();
    Q_INVOKABLE void openCanonicalSource();
    Q_INVOKABLE void openSafeLink(const QString &url);
    Q_INVOKABLE void openRawData();
    Q_INVOKABLE void openAllTitlesDialog();
    Q_INVOKABLE void saveAsProviderDefault();
    Q_INVOKABLE void saveAsGlobalDefault();
    Q_INVOKABLE void resetPerResultOverrides();

    QString renderFallbackHtml() const;

    // Test hook to inspect weight ranking & artwork decoding deterministically without HTTP.
    static int weightTierRank(const QString &weightText);
    bool setArtworkImageDataForTesting(const QByteArray &imageBytes);

signals:
    void dataChanged();
    void artworkChanged();
    void tagsChanged();
    void tagSelectionChanged();
    void preferencesChanged();
    void feedbackChanged();
    void themeChanged();

private:
    struct ParsedTag {
        int id = 0;
        QString name;
        QString namePath;
        QString weightLabel;
        int weightRank = 1;
        bool isGenre = false;
        bool isSpoiler = false;
        bool isExplicit = true;
        QString impliedByText;
        QString contentRating;
        QString description;
        bool userRevealed = false;
        bool peekRevealed = false;
    };

    void rebuildVisibleTags();
    void applyPerResultOverride();
    void abortArtworkReply();
    void updatePreferenceScopeNote();

    SearchResult m_result;
    ResultMetadata m_metadata;
    ResultContent m_content;
    QString m_providerName;
    QPalette m_palette;
    QList<Content::RawSourceDescriptor> m_rawSources;

    QString m_preferredTitle;
    QString m_originalTitle;
    QString m_originalLanguage;
    QString m_formatLabel;
    QString m_status;
    QString m_yearText;
    QString m_publishedText;
    QString m_contentRating;
    QString m_licensingText;
    QString m_volumeChapterText;
    QString m_ratingText;
    int m_ratingCount = 0;
    QString m_synopsis;
    QString m_canonicalUrl;
    QString m_coverUrl;
    QString m_artworkState = QStringLiteral("none");
    QString m_artworkSource;
    QString m_artworkError;
    QStringList m_authors;
    QStringList m_artists;
    QVariantList m_publishers;
    QVariantList m_sourceRatings;
    QVariantList m_externalLinks;
    QVariantList m_allTitles;

    QList<ParsedTag> m_allTags;
    QVariantList m_visibleTags;
    int m_hiddenLowerTierCount = 0;
    int m_hiddenImpliedCount = 0;
    int m_spoilerTagCount = 0;
    QString m_selectedTagName;
    QString m_selectedTagPath;
    QString m_selectedTagDescription;
    QString m_tagFilterText;
    QString m_tagFilterMode = QStringLiteral("all");

    DisplayPreferences m_prefs;
    bool m_hasPerResultOverride = false;
    QString m_preferenceScopeNote;
    QString m_statusFeedback;

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_artworkReply;
};

#endif // MANGABAKAEXPERIMENTALMODEL_H
