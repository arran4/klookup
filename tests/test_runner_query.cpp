#include <QTest>

#include "../runner/queryparser.h"

class TestRunnerQuery : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void searchTerms_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty") << QStringLiteral("") << QStringLiteral("");
        QTest::newRow("too short") << QStringLiteral("Qt") << QStringLiteral("");
        QTest::newRow("normal") << QStringLiteral("Linux kernel") << QStringLiteral("Linux kernel");
        QTest::newRow("trim normal") << QStringLiteral("  Linux kernel  ") << QStringLiteral("Linux kernel");
        QTest::newRow("prefix") << QStringLiteral("lookup Linux kernel") << QStringLiteral("Linux kernel");
        QTest::newRow("prefix capitalization") << QStringLiteral("LOOKUP Qt6") << QStringLiteral("Qt6");
        QTest::newRow("prefix spaces") << QStringLiteral("lookup    KDE Plasma  ") << QStringLiteral("KDE Plasma");
        QTest::newRow("bare prefix") << QStringLiteral("lookup") << QStringLiteral("");
        QTest::newRow("empty prefixed") << QStringLiteral("lookup   ") << QStringLiteral("");
        QTest::newRow("short prefixed") << QStringLiteral("lookup ab") << QStringLiteral("");
        QTest::newRow("not a prefix") << QStringLiteral("lookups") << QStringLiteral("lookups");
        QTest::newRow("literal help placeholder") << QStringLiteral(":q:") << QStringLiteral(":q:");
    }

    void searchTerms()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(KLookupRunnerQuery::searchTerm(input), expected);
    }
};

QTEST_APPLESS_MAIN(TestRunnerQuery)

#include "test_runner_query.moc"
