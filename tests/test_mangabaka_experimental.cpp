// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "isolatedguitest.h"
#include "../providers/mangabaka/provider_mangabaka.h"
#include "../providers/mangabaka/provider_mangabaka_experimental.h"
#include "mangabakaexperimentalmodel.h"
#include "mangabakaexperimentalwidget.h"
#include "providerdefaults.h"
#include "resultdetailwindow.h"
#include "searchtab.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

namespace
{

class MockReply final : public QNetworkReply
{
public:
    MockReply(const QNetworkRequest &req, const QByteArray &body, int status = 200, QObject *parent = nullptr)
        : QNetworkReply(parent)
        , m_body(body)
    {
        setRequest(req);
        setUrl(req.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this]() {
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override { setFinished(true); }
    qint64 bytesAvailable() const override { return m_body.size() + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 max) override
    {
        const qint64 n = qMin<qint64>(max, m_body.size() - m_pos);
        if (n <= 0)
            return -1;
        memcpy(data, m_body.constData() + m_pos, n);
        m_pos += n;
        return n;
    }

private:
    QByteArray m_body;
    qint64 m_pos = 0;
};

class CountingMockNam final : public QNetworkAccessManager
{
public:
    explicit CountingMockNam(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}
    int requests = 0;
    QByteArray nextBody;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        ++requests;
        return new MockReply(request, nextBody, 200, this);
    }
};

class HookedMangaBaka final : public ProviderMangaBaka
{
public:
    explicit HookedMangaBaka(QObject *parent = nullptr)
        : ProviderMangaBaka(parent)
    {
        mockNam = new CountingMockNam(this);
        m_nam = mockNam;
        m_forceSharedCacheForTesting = true;
    }
    CountingMockNam *mockNam = nullptr;
};

class HookedMangaBakaExperimental final : public ProviderMangaBakaExperimental
{
public:
    explicit HookedMangaBakaExperimental(QObject *parent = nullptr)
        : ProviderMangaBakaExperimental(parent)
    {
        mockNam = new CountingMockNam(this);
        m_nam = mockNam;
        m_forceSharedCacheForTesting = true;
    }
    CountingMockNam *mockNam = nullptr;
};

QByteArray richSeriesFixtureJson()
{
    return QByteArrayLiteral(
        "{"
        "  \"data\": {"
        "    \"id\": 91234,"
        "    \"title\": \"Starlight Caravan\","
        "    \"description\": \"Complete synopsis.\\nWith <script>alert(1)</script> safely rendered as text.\","
        "    \"canonical_url\": \"https://mangabaka.org/manga/91234/starlight-caravan\","
        "    \"type\": \"manga\","
        "    \"status\": \"releasing\","
        "    \"original_language\": \"ja\","
        "    \"content_rating\": \"suggestive\","
        "    \"rating\": 88.4,"
        "    \"rating_count\": 1420,"
        "    \"year\": 2021,"
        "    \"is_licensed\": true,"
        "    \"final_volume\": null,"
        "    \"total_chapters\": 48,"
        "    \"published\": {\"start_date\": \"2021-05-01\", \"end_date\": null},"
        "    \"authors\": [\"Aoi Taniguchi\"],"
        "    \"artists\": [\"Kenji Morita\"],"
        "    \"publishers\": [{\"name\": \"Kodansha\", \"language\": \"ja\", \"canonical_url\": \"https://mangabaka.org/publisher/2\"}],"
        "    \"titles\": ["
        "      {\"language\": \"en\", \"title\": \"Starlight Caravan\", \"traits\": [\"official\"], \"is_primary\": true},"
        "      {\"language\": \"ja\", \"title\": \"星明りのキャラバン\", \"traits\": [\"native\"]},"
        "      {\"language\": \"ja-Latn\", \"title\": \"Hoshiakari no Caravan\", \"traits\": [\"romanized\"]}"
        "    ],"
        "    \"tags\": ["
        "      {\"id\": 1, \"name\": \"Adventure\", \"name_path\": \"Genres > Adventure\", \"weight\": \"defining\", \"is_genre\": true, \"is_spoiler\": false, \"is_explicit\": true},"
        "      {\"id\": 2, \"name\": \"Astronomy\", \"name_path\": \"Topics > Science > Astronomy\", \"weight\": \"major\", \"is_genre\": false, \"is_spoiler\": false, \"is_explicit\": true},"
        "      {\"id\": 3, \"name\": \"Traveling Band\", \"name_path\": \"Themes > Traveling Band\", \"weight\": \"minor\", \"is_genre\": false, \"is_spoiler\": false, \"is_explicit\": true},"
        "      {\"id\": 4, \"name\": \"Stargazing\", \"name_path\": \"Activities > Stargazing\", \"weight\": \"subtle\", \"is_genre\": false, \"is_spoiler\": false, \"implied_by_tag_ids\": [2]},"
        "      {\"id\": 5, \"name\": \"Orbital Twist\", \"name_path\": \"Plot > Orbital Twist\", \"weight\": \"major\", \"is_genre\": false, \"is_spoiler\": true, \"is_explicit\": true}"
        "    ],"
        "    \"source\": {\"anilist\": {\"id\": 112233, \"rating\": 8.6}, \"my_anime_list\": {\"id\": 445566, \"rating\": 8.4}},"
        "    \"links\": ["
        "      {\"name_display\": \"Official\", \"url\": \"https://example.org/caravan\", \"type\": \"info\"},"
        "      {\"name_display\": \"Unsafe\", \"url\": \"javascript:alert(1)\", \"type\": \"info\"}"
        "    ],"
        "    \"cover\": {\"x250\": \"https://images.example.org/caravan.jpg\"}"
        "  }"
        "}");
}

QByteArray createTestPngBytes()
{
    QImage img(120, 170, QImage::Format_RGB32);
    img.fill(QColor(QStringLiteral("#3b4252")));
    QPainter painter(&img);
    painter.setPen(QColor(QStringLiteral("#eceff4")));
    painter.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("COVER"));
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

} // namespace

class TestMangaBakaExperimental : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        ProviderMangaBaka::clearSharedCache();
        MangaBakaExperimentalModel::clearSessionOverrides();
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("DisplayPreferences"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("DisplayPreferences-mangabaka_experimental"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("Languages"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->sync();
    }

    void testCoexistenceAndDefaultDisabledPolicy()
    {
        ProviderMangaBaka standard;
        ProviderMangaBakaExperimental exp;
        QCOMPARE(standard.id(), QStringLiteral("mangabaka"));
        QCOMPARE(exp.id(), QStringLiteral("mangabaka_experimental"));
        QVERIFY(standard.name() != exp.name());

        QVERIFY(ProviderDefaults::enabled(QStringLiteral("mangabaka")));
        QVERIFY(!ProviderDefaults::enabled(QStringLiteral("mangabaka_experimental")));
        QVERIFY(!ProviderDefaults::reason(QStringLiteral("mangabaka_experimental")).isEmpty());
    }

    void testModelTiersSpoilersExplicitImpliedArtworkAndPreferences()
    {
        HookedMangaBakaExperimental provider;
        provider.mockNam->nextBody = richSeriesFixtureJson();

        QSignalSpy detailSpy(&provider, &SearchProvider::detailsCompleted);
        provider.requestDetails(QStringLiteral("91234"), QString(), 7);
        QTRY_COMPARE(detailSpy.count(), 1);
        const SearchDetails details = detailSpy.first().at(1).value<SearchDetails>();
        QCOMPARE(details.result.id, QStringLiteral("91234"));
        QVERIFY(MangaBakaExperimentalModel::isExperimentalResult(provider.id(), details.result));

        MangaBakaExperimentalModel model;
        CountingMockNam artworkNam;
        artworkNam.nextBody = createTestPngBytes();
        model.setNetworkAccessManager(&artworkNam);
        model.setResult(details.result, details.metadata, details.content, provider.name());

        // Privacy-first artwork policy: NO automatic fetch on selection!
        QCOMPARE(artworkNam.requests, 0);
        QCOMPARE(model.artworkState(), QStringLiteral("hidden"));

        // Safe plain-text synopsis & sanitized links
        QVERIFY(model.synopsis().contains(QStringLiteral("<script>alert(1)</script>")));
        QCOMPARE(model.externalLinks().size(), 1); // javascript: was stripped!
        QCOMPARE(model.allTitles().size(), 3);
        QVERIFY(model.hasMoreTitles());

        // Top 2 non-spoiler significance tiers ("defining" & "major") shown by default -> minor + subtle hidden
        QCOMPARE(model.totalTagCount(), 5);
        QCOMPARE(model.hiddenLowerTierCount(), 2);
        QCOMPARE(model.visibleTags().size(), 3); // Adventure(defining), Astronomy(major), Orbital Twist(major, spoiler)

        // Spoiler masked until hover/focus/click, and hidden from filter search while masked
        bool foundMaskedSpoiler = false;
        int spoilerIdx = -1;
        for (const QVariant &tv : model.visibleTags()) {
            const auto tm = tv.toMap();
            if (tm.value(QStringLiteral("isSpoiler")).toBool()) {
                foundMaskedSpoiler = !tm.value(QStringLiteral("isRevealed")).toBool();
                spoilerIdx = tm.value(QStringLiteral("index")).toInt();
                QVERIFY(tm.value(QStringLiteral("displayText")).toString() != QStringLiteral("Orbital Twist"));
            }
        }
        QVERIFY(foundMaskedSpoiler);
        model.setTagFilterText(QStringLiteral("Orbital"));
        QCOMPARE(model.visibleTags().size(), 0); // masked spoiler must not leak via filter!
        model.setTagFilterText({});

        // Hover/focus peek reveals temporarily; click toggles reveal cleanly
        model.peekSpoilerTag(spoilerIdx, true);
        model.selectTag(spoilerIdx);
        QCOMPARE(model.selectedTagName(), QStringLiteral("Orbital Twist"));
        model.peekSpoilerTag(spoilerIdx, false);
        model.toggleSpoilerTag(spoilerIdx);
        model.setTagFilterText(QStringLiteral("Orbital"));
        QCOMPARE(model.visibleTags().size(), 1);
        model.setTagFilterText({});

        // Lower tiers & explicit/implied filtering
        model.setShowLowerTagTiers(true);
        QCOMPARE(model.visibleTags().size(), 5);
        model.setShowImpliedTags(false);
        QCOMPARE(model.hiddenImpliedCount(), 1); // Stargazing was implied_by_tag_ids: [2]
        QCOMPARE(model.visibleTags().size(), 4);

        // Save per-result vs provider default vs global default (#144)
        model.saveAsProviderDefault();
        MangaBakaExperimentalModel fresh;
        fresh.setResult(details.result, details.metadata, details.content, provider.name());
        QVERIFY(fresh.showLowerTagTiers());
        QVERIFY(!fresh.showImpliedTags());

        // Per-result override stays local to that result unless saved
        fresh.setShowImpliedTags(true);
        QCOMPARE(fresh.visibleTags().size(), 5);
        fresh.resetPerResultOverrides();
        QVERIFY(!fresh.showImpliedTags());

        // Explicit click-to-load artwork
        model.loadArtwork();
        QTRY_COMPARE(model.artworkState(), QStringLiteral("loaded"));
        QCOMPARE(artworkNam.requests, 1);
        QVERIFY(model.artworkSource().startsWith(QStringLiteral("data:image/png;base64,")));
    }

    void testSearchTabRendersExperimentalViewWithoutAffectingStandardMangaBaka()
    {
        SearchTab tab(QStringLiteral("Caravan"));
        HookedMangaBakaExperimental expProv;
        expProv.mockNam->nextBody = richSeriesFixtureJson();
        QSignalSpy spy(&expProv, &SearchProvider::detailsCompleted);
        expProv.requestDetails(QStringLiteral("91234"), QString(), 9);
        QTRY_COMPARE(spy.count(), 1);
        const auto expDetails = spy.first().at(1).value<SearchDetails>();

        SearchResult stdResult;
        stdResult.id = QStringLiteral("100");
        stdResult.title = QStringLiteral("Standard MangaBaka");
        stdResult.description = QStringLiteral("Standard HTML summary.");
        stdResult.type = QStringLiteral("Manga");

        tab.addResults(QStringLiteral("MangaBaka"), {stdResult}, QStringLiteral("mangabaka"));
        tab.addResults(QStringLiteral("MangaBaka Experimental"), {expDetails.result}, QStringLiteral("mangabaka_experimental"));

        auto *tree = tab.findChild<QTreeWidget *>(QStringLiteral("resultsTree"));
        auto *summaryStack = tab.findChild<QStackedWidget *>(QStringLiteral("detailSummaryStack"));
        auto *stdView = tab.findChild<QTextBrowser *>(QStringLiteral("detailsView"));
        auto *expView = tab.findChild<MangaBakaExperimentalWidget *>(QStringLiteral("mangaBakaExperimentalSurface"));
        QVERIFY(tree && summaryStack && stdView && expView);

        // Selecting standard MangaBaka uses unchanged QTextBrowser page
        auto *stdItem = tree->topLevelItem(0)->child(0);
        tree->setCurrentItem(stdItem);
        emit tree->itemClicked(stdItem, 0);
        QCOMPARE(summaryStack->currentWidget(), stdView);
        QVERIFY(stdView->toPlainText().contains(QStringLiteral("Standard HTML summary.")));

        // Selecting MangaBaka Experimental activates the QML-backed/fallback experimental surface cleanly
        auto *expItem = tree->topLevelItem(1)->child(0);
        tree->setCurrentItem(expItem);
        emit tree->itemClicked(expItem, 0);
        QCOMPARE(summaryStack->currentWidget(), expView);
#ifdef KLOOKUP_HAS_QML_MANGABAKA
        QVERIFY2(expView->isUsingQml(), qPrintable(expView->qmlErrors().join(QLatin1Char('\n'))));
#else
        QVERIFY(!expView->isUsingQml());
#endif

        // Opening detail window for experimental result also renders cleanly
        ResultDetailWindow win(expDetails.result, QStringLiteral("MangaBaka Experimental"), expDetails.metadata, expDetails.content);
        auto *winExp = win.findChild<MangaBakaExperimentalWidget *>(QStringLiteral("mangaBakaExperimentalSurface"));
        QVERIFY(winExp != nullptr);
    }

    void testSharedResponseCacheBetweenStandardAndExperimental()
    {
        HookedMangaBaka standard;
        HookedMangaBakaExperimental experimental;
        const QByteArray searchJson = QByteArrayLiteral("{\"data\":[{\"id\":91234,\"title\":\"Starlight Caravan\",\"type\":\"manga\"}]}");
        standard.mockNam->nextBody = searchJson;
        experimental.mockNam->nextBody = searchJson;

        QSignalSpy stdSearchSpy(&standard, &SearchProvider::pageCompleted);
        standard.requestPage(QStringLiteral("caravan"), QString(), 1);
        QTRY_COMPARE(stdSearchSpy.count(), 1);
        QCOMPARE(standard.mockNam->requests, 1);
        QCOMPARE(ProviderMangaBaka::sharedCacheNetworkRequestCount(), 1);

        // Second search for identical query/page on experimental hits shared cache with zero extra network calls
        QSignalSpy expSearchSpy(&experimental, &SearchProvider::pageCompleted);
        experimental.requestPage(QStringLiteral("caravan"), QString(), 2);
        QTRY_COMPARE(expSearchSpy.count(), 1);
        QCOMPARE(experimental.mockNam->requests, 0);
        QCOMPARE(ProviderMangaBaka::sharedCacheNetworkRequestCount(), 1);
        const SearchPage cachedPage = expSearchSpy.first().at(1).value<SearchPage>();
        QCOMPARE(cachedPage.results.size(), 1);
        QVERIFY(MangaBakaExperimentalModel::isExperimentalResult(experimental.id(), cachedPage.results.first()));

        // Same for detail responses: standard populates, experimental reads cached payload and still decorates
        standard.mockNam->nextBody = richSeriesFixtureJson();
        experimental.mockNam->nextBody = richSeriesFixtureJson();
        QSignalSpy stdDetailSpy(&standard, &SearchProvider::detailsCompleted);
        standard.requestDetails(QStringLiteral("91234"), QString(), 3);
        QTRY_COMPARE(stdDetailSpy.count(), 1);
        QCOMPARE(standard.mockNam->requests, 2);
        QCOMPARE(ProviderMangaBaka::sharedCacheNetworkRequestCount(), 2);

        QSignalSpy expDetailSpy(&experimental, &SearchProvider::detailsCompleted);
        experimental.requestDetails(QStringLiteral("91234"), QString(), 4);
        QTRY_COMPARE(expDetailSpy.count(), 1);
        QCOMPARE(experimental.mockNam->requests, 0);
        QCOMPARE(ProviderMangaBaka::sharedCacheNetworkRequestCount(), 2);
        const SearchDetails expDetails = expDetailSpy.first().at(1).value<SearchDetails>();
        QVERIFY(MangaBakaExperimentalModel::isExperimentalResult(experimental.id(), expDetails.result));

        ProviderMangaBaka::clearSharedCache();
        QCOMPARE(ProviderMangaBaka::sharedCacheNetworkRequestCount(), 0);
    }
};

int main(int argc, char **argv)
{
    return runIsolatedGuiTest<TestMangaBakaExperimental>(argc, argv);
}

#include "test_mangabaka_experimental.moc"
