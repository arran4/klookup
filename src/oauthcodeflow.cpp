#include "oauthcodeflow.h"
#include "oauthreplyhandler.h"
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUrlQuery>
#include <cstring>

namespace
{
QByteArray encodedForm(const QList<QPair<QString, QString>> &parameters)
{
    QByteArray form;
    for (const auto &parameter : parameters) {
        if (!form.isEmpty())
            form += '&';
        form += QUrl::toPercentEncoding(parameter.first) + '=' + QUrl::toPercentEncoding(parameter.second);
    }
    return form;
}
QString verifier()
{
    QByteArray random(48, Qt::Uninitialized);
    for (int offset = 0; offset < random.size(); offset += 4) {
        const quint32 value = QRandomGenerator::system()->generate();
        memcpy(random.data() + offset, &value, 4);
    }
    return QString::fromLatin1(random.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
} // namespace

OAuthCodeFlow::OAuthCodeFlow(const OAuthConfiguration &configuration, const QString &clientId, QNetworkAccessManager *network,
                             QObject *parent)
    : QObject(parent), m_configuration(configuration), m_clientId(clientId), m_network(network)
{
    network->setParent(this);
}
OAuthCodeFlow::~OAuthCodeFlow()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

void OAuthCodeFlow::attach(OAuthReplyHandler *handler)
{
    m_handler = handler;
    connect(handler, &OAuthReplyHandler::tokensReceived, this, [this](const QVariantMap &tokens) {
        m_accessToken = tokens.value(QStringLiteral("access_token")).toString();
        if (tokens.contains(QStringLiteral("refresh_token")))
            m_refreshToken = tokens.value(QStringLiteral("refresh_token")).toString();
        const qint64 lifetime = tokens.value(QStringLiteral("expires_in")).toLongLong();
        m_expiresAt = lifetime > 0 ? QDateTime::currentDateTimeUtc().addSecs(lifetime) : QDateTime();
        emit granted();
    });
}
void OAuthCodeFlow::authorize(OAuthReplyHandler *handler, const QString &state)
{
    attach(handler);
    m_verifier = verifier();
    const QString challenge = QString::fromLatin1(QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256)
                                                      .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    const QList<QPair<QString, QString>> parameters = {{QStringLiteral("response_type"), QStringLiteral("code")},
                                                       {QStringLiteral("client_id"), m_clientId},
                                                       {QStringLiteral("redirect_uri"), handler->callback()},
                                                       {QStringLiteral("state"), state},
                                                       {QStringLiteral("code_challenge"), challenge},
                                                       {QStringLiteral("code_challenge_method"), QStringLiteral("S256")},
                                                       {QStringLiteral("scope"), m_configuration.scopes.join(QLatin1Char(' '))}};
    QUrl url = m_configuration.authorizationUrl;
    QUrlQuery existing(url);
    for (const auto &parameter : parameters)
        existing.removeAllQueryItems(parameter.first);
    QByteArray query = existing.query(QUrl::FullyEncoded).toLatin1();
    if (!query.isEmpty())
        query += '&';
    query += encodedForm(parameters);
    url.setQuery(QString::fromLatin1(query), QUrl::StrictMode);
    connect(handler, &OAuthReplyHandler::callbackReceived, this, [this](const QVariantMap &values) {
        // The session's earlier callback validates the state and code first.
        if (!m_handler)
            return;
        requestToken({{QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
                      {QStringLiteral("client_id"), m_clientId},
                      {QStringLiteral("redirect_uri"), m_handler->callback()},
                      {QStringLiteral("code"), values.value(QStringLiteral("code")).toString()},
                      {QStringLiteral("code_verifier"), m_verifier}});
    });
    emit browserRequested(url);
}
void OAuthCodeFlow::refresh(OAuthReplyHandler *handler, const QString &refreshToken)
{
    attach(handler);
    m_refreshToken = refreshToken;
    requestToken({{QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
                  {QStringLiteral("client_id"), m_clientId},
                  {QStringLiteral("refresh_token"), refreshToken}});
}
void OAuthCodeFlow::requestToken(const QList<QPair<QString, QString>> &parameters)
{
    if (!m_handler || m_reply)
        return;
    QNetworkRequest request(m_configuration.tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    m_reply = m_network->post(request, encodedForm(parameters));
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = QPointer<QNetworkReply>(m_reply)] {
        if (!reply || reply != m_reply)
            return;
        m_reply = nullptr;
        if (m_handler)
            m_handler->networkReplyFinished(reply);
        else
            reply->deleteLater();
    });
}
