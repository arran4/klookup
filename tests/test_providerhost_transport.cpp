// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "providerhostservice.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <zlib.h>

using namespace ProviderHost;
constexpr int Limit = 5 * 1024 * 1024;

class HttpServer : public QObject {
public:
    struct Request {
        QByteArray method, target;
        QMap<QByteArray, QByteArray> headers;
        QPointer<QTcpSocket> socket;
    };
    QList<Request> requests;
    int disconnected = 0;
    std::function<void(const Request &)> handler;
    bool listen() {
        connect(&listener, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = listener.nextPendingConnection()) {
                auto input = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] { ++disconnected; socket->deleteLater(); });
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, input, handled] {
                    input->append(socket->readAll());
                    const auto end = input->indexOf("\r\n\r\n");
                    if (*handled || end < 0) return;
                    const auto lines = input->left(end).split('\n');
                    const auto first = lines.first().trimmed().split(' ');
                    if (first.size() < 2) return;
                    Request request{first[0], first[1], {}, socket};
                    for (int i = 1; i < lines.size(); ++i) {
                        const auto colon = lines[i].indexOf(':');
                        if (colon > 0) request.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
                    }
                    if (input->size() - end - 4 < request.headers.value("content-length").toInt()) return;
                    *handled = true;
                    requests.append(request);
                    if (handler) handler(request);
                });
            }
        });
        return listener.listen(QHostAddress::LocalHost, 0);
    }
    QUrl url(const QString &path = QStringLiteral("/")) const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(listener.serverPort()).arg(path));
    }
    static void send(const Request &request, const QByteArray &body, QByteArray headers = {},
                     bool chunked = false, int chunkBytes = 65536, int intervalMs = 0, int status = 200) {
        auto socket = request.socket;
        if (!socket) return;
        if (chunked) headers += "Transfer-Encoding: chunked\r\n";
        else if (!headers.contains("Content-Length:") && !headers.contains("Connection: close"))
            headers += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\nConnection: close\r\n" + headers + "\r\n");
        auto data = std::make_shared<QByteArray>(body);
        auto offset = std::make_shared<qsizetype>(0);
        auto *timer = new QTimer(socket);
        connect(timer, &QTimer::timeout, timer, [socket, timer, data, offset, chunked, chunkBytes] {
            if (!socket || socket->state() != QAbstractSocket::ConnectedState) { timer->stop(); timer->deleteLater(); return; }
            if (*offset >= data->size()) {
                if (chunked) socket->write("0\r\n\r\n");
                socket->disconnectFromHost();
                timer->stop(); timer->deleteLater(); return;
            }
            const auto chunk = data->mid(*offset, chunkBytes);
            *offset += chunk.size();
            if (chunked) socket->write(QByteArray::number(chunk.size(), 16) + "\r\n" + chunk + "\r\n");
            else socket->write(chunk);
        });
        timer->start(intervalMs);
    }
private:
    QTcpServer listener{this};
};

static QByteArray compressed(const QByteArray &input, bool gzip = true)
{
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, gzip ? 31 : 15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};
    QByteArray result(deflateBound(&stream, input.size()), Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = input.size();
    stream.next_out = reinterpret_cast<Bytef *>(result.data());
    stream.avail_out = result.size();
    const auto status = deflate(&stream, Z_FINISH);
    result.resize(stream.total_out);
    deflateEnd(&stream);
    return status == Z_STREAM_END ? result : QByteArray();
}

class TestProviderHostTransport : public QObject {
    Q_OBJECT
private:
    QTemporaryDir packageRoot;
    ProviderPackage package(const QList<QUrl> &origins) {
        QFile manifest(packageRoot.filePath(QStringLiteral("provider.json")));
        if (!manifest.open(QIODevice::WriteOnly)) return {};
        QJsonArray allowed;
        for (const auto &origin : origins) allowed.append(origin.toString(QUrl::RemovePath));
        manifest.write(QJsonDocument(QJsonObject{{QStringLiteral("id"), QStringLiteral("transport")},
            {QStringLiteral("name"), QStringLiteral("Transport")},
            {QStringLiteral("permissions"), QJsonObject{{QStringLiteral("origins"), allowed},
                {QStringLiteral("secrets"), QJsonArray{QStringLiteral("token")}}}}}).toJson());
        manifest.close();
        QFile entry(packageRoot.filePath(QStringLiteral("main.mjs")));
        if (!entry.open(QIODevice::WriteOnly)) return {};
        entry.write("export function search() { return []; }");
        entry.close();
        auto result = ProviderPackage::fromDirectory(packageRoot.path());
        result.setTrusted(true);
        return result;
    }
private slots:
    void bounds_data() {
        QTest::addColumn<int>("bytes");
        QTest::addColumn<bool>("chunked");
        QTest::addColumn<bool>("length");
        QTest::addColumn<int>("chunkBytes");
        QTest::newRow("exact-known-length") << Limit << false << true << 65536;
        QTest::newRow("over-known-length") << Limit + 1 << false << true << 65536;
        QTest::newRow("exact-no-length") << Limit << false << false << 8192;
        QTest::newRow("over-no-length") << Limit + 1 << false << false << 8192;
        QTest::newRow("exact-small-chunks") << Limit << true << false << 4096;
        QTest::newRow("over-small-chunks") << Limit + 1 << true << false << 4096;
    }
    void bounds() {
        QFETCH(int, bytes); QFETCH(bool, chunked); QFETCH(bool, length); QFETCH(int, chunkBytes);
        HttpServer server;
        QVERIFY(server.listen());
        server.handler = [&](const HttpServer::Request &request) {
            HttpServer::send(request, QByteArray(bytes, 'a'), length ? QByteArray() : QByteArray("Connection: close\r\n"), chunked, chunkBytes);
        };
        auto pkg = package({server.url()});
        QVERIFY2(pkg.isValid(), qPrintable(pkg.validationError()));
        ProviderHostService host(pkg);
        QSignalSpy responses(&host, &ProviderHostService::httpResponseReady);
        HttpRequestOptions options; options.url = server.url();
        host.executeHttpRequest(options);
        QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 10000);
        auto response = responses.first()[1].value<HttpResponse>();
        QCOMPARE(response.success, bytes <= Limit);
        QCOMPARE(response.truncated, bytes > Limit);
        QCOMPARE(response.body.size(), bytes <= Limit ? bytes : 0);
        QTest::qWait(10);
        QCOMPARE(responses.count(), 1);
        QTRY_VERIFY(host.networkAccessManager()->findChildren<QNetworkReply *>().isEmpty());
    }
    void misleadingLengthSlowStreamsAndConcurrentRequests() {
        HttpServer server; QVERIFY(server.listen());
        server.handler = [&](const HttpServer::Request &request) {
            if (request.target == "/misleading") HttpServer::send(request, QByteArray(Limit + 1, 'a'), "Content-Length: 1\r\n", true, 8192);
            else if (request.target == "/stream") HttpServer::send(request, QByteArray(32768, 'x'), {}, true, 1024, 3);
            else HttpServer::send(request, request.target, {}, false, 1024, request.target == "/a" ? 20 : 0);
        };
        ProviderHostService host(package({server.url()}));
        QSignalSpy responses(&host, &ProviderHostService::httpResponseReady);
        HttpRequestOptions options; options.url = server.url(QStringLiteral("/misleading"));
        host.executeHttpRequest(options);
        QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 10000);
        auto response = responses.takeFirst()[1].value<HttpResponse>();
        QVERIFY(!response.success && response.body.isEmpty() && !response.errorMessage.isEmpty());
        options.url = server.url(QStringLiteral("/stream")); options.timeoutMs = 25;
        host.executeHttpRequest(options);
        QTRY_COMPARE(responses.count(), 1);
        response = responses.takeFirst()[1].value<HttpResponse>();
        QVERIFY(!response.success && response.body.isEmpty() && response.errorMessage.contains(QStringLiteral("timed out")));
        options.timeoutMs = 1000; options.url = server.url(QStringLiteral("/a")); options.requestId = 61;
        host.executeHttpRequest(options);
        options.url = server.url(QStringLiteral("/b")); options.requestId = 62;
        host.executeHttpRequest(options);
        QTRY_COMPARE(responses.count(), 2);
        QMap<quint64, QByteArray> results;
        for (const auto &arguments : responses) {
            const auto result = arguments[1].value<HttpResponse>();
            QVERIFY(result.success); results.insert(result.requestId, result.body);
        }
        QCOMPARE(results.value(61), QByteArray("/a"));
        QCOMPARE(results.value(62), QByteArray("/b"));
        QTRY_VERIFY(host.networkAccessManager()->findChildren<QNetworkReply *>().isEmpty());
    }
    void reusedIdsSuppressAlreadyQueuedFailures() {
        HttpServer server; QVERIFY(server.listen());
        server.handler = [&](const HttpServer::Request &request) { HttpServer::send(request, "ok"); };
        auto pkg = package({server.url()});
        pkg.setTrusted(false);
        ProviderHostService host(pkg);
        QSignalSpy responses(&host, &ProviderHostService::httpResponseReady);
        HttpRequestOptions options; options.url = server.url(); options.requestId = 99;
        host.executeHttpRequest(options);
        pkg.setTrusted(true); host.setPackage(pkg);
        host.executeHttpRequest(options);
        QTRY_COMPARE(responses.count(), 1);
        QVERIFY(responses.first()[1].value<HttpResponse>().success);
        QTest::qWait(10);
        QCOMPARE(responses.count(), 1);
        QCOMPARE(server.requests.size(), 1);
    }

    void compression_data() {
        QTest::addColumn<QByteArray>("encoding"); QTest::addColumn<bool>("over");
        QTest::newRow("gzip-exact") << QByteArray("gzip") << false;
        QTest::newRow("gzip-over") << QByteArray("gzip") << true;
        QTest::newRow("deflate-exact") << QByteArray("deflate") << false;
        QTest::newRow("deflate-over") << QByteArray("deflate") << true;
    }
    void compression() {
        QFETCH(QByteArray, encoding); QFETCH(bool, over);
        HttpServer server; QVERIFY(server.listen());
        const auto payload = compressed(QByteArray(Limit + (over ? 1 : 0), 'z'), encoding == "gzip");
        QVERIFY(!payload.isEmpty() && payload.size() < Limit);
        server.handler = [&](const HttpServer::Request &request) {
            QCOMPARE(request.headers.value("accept-encoding"), QByteArray("gzip, deflate"));
            HttpServer::send(request, payload, "Content-Encoding: " + encoding + "\r\n", true, 127);
        };
        ProviderHostService host(package({server.url()}));
        QSignalSpy responses(&host, &ProviderHostService::httpResponseReady);
        HttpRequestOptions options; options.url = server.url();
        host.executeHttpRequest(options);
        QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 10000);
        const auto response = responses.first()[1].value<HttpResponse>();
        QCOMPARE(response.success, !over);
        QCOMPARE(response.truncated, over);
        QCOMPARE(response.body.size(), over ? 0 : Limit);
        if (over) QVERIFY(response.errorMessage.contains(QStringLiteral("decompressed")));
    }
    void compressedWireBoundAndCorruptStreams() {
        HttpServer server; QVERIFY(server.listen());
        QByteArray random(Limit, Qt::Uninitialized);
        quint32 state = 17;
        for (char &byte : random) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; byte = char(state); }
        const auto large = compressed(random);
        QVERIFY(large.size() > Limit);
        server.handler = [&](const HttpServer::Request &request) {
            if (request.target == "/wire") HttpServer::send(request, large, "Content-Encoding: gzip\r\n", true, 16384);
            else if (request.target == "/truncated") HttpServer::send(request, compressed("small").chopped(4), "Content-Encoding: gzip\r\n");
            else HttpServer::send(request, "not gzip", "Content-Encoding: gzip\r\n");
        };
        ProviderHostService host(package({server.url()}));
        QSignalSpy responses(&host, &ProviderHostService::httpResponseReady);
        for (const auto &path : {QStringLiteral("/wire"), QStringLiteral("/truncated"), QStringLiteral("/invalid")}) {
            HttpRequestOptions options; options.url = server.url(path);
            host.executeHttpRequest(options);
            QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 10000);
            const auto response = responses.takeFirst()[1].value<HttpResponse>();
            QVERIFY(!response.success);
            QVERIFY(response.body.isEmpty());
            QCOMPARE(response.truncated, path == QLatin1String("/wire"));
        }
    }
    void redirectsStripChangedPortSecretsAndPreserveSameOrigin() {
        HttpServer first, second; QVERIFY(first.listen()); QVERIFY(second.listen());
        first.handler = [&](const HttpServer::Request &request) {
            const auto target = request.target == "/same" ? first.url(QStringLiteral("/final")) : second.url(QStringLiteral("/final"));
            if (request.target == "/final") HttpServer::send(request, "same");
            else HttpServer::send(request, {}, "Location: " + target.toEncoded() + "\r\nSet-Cookie: remembered=private; Path=/\r\n", false, 1024, 0, 302);
        };
        second.handler = [&](const HttpServer::Request &request) { HttpServer::send(request, "changed"); };
        ProviderHostService host(package({first.url(), second.url()}));
        host.setSecretResolver([](const QString &) { return QStringLiteral("injected-private"); });
        // A populated shared-domain cookie jar must not recreate cookies on a
        // changed-port redirect after explicit headers have been stripped.
        QVERIFY(host.networkAccessManager()->cookieJar()->setCookiesFromUrl({QNetworkCookie("stored", "private")}, first.url()));
        QVERIFY(!host.networkAccessManager()->cookieJar()->cookiesForUrl(second.url()).isEmpty());
        QSignalSpy responses(&host, &ProviderHostService::httpResponseReady);
        HttpRequestOptions options; options.url = first.url(QStringLiteral("/same"));
        options.headers = {{QStringLiteral("Authorization"), QStringLiteral("Bearer private")},
            {QStringLiteral("X-Custom-Key"), QStringLiteral("private")}, {QStringLiteral("Cookie"), QStringLiteral("manual=private")}};
        options.secretKey = QStringLiteral("token"); options.secretHeader = QStringLiteral("X-Injected-Key");
        host.executeHttpRequest(options);
        QTRY_COMPARE(responses.count(), 1);
        QVERIFY(responses.takeFirst()[1].value<HttpResponse>().success);
        QCOMPARE(first.requests.last().headers.value("x-custom-key"), QByteArray("private"));
        QCOMPARE(first.requests.last().headers.value("x-injected-key"), QByteArray("injected-private"));
        QCOMPARE(first.requests.last().headers.value("cookie"), QByteArray("manual=private"));
        options.url = first.url(QStringLiteral("/other"));
        host.executeHttpRequest(options);
        QTRY_COMPARE(responses.count(), 1);
        QVERIFY(responses.takeFirst()[1].value<HttpResponse>().success);
        QCOMPARE(second.requests.size(), 1);
        for (const auto &name : {QByteArray("authorization"), QByteArray("x-custom-key"), QByteArray("x-injected-key"), QByteArray("cookie")})
            QVERIFY2(!second.requests.first().headers.contains(name), name.constData());
        QCOMPARE(second.requests.first().headers.value("user-agent"), QByteArray("KLookup/1.0 (XDG Provider Package)"));
        ProviderHostService restricted(package({first.url()}));
        QSignalSpy blocked(&restricted, &ProviderHostService::httpResponseReady);
        restricted.executeHttpRequest(options);
        QTRY_COMPARE(blocked.count(), 1);
        QVERIFY(!blocked.first()[1].value<HttpResponse>().success);
        QVERIFY(blocked.first()[1].value<HttpResponse>().errorMessage.contains(QStringLiteral("blocked by package policy")));
        QCOMPARE(second.requests.size(), 1); // Undeclared port was never contacted.
    }
    void timeoutCancellationConcurrencyAndDestruction() {
        HttpServer server; QVERIFY(server.listen());
        server.handler = [&](const HttpServer::Request &request) {
            if (request.target == "/slow") {
                request.socket->write("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx\r\n");
            } else HttpServer::send(request, request.target, {}, false, 1024, 1);
        };
        auto pkg = package({server.url()});
        auto host = std::make_unique<ProviderHostService>(pkg);
        QSignalSpy responses(host.get(), &ProviderHostService::httpResponseReady);
        HttpRequestOptions slow; slow.url = server.url(QStringLiteral("/slow")); slow.timeoutMs = 30; slow.requestId = 10;
        host->executeHttpRequest(slow);
        QTRY_COMPARE(responses.count(), 1);
        auto response = responses.takeFirst()[1].value<HttpResponse>();
        QVERIFY(!response.success && response.errorMessage.contains(QStringLiteral("timed out")));
        slow.timeoutMs = 5000; slow.requestId = 11;
        host->executeHttpRequest(slow);
        QTRY_COMPARE(server.requests.size(), 2);
        host->cancelRequest(11);
        HttpRequestOptions fast; fast.url = server.url(QStringLiteral("/fast")); fast.requestId = 12;
        host->executeHttpRequest(fast);
        QTRY_COMPARE(responses.count(), 1);
        response = responses.takeFirst()[1].value<HttpResponse>();
        QCOMPARE(response.requestId, 12ULL); QCOMPARE(response.body, QByteArray("/fast"));
        slow.requestId = 13; host->executeHttpRequest(slow);
        QTRY_COMPARE(server.requests.size(), 4);
        const int closed = server.disconnected;
        host.reset();
        QTRY_VERIFY(server.disconnected > closed);
        QCOMPARE(responses.count(), 0);
    }
    void redirectDeadlineAndHttpErrors() {
        HttpServer server; QVERIFY(server.listen());
        server.handler = [&](const HttpServer::Request &request) {
            if (request.target == "/rate") HttpServer::send(request, "{\"error\":\"limited\"}", "Retry-After: 12\r\n", false, 1024, 0, 429);
            else {
                auto next = request;
                QTimer::singleShot(15, request.socket, [next] { HttpServer::send(next, {}, "Location: /again\r\n", false, 1024, 0, 302); });
            }
        };
        ProviderHostService host(package({server.url()}));
        QSignalSpy responses(&host, &ProviderHostService::httpResponseReady);
        HttpRequestOptions options; options.url = server.url(QStringLiteral("/start")); options.timeoutMs = 25;
        host.executeHttpRequest(options);
        QTRY_COMPARE(responses.count(), 1);
        auto response = responses.takeFirst()[1].value<HttpResponse>();
        QVERIFY(!response.success && response.errorMessage.contains(QStringLiteral("timed out")));
        options.url = server.url(QStringLiteral("/rate")); options.timeoutMs = 1000;
        host.executeHttpRequest(options);
        QTRY_COMPARE(responses.count(), 1);
        response = responses.takeFirst()[1].value<HttpResponse>();
        QVERIFY(response.success); QCOMPARE(response.statusCode, 429); QCOMPARE(response.statusText, QStringLiteral("Test")); QCOMPARE(response.body, QByteArray("{\"error\":\"limited\"}"));
        bool retry = false;
        for (auto it = response.headers.cbegin(); it != response.headers.cend(); ++it)
            if (it.key().compare(QStringLiteral("Retry-After"), Qt::CaseInsensitive) == 0) retry = it.value() == QLatin1String("12");
        QVERIFY(retry);
    }
};
QTEST_GUILESS_MAIN(TestProviderHostTransport)
#include "test_providerhost_transport.moc"
