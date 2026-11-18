// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../providers/duckduckgo/provider_duckduckgo.h"
#include "providerdefaults.h"

#include <KConfig>
#include <KConfigGroup>
#include <QTemporaryDir>
#include <QTest>

class TestProviderDuckDuckGo : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultOffAndExplicitOverridePreserved()
    {
        QVERIFY(!ProviderDefaults::enabled(QStringLiteral("duckduckgo")));
        QVERIFY(ProviderDefaults::reason(QStringLiteral("duckduckgo")).contains(QStringLiteral("Instant Answer API")));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        KConfig cfg(dir.filePath(QStringLiteral("klookuprc")), KConfig::SimpleConfig);
        KConfigGroup providers(&cfg, QStringLiteral("Providers"));
        QVERIFY(!ProviderDefaults::enabled(providers, QStringLiteral("duckduckgo")));

        // Existing explicit opt-in setting must never be silently overwritten
        providers.writeEntry(QStringLiteral("duckduckgo_Enabled"), true);
        QVERIFY(ProviderDefaults::enabled(providers, QStringLiteral("duckduckgo")));
    }

    void testParsesAbstractDefinitionAnswerAndNestedTopics()
    {
        const QByteArray json =
            "{\n"
            "  \"Heading\": \"Qt\",\n"
            "  \"AbstractText\": \"Qt is a cross-platform framework.\",\n"
            "  \"AbstractSource\": \"Wikipedia\",\n"
            "  \"AbstractURL\": \"https://en.wikipedia.org/wiki/Qt_(software)\",\n"
            "  \"Entity\": \"software\",\n"
            "  \"Answer\": \"42\",\n"
            "  \"AnswerType\": \"calc\",\n"
            "  \"Definition\": \"A unit of software.\",\n"
            "  \"DefinitionSource\": \"Dictionary\",\n"
            "  \"DefinitionURL\": \"https://example.org/def\",\n"
            "  \"RelatedTopics\": [\n"
            "    { \"Text\": \"Qt Creator - IDE for Qt\", \"FirstURL\": \"https://duckduckgo.com/Qt_Creator\" },\n"
            "    { \"Name\": \"Bindings\", \"Topics\": [ { \"Text\": \"PyQt - Python bindings\", \"FirstURL\": \"https://duckduckgo.com/PyQt\" } ] }\n"
            "  ]\n"
            "}\n";

        QString error;
        const auto results = ProviderDuckDuckGo::parseResponse(json, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(results.size(), 5);
        QCOMPARE(results[0].id, QStringLiteral("abstract"));
        QCOMPARE(results[0].title, QStringLiteral("Qt"));
        QCOMPARE(results[0].additionalData.value(QStringLiteral("source")).toString(), QStringLiteral("Wikipedia"));
        QCOMPARE(results[1].id, QStringLiteral("answer"));
        QCOMPARE(results[1].type, QStringLiteral("calc"));
        QCOMPARE(results[2].id, QStringLiteral("definition"));
        QCOMPARE(results[3].id, QStringLiteral("https://duckduckgo.com/Qt_Creator"));
        QCOMPARE(results[3].title, QStringLiteral("Qt Creator"));
        QCOMPARE(results[4].id, QStringLiteral("https://duckduckgo.com/PyQt"));
        QCOMPARE(results[4].additionalData.value(QStringLiteral("topicGroup")).toString(), QStringLiteral("Bindings"));
    }

    void testInvalidJsonReportsError()
    {
        QString error;
        const auto results = ProviderDuckDuckGo::parseResponse(QByteArrayLiteral("{broken"), &error);
        QVERIFY(results.isEmpty());
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(TestProviderDuckDuckGo)
#include "test_provider_duckduckgo.moc"
