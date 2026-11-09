// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "isolatedguitest.h"
#include "settingsdialog.h"
#include "providerauthentication.h"
#include "providerauthmanager.h"
#include "providerauthsession.h"
#include "providerpackage.h"
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QPluginLoader>
#include <KConfigGroup>
#include <KSharedConfig>
class CapabilityProvider : public SearchProvider, public ProviderAuthentication, public ProviderAuthenticationV2 {
    Q_OBJECT
    Q_INTERFACES(ProviderAuthentication ProviderAuthenticationV2)
public:
    AuthenticationDescriptorV2 descriptor;
    QString sourceId, credential;
    CapabilityProvider(const QString &id, AuthenticationMode mode) : sourceId(id) { descriptor.mode = mode; }
    QString id() const override { return sourceId; }
    QString name() const override { return sourceId; }
    AuthenticationDescriptor authenticationDescriptor() const override { return descriptor; }
    AuthenticationDescriptorV2 authenticationDescriptorV2() const override { return descriptor; }
    void setCredentials(const QString &, const QString &secret) override { credential = secret; }
    void search(const QString &) override {}
    SearchProvider *clone(QObject *) const override { return nullptr; }
};
class CapabilityDialog : public SettingsDialog {
public:
    using SettingsDialog::SettingsDialog;
    QString promptedLabel;
protected:
    QString promptForToken(const QString &, const QString &label, bool *accepted) override {
        promptedLabel = label; *accepted = true; return QStringLiteral("SYNTHETIC_SECRET_FOR_TEST");
    }
};
class AuthCapabilitiesTest : public QObject {
    Q_OBJECT
private slots:
    void compiledLegacyV1AuthenticationAbi() {
        QPluginLoader loader(QStringLiteral(LEGACY_AUTH_LIBRARY));
        auto *provider = qobject_cast<SearchProvider *>(loader.instance());
        QVERIFY2(provider, qPrintable(loader.errorString()));
        QCOMPARE(provider->id(), QStringLiteral("legacy_auth_abi_fixture"));
        auto *legacy = qobject_cast<ProviderAuthentication *>(provider);
        QVERIFY(legacy);
        const AuthenticationDescriptor descriptor = legacy->authenticationDescriptor();
        QCOMPARE(descriptor.mode, AuthenticationMode::OAuth2Pkce);
        QCOMPARE(descriptor.oauth.authorizationUrl, QUrl(QStringLiteral("https://example.invalid/authorize")));
        QCOMPARE(descriptor.oauth.scopes, QStringList{QStringLiteral("read")});
        const auto current = authenticationDescriptorV2(provider);
        QCOMPARE(current.mode, descriptor.mode);
        QCOMPARE(current.oauth.tokenUrl, descriptor.oauth.tokenUrl);
        QVERIFY(current.usable());
        provider->setEnabled(false);
        QVERIFY(!provider->isEnabled());
        provider->setEnabled(true);
        QVERIFY(provider->isEnabled());
    }
    void conditionalControlsAndSelectionSwaps() {
        CapabilityProvider none(QStringLiteral("none"), AuthenticationMode::None);
        CapabilityProvider token(QStringLiteral("token"), AuthenticationMode::ManualToken);
        token.descriptor.tokenLabel = QStringLiteral("Catalogue API token");
        CapabilityProvider oauth(QStringLiteral("oauth"), AuthenticationMode::OAuth2Pkce);
        oauth.descriptor.oauth = {QUrl(QStringLiteral("https://example.invalid/authorize")), QUrl(QStringLiteral("https://example.invalid/token")), {QStringLiteral("read")}, 0, QStringLiteral("/callback")};
        CapabilityProvider unsupported(QStringLiteral("unsupported"), AuthenticationMode::Unsupported);
        CapabilityProvider fixed(QStringLiteral("fixed"), AuthenticationMode::OAuth2Pkce);
        fixed.descriptor.oauth = oauth.descriptor.oauth; fixed.descriptor.publicClientId = QStringLiteral("fixed-public-id");
        ProviderAuthManager manager(nullptr);
        CapabilityDialog dialog({&none, &token, &oauth, &unsupported, &fixed}, nullptr, &manager);
        dialog.show();
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("nativeProviders")); QVERIFY(list);
        auto *client = dialog.findChild<QLineEdit *>(QStringLiteral("oauthClientId")); QVERIFY(client);
        auto *login = dialog.findChild<QPushButton *>(QStringLiteral("connectProvider")); QVERIFY(login);
        auto *wallet = dialog.findChild<QCheckBox *>(QStringLiteral("rememberAuthentication")); QVERIFY(wallet);
        auto *mode = dialog.findChild<QLabel *>(QStringLiteral("providerAuthMode")); QVERIFY(mode);
        for (int row : {0, 2, 1, 3, 0, 4, 0}) {
            list->setCurrentRow(row); QCoreApplication::processEvents();
            const bool supported = row == 1 || row == 2 || row == 4;
            QCOMPARE(login->isVisible(), supported); QCOMPARE(wallet->isVisible(), supported);
            QCOMPARE(client->isVisible(), row == 2);
            if (row != 2) QVERIFY(client->text().isEmpty());
            if (row == 0) QCOMPARE(mode->text(), QStringLiteral("No authentication required"));
            if (row == 3) QVERIFY(mode->text().contains(QStringLiteral("not implemented")));
            if (row == 2) { client->setText(QStringLiteral("draft-id")); emit client->textEdited(client->text()); }
        }
        QVERIFY(!manager.sessionFor(&none)); QVERIFY(!manager.sessionFor(&unsupported));
        list->setCurrentRow(1); wallet->setChecked(false);
        QVERIFY(QMetaObject::invokeMethod(&dialog, "showLoginDialog", Qt::DirectConnection));
        QVERIFY(dialog.promptedLabel.contains(QStringLiteral("Catalogue API token")));
        QCOMPARE(token.credential, QStringLiteral("SYNTHETIC_SECRET_FOR_TEST"));
        list->setCurrentRow(0);
        QVERIFY(QMetaObject::invokeMethod(&dialog, "saveSettings", Qt::DirectConnection));
        QFile config(qEnvironmentVariable("XDG_CONFIG_HOME") + QStringLiteral("/klookuprc"));
        QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(!config.readAll().contains("SYNTHETIC_SECRET_FOR_TEST"));
    }
    void scriptDeclarationsAreDataAndNeverAdvertiseCredentialExecution_data() {
        QTest::addColumn<QStringList>("methods"); QTest::addColumn<AuthenticationMode>("mode");
        QTest::newRow("none") << QStringList{QStringLiteral("urn:klookup:auth:none")} << AuthenticationMode::None;
        QTest::newRow("token") << QStringList{QStringLiteral("urn:klookup:auth:token")} << AuthenticationMode::ManualToken;
        QTest::newRow("oauth") << QStringList{QStringLiteral("urn:klookup:auth:oauth2-pkce")} << AuthenticationMode::OAuth2Pkce;
        QTest::newRow("password") << QStringList{QStringLiteral("urn:klookup:auth:username-password")} << AuthenticationMode::Unsupported;
        QTest::newRow("combined") << QStringList{QStringLiteral("urn:klookup:auth:token"), QStringLiteral("urn:klookup:auth:username-password")} << AuthenticationMode::Unsupported;
        QTest::newRow("unknown") << QStringList{QStringLiteral("urn:other:auth:future")} << AuthenticationMode::Unsupported;
    }
    void scriptDeclarationsAreDataAndNeverAdvertiseCredentialExecution() {
        QFETCH(QStringList, methods); QFETCH(AuthenticationMode, mode);
        QTemporaryDir root; QVERIFY(root.isValid());
        QJsonObject auth{{QStringLiteral("version"), QStringLiteral("1.0.0")}, {QStringLiteral("methods"), QJsonArray::fromStringList(methods)}};
        QFile manifest(root.filePath(QStringLiteral("provider.json"))); QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(QJsonDocument(QJsonObject{{QStringLiteral("id"), QStringLiteral("script_fixture")}, {QStringLiteral("name"), QStringLiteral("Script fixture")}, {QStringLiteral("authentication"), auth}}).toJson()); manifest.close();
        QFile script(root.filePath(QStringLiteral("main.mjs"))); QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("throw Error('must not run during metadata discovery');"); script.close();
        auto package = ProviderPackage::fromDirectory(root.path()); QVERIFY2(package.isValid(), qPrintable(package.validationError()));
        const auto descriptor = package.manifest().authentication;
        QCOMPARE(descriptor.mode, mode); QCOMPARE(descriptor.methods(), methods);
        QVERIFY(!descriptor.usable()); QVERIFY(!descriptor.walletSupported);
        SettingsDialog settings({}, nullptr, nullptr, {{package, QStringLiteral("Approval required")}});
        auto *list = settings.findChild<QListWidget *>(QStringLiteral("scriptedPackages")); QVERIFY(list); list->setCurrentRow(0);
        auto *identity = settings.findChild<QLabel *>(QStringLiteral("packageIdentity")); QVERIFY(identity);
        QVERIFY(identity->text().contains(mode == AuthenticationMode::None ? QStringLiteral("No authentication required") : QStringLiteral("unavailable")));
        auto invalid = auth; invalid.insert(QStringLiteral("methods"), QJsonArray{QStringLiteral("urn:klookup:auth:none"), QStringLiteral("urn:klookup:auth:token")});
        AuthenticationDescriptorV2 unchanged; QString error;
        QVERIFY(!AuthenticationDescriptorV2::fromManifest(invalid, &unchanged, &error));
        invalid = auth; invalid.insert(QStringLiteral("password"), QStringLiteral("not-allowed"));
        QVERIFY(!AuthenticationDescriptorV2::fromManifest(invalid, &unchanged, &error));
    }
};
int main(int argc, char **argv) { return runIsolatedGuiTest<AuthCapabilitiesTest>(argc, argv); }
#include "test_authcapabilities.moc"
