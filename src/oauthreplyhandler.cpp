#include "oauthreplyhandler.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

OAuthReplyHandler::OAuthReplyHandler(quint16 port, const QString &path, QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)), m_path(path)
{
    m_server->setMaxPendingConnections(4);
    m_server->listen(QHostAddress::LocalHost, port);
    QUrl callbackUrl;
    callbackUrl.setScheme(QStringLiteral("http"));
    callbackUrl.setHost(QStringLiteral("127.0.0.1"));
    callbackUrl.setPort(m_server->serverPort());
    callbackUrl.setPath(m_path);
    m_callbackUrl = callbackUrl.toString(QUrl::FullyEncoded);
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (auto *socket = m_server->nextPendingConnection()) {
            if (m_clients >= 4) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            ++m_clients;
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                --m_clients;
                socket->deleteLater();
            });
            auto buffer = QSharedPointer<QByteArray>::create();
            auto *timer = new QTimer(socket);
            timer->setSingleShot(true);
            connect(timer, &QTimer::timeout, socket, &QTcpSocket::abort);
            timer->start(5000);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, timer, buffer] {
                buffer->append(socket->read(qMax(qint64(0), qint64(8193 - buffer->size()))));
                auto respond = [socket](bool ok) {
                    socket->disconnect(socket);
                    const QByteArray body =
                        ok ? QByteArray("Return to KLookup. You may close this tab.\n") : QByteArray("Invalid authorization callback.\n");
                    socket->write(
                        (ok ? QByteArray("HTTP/1.1 200 OK\r\n") : QByteArray("HTTP/1.1 400 Bad Request\r\n")) +
                        "Content-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
                        QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                };
                if (buffer->size() > 8192 || buffer->contains('\0')) {
                    respond(false);
                    return;
                }
                if (!buffer->contains("\r\n\r\n"))
                    return;
                timer->stop();
                const auto lines = buffer->split('\n');
                const auto first = lines.first().trimmed().split(' ');
                if (first.size() != 3 || first[0] != "GET" || !first[1].startsWith('/') ||
                    (first[2] != "HTTP/1.1" && first[2] != "HTTP/1.0")) {
                    respond(false);
                    return;
                }
                const QUrl target(QString::fromLatin1(first[1]), QUrl::StrictMode);
                const auto expectedHost = QByteArray("127.0.0.1:") + QByteArray::number(m_server->serverPort());
                int hosts = 0;
                bool validHost = false;
                bool hasBody = false;
                for (int i = 1; i < lines.size(); ++i) {
                    const auto line = lines[i].trimmed();
                    if (line.toLower().startsWith("host:")) {
                        ++hosts;
                        const auto host = line.mid(5).trimmed();
                        validHost = host == expectedHost || (m_server->serverPort() == 80 && host == "127.0.0.1");
                    }
                    if (line.toLower().startsWith("content-length:") && line.mid(15).trimmed() != "0")
                        hasBody = true;
                    if (line.toLower().startsWith("transfer-encoding:"))
                        hasBody = true;
                }
                if (!target.isValid() || !target.isRelative() || !target.authority().isEmpty() || target.path() != m_path ||
                    target.hasFragment() || hosts != 1 || !validHost || hasBody) {
                    respond(false);
                    return;
                }
                QUrlQuery query(target);
                QVariantMap values;
                for (const auto &key : {QStringLiteral("state"), QStringLiteral("code"), QStringLiteral("error")}) {
                    const auto all = query.allQueryItemValues(key, QUrl::FullyEncoded);
                    if (all.size() > 1) {
                        respond(false);
                        return;
                    }
                    if (!all.isEmpty())
                        values.insert(key, key == QStringLiteral("error")
                                               ? QStringLiteral("authorization_rejected")
                                               : QUrl::fromPercentEncoding(all.first().toLatin1().replace('+', ' ')));
                }
                respond(true);
                emit callbackReceived(values);
            });
        }
    });
}

QString OAuthReplyHandler::callback() const
{
    return m_callbackUrl;
}

bool OAuthReplyHandler::isListening() const
{
    return m_server->isListening();
}
void OAuthReplyHandler::close()
{
    m_server->close();
}

void OAuthReplyHandler::networkReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->bytesAvailable() > 65536) {
        failure(false);
        return;
    }
    const auto doc = QJsonDocument::fromJson(reply->readAll());
    const auto object = doc.object();
    if (object.contains(QStringLiteral("error"))) {
        failure(object.value(QStringLiteral("error")).toString() == QStringLiteral("invalid_grant"));
        return;
    }
    const auto contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().section(QLatin1Char(';'), 0, 0).trimmed();
    if (reply->error() != QNetworkReply::NoError || (status && (status < 200 || status >= 300)) || !doc.isObject() ||
        contentType.compare(QStringLiteral("application/json"), Qt::CaseInsensitive) != 0) {
        failure(false);
        return;
    }
    QVariantMap safe;
    for (const auto &key : {QStringLiteral("access_token"), QStringLiteral("refresh_token"), QStringLiteral("token_type"),
                            QStringLiteral("expires_in"), QStringLiteral("scope"), QStringLiteral("id_token")})
        if (object.contains(key))
            safe.insert(key, object.value(key).toVariant());
    emit tokensReceived(safe);
}
