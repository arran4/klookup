// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providerhostservice.h"
#include "providerrequest.h"
#include "../providers/spotify/provider_spotify.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include <QSslSocket>
namespace {
QStringList closedDeviceWarnings;
QtMessageHandler previousHandler = nullptr;
void captureClosedWarning(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    if (message.contains(QStringLiteral("device not open"))) closedDeviceWarnings.append(message);
    if (previousHandler) previousHandler(type, context, message);
}
struct WarningCapture {
    WarningCapture() { closedDeviceWarnings.clear(); previousHandler = qInstallMessageHandler(captureClosedWarning); }
    ~WarningCapture() { qInstallMessageHandler(previousHandler); }
};
}
class ClosedReply : public QNetworkReply {
public:
    ClosedReply(NetworkError error, QObject *parent) : QNetworkReply(parent), socket(this) {
        setOpenMode(error == NoError ? QIODevice::NotOpen : QIODevice::ReadOnly);
        if (error != NoError) setError(error, QStringLiteral("Fixture transport failure"));
    }
    QSslSocket socket; int reads = 0;
    qint64 bytesAvailable() const override { return 1; } // Stale availability after transport closure.
    void finish() { setFinished(true); emit readyRead(); emit finished(); }
    void abort() override { setError(OperationCanceledError, QStringLiteral("Cancelled")); finish(); }
protected:
    qint64 readData(char *data, qint64 max) override { ++reads; return socket.read(data, max); }
};
class ClosedNetwork : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;
    QNetworkReply::NetworkError error = QNetworkReply::SslHandshakeFailedError;
    QPointer<ClosedReply> reply;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &, QIODevice *) override { reply = new ClosedReply(error, this); return reply; }
};
class ClosedProvider : public SearchProvider {
public:
    using SearchProvider::SearchProvider;
    QString id() const override { return QStringLiteral("fixture"); }
    QString name() const override { return id(); }
    void search(const QString &) override {}
    SearchProvider *clone(QObject *) const override { return nullptr; }
};
class ClosedSpotify : public ProviderSpotify {
public:
    ClosedSpotify() { m_nam->deleteLater(); network = new ClosedNetwork(this); m_nam = network; setCredentials({}, QStringLiteral("synthetic-fixture-token")); }
    ClosedNetwork *network;
};
class ClosedRepliesTest : public QObject {
    Q_OBJECT
private slots:
    void failedTransports_data() {
        QTest::addColumn<int>("error");
        QTest::newRow("tls-failed") << int(QNetworkReply::SslHandshakeFailedError);
        QTest::newRow("offline") << int(QNetworkReply::ConnectionRefusedError);
        QTest::newRow("timeout") << int(QNetworkReply::TimeoutError);
        QTest::newRow("cancelled") << int(QNetworkReply::OperationCanceledError);
        QTest::newRow("closed-without-error") << int(QNetworkReply::NoError);
    }
    void failedTransports() {
        QFETCH(int, error);
        WarningCapture warnings;
        ClosedNetwork network; network.error = QNetworkReply::NetworkError(error);
        ClosedProvider owner; QPointer<QNetworkReply> lane;
        QSignalSpy failed(&owner, &SearchProvider::requestFailed);
        ProviderRequest::getJson(&owner, &network, lane, QUrl(QStringLiteral("https://example.invalid")), 1, [](const QJsonObject &) { QFAIL("Closed reply cannot succeed"); });
        auto *reply = network.reply.data(); reply->finish();
        QCOMPARE(reply->reads, 0); QCOMPARE(failed.count(), 1);
        reply->finish(); QCOMPARE(failed.count(), 1); // Stale terminal callback.
        ClosedSpotify spotify; spotify.network->error = QNetworkReply::NetworkError(error);
        QSignalSpy spotifyFailed(&spotify, &SearchProvider::requestFailed);
        spotify.requestPage(QStringLiteral("fixture"), {}, 2);
        auto *spotifyReply = spotify.network->reply.data(); spotifyReply->finish();
        QCOMPARE(spotifyReply->reads, 0); QCOMPARE(spotifyFailed.count(), 1);
        QTemporaryDir root; QVERIFY(root.isValid());
        QFile manifest(root.filePath(QStringLiteral("provider.json"))); QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write("{\"id\":\"closed_fixture\",\"name\":\"Fixture\",\"permissions\":{\"origins\":[\"https://example.invalid\"]}}"); manifest.close();
        QFile script(root.filePath(QStringLiteral("main.mjs"))); QVERIFY(script.open(QIODevice::WriteOnly)); script.write("export function search() { return []; }"); script.close();
        auto package = ProviderPackage::fromDirectory(root.path()); QVERIFY(package.isValid()); package.setTrusted(true);
        ProviderHost::ProviderHostService host(package, nullptr, &network);
        QSignalSpy finished(&host, &ProviderHost::ProviderHostService::httpResponseReady);
        ProviderHost::HttpRequestOptions options; options.url = QUrl(QStringLiteral("https://example.invalid"));
        host.executeHttpRequest(options); reply = network.reply.data(); reply->finish();
        QCOMPARE(reply->reads, 0);
        reply->finish(); // A queued terminal callback must still be delivered only once.
        QTRY_COMPARE(finished.count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(finished.count(), 1);
        QVERIFY2(closedDeviceWarnings.isEmpty(), qPrintable(closedDeviceWarnings.join(QLatin1Char('\n'))));
    }
    void cancellationAndDestroyedOwnerIgnoreStaleCallbacks() {
        WarningCapture warnings;
        ClosedNetwork network;
        auto owner = std::make_unique<ClosedProvider>(); QPointer<QNetworkReply> lane;
        ProviderRequest::getJson(owner.get(), &network, lane, QUrl(QStringLiteral("https://example.invalid")), 1, [](const QJsonObject &) {});
        auto *reply = network.reply.data();
        ProviderRequest::cancel(lane, owner.get()); reply->finish(); QCOMPARE(reply->reads, 0);
        owner.reset(); reply->finish(); QCOMPARE(reply->reads, 0);
        QVERIFY(closedDeviceWarnings.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ClosedRepliesTest)
#include "test_closedreplies.moc"
