// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../providers/github/provider_github.h"

#include <QJsonObject>
#include <QTest>

class TestProviderGithub : public QObject
{
    Q_OBJECT

private slots:
    void testSemanticRepositoryMetadataExtraction()
    {
        const QByteArray json =
            "{\n"
            "  \"items\": [\n"
            "    {\n"
            "      \"id\": 12345,\n"
            "      \"full_name\": \"arran4/klookup\",\n"
            "      \"description\": \"Desktop lookup utility\",\n"
            "      \"html_url\": \"https://github.com/arran4/klookup\",\n"
            "      \"homepage\": \"https://arran4.github.io/klookup\",\n"
            "      \"stargazers_count\": 100,\n"
            "      \"forks_count\": 12,\n"
            "      \"open_issues_count\": 5,\n"
            "      \"watchers_count\": 100,\n"
            "      \"language\": \"C++\",\n"
            "      \"archived\": true,\n"
            "      \"fork\": false,\n"
            "      \"private\": false,\n"
            "      \"topics\": [\"kde\", \"qt6\", \"lookup\"],\n"
            "      \"license\": { \"spdx_id\": \"GPL-3.0-or-later\", \"name\": \"GNU General Public License v3.0 or later\" },\n"
            "      \"updated_at\": \"2026-11-15T10:00:00Z\",\n"
            "      \"pushed_at\": \"2026-11-15T09:00:00Z\"\n"
            "    }\n"
            "  ]\n"
            "}\n";

        bool ok = false;
        const auto results = ProviderGithub::parseResponse(json, &ok);
        QVERIFY(ok);
        QCOMPARE(results.size(), 1);
        const auto &r = results.first();
        QCOMPARE(r.id, QStringLiteral("12345"));
        QCOMPARE(r.title, QStringLiteral("arran4/klookup"));
        QCOMPARE(r.additionalData.value(QStringLiteral("stars")).toInt(), 100);
        QCOMPARE(r.additionalData.value(QStringLiteral("forks")).toInt(), 12);
        QCOMPARE(r.additionalData.value(QStringLiteral("openIssues")).toInt(), 5);
        QCOMPARE(r.additionalData.value(QStringLiteral("language")).toString(), QStringLiteral("C++"));
        QCOMPARE(r.additionalData.value(QStringLiteral("status")).toString(), QStringLiteral("Archived"));
        QCOMPARE(r.additionalData.value(QStringLiteral("license")).toString(), QStringLiteral("GPL-3.0-or-later"));
        QCOMPARE(r.additionalData.value(QStringLiteral("updatedAt")).toString(), QStringLiteral("2026-11-15"));
        QCOMPARE(r.additionalData.value(QStringLiteral("homepage")).toString(), QStringLiteral("https://arran4.github.io/klookup"));
        QCOMPARE(r.additionalData.value(QStringLiteral("topics")).toStringList(),
                 (QStringList{QStringLiteral("kde"), QStringLiteral("qt6"), QStringLiteral("lookup")}));
        QVERIFY(!r.additionalData.value(QStringLiteral("rawSources")).toList().isEmpty());
    }

    void testNullMissingFieldsAndUnsafeHomepageRejected()
    {
        const QByteArray json =
            "{\n"
            "  \"items\": [\n"
            "    {\n"
            "      \"id\": 99,\n"
            "      \"full_name\": \"org/minimal\",\n"
            "      \"description\": null,\n"
            "      \"html_url\": \"https://github.com/org/minimal\",\n"
            "      \"homepage\": \"javascript:alert(1)\",\n"
            "      \"language\": null,\n"
            "      \"license\": { \"spdx_id\": \"NOASSERTION\" }\n"
            "    }\n"
            "  ]\n"
            "}\n";

        bool ok = false;
        const auto results = ProviderGithub::parseResponse(json, &ok);
        QVERIFY(ok);
        QCOMPARE(results.size(), 1);
        const auto &r = results.first();
        QVERIFY(r.description.isEmpty());
        QVERIFY(!r.additionalData.contains(QStringLiteral("homepage")));
        QVERIFY(!r.additionalData.contains(QStringLiteral("language")));
        QVERIFY(!r.additionalData.contains(QStringLiteral("license")));
    }

    void testSelfTestV1Passes()
    {
        ProviderGithub gh;
        QVERIFY(gh.selfTestV1().value(QStringLiteral("passed")).toBool());
    }
};

QTEST_MAIN(TestProviderGithub)
#include "test_provider_github.moc"
