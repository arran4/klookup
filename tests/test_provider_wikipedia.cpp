#include "../providers/wikipedia/provider_wikipedia.h"
#include "../src/searchtab.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextBlock>
#include <QTextFragment>
#include <QUrlQuery>
#include <QtTest>

class WikiReply : public QNetworkReply
{
public:
    QByteArray body;
    bool aborted = false;
    WikiReply(QObject *parent, const QNetworkRequest &request) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOpenMode(ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
    }
    void finish(const QJsonObject &object)
    {
        body = QJsonDocument(object).toJson();
        setFinished(true);
        emit finished();
    }
    void abort() override
    {
        aborted = true;
        setFinished(true);
        setError(OperationCanceledError, {});
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return body.size() + QNetworkReply::bytesAvailable();
    }
    bool isSequential() const override
    {
        return true;
    }

protected:
    qint64 readData(char *out, qint64 size) override
    {
        const auto count = qMin(size, qint64(body.size()));
        memcpy(out, body.constData(), count);
        body.remove(0, count);
        return count;
    }
};
class WikiNetwork : public QNetworkAccessManager
{
public:
    QList<QPointer<WikiReply>> replies;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        auto *reply = new WikiReply(this, request);
        replies.append(reply);
        return reply;
    }
};
class WikiProvider : public ProviderWikipedia
{
public:
    WikiNetwork *network;
    WikiProvider()
    {
        delete m_nam;
        network = new WikiNetwork;
        network->setParent(this);
        m_nam = network;
    }
};
static QJsonObject searchFixture()
{
    return {
        {"query", QJsonObject{{"search", QJsonArray{QJsonObject{{"pageid", 123},
                                                                {"title", "First"},
                                                                {"snippet", "A <span class=\"searchmatch\">highlight</span> &amp; text"}},
                                                    QJsonObject{{"pageid", 456}, {"title", "Second"}, {"snippet", "second snippet"}},
                                                    QJsonObject{{"pageid", 1.2}, {"title", "bad"}}}}}},
        {"continue", QJsonObject{{"sroffset", 10}}}};
}
static QJsonObject articleFixture(int id, const QString &text)
{
    return {{"parse", QJsonObject{{"pageid", id}, {"title", "Article"}, {"text", text}}}};
}
static bool boldText(QTextDocument *document, const QString &text)
{
    for (auto block = document->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
            if (it.fragment().text().contains(text) && it.fragment().charFormat().fontWeight() >= QFont::Bold)
                return true;
    return false;
}
class WikipediaTest : public QObject
{
    Q_OBJECT
private slots:
    void sanitizer()
    {
        const auto formatted = Content::render(
            {QStringLiteral(
                 "<p>Plain &amp; <span class='searchmatch'>hit</span> <i>emphasis</i></p>"
                 "<p><a href='/wiki/Qt'>wiki</a><a href='javascript:alert(1)'>bad</a><a href='https://user:pass@host/a'>credentials</a></p>"
                 "<script>alert(9)</script><style>p{background:url(file:///private)}</style><img src='file:///private'>"
                 "<iframe src='https://bad'>frame</iframe><object data='file:///private'>object</object>"),
             ResultContent::Format::LimitedHtml, QUrl(QStringLiteral("https://en.wikipedia.org/wiki/Test"))});
        QVERIFY(formatted.html.contains(QStringLiteral("<b>hit</b>")));
        QVERIFY(formatted.html.contains(QStringLiteral("<i>emphasis</i>")));
        QVERIFY(formatted.html.contains(QStringLiteral("https://en.wikipedia.org/wiki/Qt")));
        for (const auto &denied : {"<img", "<script", "<style", "<iframe", "<object", "javascript:", "file:", "user:pass", "onerror="})
            QVERIFY2(!formatted.html.contains(QString::fromLatin1(denied)), denied);
        QVERIFY(formatted.plainText.contains(QStringLiteral("Plain & hit")));
        const auto plain = Content::render({QStringLiteral("<b>ordinary provider</b>"), ResultContent::Format::PlainText, {}});
        QVERIFY(plain.html.contains(QStringLiteral("&lt;b&gt;")));
        const auto bounded = Content::render({QString(300000, QLatin1Char('x')), ResultContent::Format::LimitedHtml, {}});
        QVERIFY(bounded.truncated);
        QVERIFY(bounded.plainText.size() <= 256 * 1024);
        QVERIFY(bounded.html.contains(QStringLiteral("display limit")));
    }
    void resourceAccessIsBlocked()
    {
        int requests = 0;
        QTextDocument::setDefaultResourceProvider([&](const QUrl &) {
            ++requests;
            return QVariant();
        });
        const auto result =
            Content::render({QStringLiteral("<link rel='stylesheet' href='file:///private'><img src='https://host/image'><p>safe</p>"),
                             ResultContent::Format::LimitedHtml,
                             {}});
        QTextDocument::setDefaultResourceProvider({});
        QCOMPARE(requests, 0);
        QVERIFY(!result.html.contains(QStringLiteral("image")));
    }
    void pagesAndDetails()
    {
        WikiProvider provider;
        QSignalSpy pages(&provider, &SearchProvider::pageCompleted);
        QSignalSpy details(&provider, &SearchProvider::detailsCompleted);
        QSignalSpy failed(&provider, &SearchProvider::requestFailed);
        provider.requestPage(QStringLiteral("a & b"), {}, 1);
        auto *reply = provider.network->replies.last().data();
        const QUrlQuery query(reply->url());
        QCOMPARE(query.queryItemValue(QStringLiteral("srsearch")), QStringLiteral("a & b"));
        QVERIFY(!query.hasQueryItem(QStringLiteral("pageid")));
        reply->finish(searchFixture());
        QCOMPARE(pages.count(), 1);
        const auto page = pages.first().at(1).value<SearchPage>();
        QCOMPARE(page.results.size(), 2);
        QCOMPARE(page.nextToken, QStringLiteral("10"));
        QCOMPARE(page.results.first().description, QStringLiteral("A highlight & text"));
        QCOMPARE(page.content.first().format, ResultContent::Format::LimitedHtml);
        QCOMPARE(provider.network->replies.size(), 1); // no article prefetch
        provider.requestPage(QStringLiteral("a & b"), page.nextToken, 2);
        QCOMPARE(QUrlQuery(provider.network->replies.last()->url()).queryItemValue(QStringLiteral("sroffset")), QStringLiteral("10"));
        provider.network->replies.last()->finish(QJsonObject{{"query", QJsonObject{{"search", QJsonArray{}}}}});
        QVERIFY(pages.last().at(1).value<SearchPage>().nextToken.isEmpty());
        provider.requestDetails(QStringLiteral("123"), {}, 3);
        QCOMPARE(QUrlQuery(provider.network->replies.last()->url()).queryItemValue(QStringLiteral("action")), QStringLiteral("parse"));
        provider.network->replies.last()->finish(articleFixture(123, QStringLiteral("<p>Useful <b>article</b>.</p>")));
        QCOMPARE(details.count(), 1);
        provider.requestDetails(QStringLiteral("123"), {}, 30);
        auto reserved = articleFixture(123, QStringLiteral("<p><a href='#section'>Section</a></p>"));
        auto parsed = reserved.value(QStringLiteral("parse")).toObject();
        parsed.insert(QStringLiteral("title"), QStringLiteral("A?B#C"));
        reserved.insert(QStringLiteral("parse"), parsed);
        provider.network->replies.last()->finish(reserved);
        const auto reservedDetails = details.last().at(1).value<SearchDetails>();
        QCOMPARE(reservedDetails.content.baseUrl.path(), QStringLiteral("/wiki/A?B#C"));
        QVERIFY(Content::render(reservedDetails.content).html.contains(QStringLiteral("A%3FB%23C#section")));

        QVERIFY(!details.first().at(1).value<SearchDetails>().metadata.attribution.isEmpty());
        provider.requestDetails(QStringLiteral("456"), {}, 4);
        provider.network->replies.last()->finish(articleFixture(456, QString()));
        QVERIFY(details.last().at(1).value<SearchDetails>().result.description.contains(QStringLiteral("No article content")));
        provider.requestDetails(QStringLiteral("123"), {}, 5);
        provider.network->replies.last()->finish(articleFixture(456, QStringLiteral("wrong")));
        QCOMPARE(failed.count(), 1);
        provider.requestPage(QStringLiteral("query"), {}, 6);
        provider.network->replies.last()->finish(QJsonObject{{"error", QJsonObject{{"info", "sensitive upstream text"}}}});
        QCOMPARE(failed.count(), 2);
        QVERIFY(!failed.last().at(1).toString().contains(QStringLiteral("sensitive")));
        provider.requestDetails(QStringLiteral("../123"), {}, 7);
        provider.requestPage(QStringLiteral("query"), QStringLiteral("-1"), 8);
        QCOMPARE(failed.count(), 4);
    }
    void selectionAndDestruction()
    {
        auto *tab = new SearchTab(QStringLiteral("query"));
        auto *provider = new WikiProvider;
        tab->startProvider(provider);
        provider->network->replies.last()->finish(searchFixture());
        auto *tree = tab->findChild<QTreeWidget *>();
        auto *view = tab->findChild<QTextBrowser *>();
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        QVERIFY(boldText(view->document(), QStringLiteral("highlight")));
        QCOMPARE(provider->network->replies.size(), 2);
        auto first = provider->network->replies.last();
        tree->setCurrentItem(tree->topLevelItem(0)->child(1));
        first->finish(articleFixture(123, QStringLiteral("<p><b>First article</b></p>")));
        QVERIFY(!view->toPlainText().contains(QStringLiteral("First article")));
        QCOMPARE(provider->network->replies.size(), 3);
        provider->network->replies.last()->finish(articleFixture(456, QStringLiteral("<p><i>Second article</i></p>")));
        QVERIFY(view->toPlainText().contains(QStringLiteral("Second article")));
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        QVERIFY(boldText(view->document(), QStringLiteral("First article")));
        QCOMPARE(provider->network->replies.size(), 3); // cached
        provider->requestDetails(QStringLiteral("123"), {}, 99);
        auto pending = provider->network->replies.last();
        delete tab;
        QVERIFY(pending.isNull());
    }
    void cancelledAndReplacedReplies()
    {
        WikiProvider provider;
        QSignalSpy pages(&provider, &SearchProvider::pageCompleted);
        QSignalSpy failed(&provider, &SearchProvider::requestFailed);
        provider.requestPage(QStringLiteral("one"), {}, 1);
        auto old = provider.network->replies.last();
        provider.requestPage(QStringLiteral("two"), {}, 2);
        QVERIFY(old->aborted);
        old->finish(searchFixture());
        QCOMPARE(pages.count(), 0);
        provider.cancelRequests();
        provider.network->replies.last()->finish(searchFixture());
        QCOMPARE(pages.count(), 0);
        QCOMPARE(failed.count(), 0);
    }
};
QTEST_MAIN(WikipediaTest)
#include "test_provider_wikipedia.moc"
