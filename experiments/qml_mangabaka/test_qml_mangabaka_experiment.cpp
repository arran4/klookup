// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mangabakaviewmodel.h"
#include "qmlhostwidget.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTextBrowser>
#include <QtTest>

class QmlMangaBakaExperimentTest : public QObject
{
    Q_OBJECT

private slots:
    void approachAAndBLoadAndFallbackOnError()
    {
        QFile fixtureFile(QStringLiteral(":/klookup/experiments/mangabaka/fixtures/synthetic_manga.json"));
        QVERIFY(fixtureFile.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(fixtureFile.readAll());
        QVERIFY(doc.isObject());

        MangaBakaViewModel model;
        QVERIFY(model.loadFromJsonObject(doc.object()));
        QCOMPARE(model.primaryTitle(), QStringLiteral("Clockwork Lantern Archive"));
        QCOMPARE(model.formatType(), QStringLiteral("manga"));
        QCOMPARE(model.rating(), 88.4);
        QCOMPARE(model.ratingCount(), 1245);
        QCOMPARE(model.alternateTitles().size(), 4);

        QmlHostWidget host(&model);
        host.resize(720, 560);

        // Approach A: C++ ViewModel + QML declarative view.
        QVERIFY(host.loadQml(QUrl(QStringLiteral("qrc:/klookup/experiments/mangabaka/qml/MangaBakaDetailView.qml"))));
        QVERIFY(!host.isUsingFallback());
        QVERIFY(host.qmlErrorStrings().isEmpty());
        auto *rootA = host.quickWidget()->rootObject();
        QVERIFY(rootA);
        QCOMPARE(rootA->objectName(), QStringLiteral("approachADetailView"));

        // Approach B: QML logic + view transforming synthetic JSON fixture.
        QVERIFY(host.loadQml(QUrl(QStringLiteral("qrc:/klookup/experiments/mangabaka/qml/MangaBakaFullProvider.qml")),
                             doc.object()));
        QVERIFY(!host.isUsingFallback());
        auto *rootB = host.quickWidget()->rootObject();
        QVERIFY(rootB);
        QCOMPARE(rootB->property("computedTitle").toString(), QStringLiteral("Clockwork Lantern Archive (manga)"));
        QCOMPARE(rootB->property("computedGenreCount").toInt(), 3);

        // Fallback path when QML fails to load (renders safe HTML/text summary instead).
        QVERIFY(!host.loadQml(QUrl(QStringLiteral("qrc:/klookup/experiments/mangabaka/qml/DoesNotExist.qml"))));
        QVERIFY(host.isUsingFallback());
        QVERIFY(!host.qmlErrorStrings().isEmpty());
        QVERIFY(host.fallbackView()->toPlainText().contains(QStringLiteral("Clockwork Lantern Archive")));
    }
};

QTEST_MAIN(QmlMangaBakaExperimentTest)
#include "test_qml_mangabaka_experiment.moc"
