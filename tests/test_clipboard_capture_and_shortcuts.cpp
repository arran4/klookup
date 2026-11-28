// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/clipboardinput.h"
#include "../src/clipboardpolicy.h"
#include "../src/mainwindow.h"
#include "../src/searchdashboard.h"
#include "../src/searchtab.h"
#include "../src/settingsdialog.h"
#include <KActionCollection>
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

class ClipboardCaptureAndShortcutsTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_configHome;

private slots:
    void initTestCase()
    {
        QVERIFY(m_configHome.isValid());
        qputenv("XDG_CONFIG_HOME", m_configHome.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanup()
    {
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("General"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->sync();
        QGuiApplication::clipboard()->clear(QClipboard::Clipboard);
        if (QGuiApplication::clipboard()->supportsSelection())
            QGuiApplication::clipboard()->clear(QClipboard::Selection);
    }

    void sensitiveMimeAndEmbeddedUrisAreRejected()
    {
        QMimeData secret;
        secret.setText(QStringLiteral("hunter2"));
        secret.setData(QStringLiteral("x-kde-passwordManagerHint"), QByteArrayLiteral("secret"));
        QVERIFY(ClipboardInput::isSensitiveMimeData(&secret));
        QVERIFY(!ClipboardInput::classifyMimeData(&secret).permitsAutomaticCapture());
        QVERIFY(!ClipboardInput::evaluateAutomaticCapture(&secret).eligibleForManualPopulation);

        QMimeData keepass;
        keepass.setText(QStringLiteral("top-secret-token"));
        keepass.setData(QStringLiteral("application/x-keepass-entry"), QByteArrayLiteral("1"));
        QVERIFY(ClipboardInput::isSensitiveMimeData(&keepass));
        QVERIFY(!ClipboardInput::evaluateAutomaticCapture(&keepass).eligibleForAutomaticLookup);

        for (const QString &uriCase : {
                 QStringLiteral("See mailto:user@example.org for details"),
                 QStringLiteral("Lookup urn:isbn:9780140328721 quickly"),
                 QStringLiteral("Check example.org/reference next"),
                 QStringLiteral("Grab magnet:?xt=urn:btih:abcdef here"),
             }) {
            const auto dec = ClipboardInput::evaluateAutomaticCaptureText(uriCase);
            QVERIFY2(!dec.eligibleForAutomaticLookup, qPrintable(uriCase));
            QVERIFY2(!dec.eligibleForManualPopulation, qPrintable(uriCase));
        }

        // Valid shell commands and programming identifiers remain eligible.
        for (const QString &okCase : {
                 QStringLiteral("git status"),
                 QStringLiteral("QtCore.QString"),
                 QStringLiteral("readme.md"),
                 QStringLiteral("cmake --build build --parallel"),
             }) {
            const auto dec = ClipboardInput::evaluateAutomaticCaptureText(okCase);
            QVERIFY2(dec.eligibleForAutomaticLookup, qPrintable(okCase));
            QCOMPARE(dec.text, okCase);
        }
    }

    void charAndWordLimitsPopulateWithoutSilentTruncation()
    {
        ClipboardInput::AutoCaptureLimits limits;
        limits.maxChars = 180;
        limits.maxWords = 10;

        const QString elevenWords = QStringLiteral("one two three four five six seven eight nine ten eleven");
        const auto wordDecision = ClipboardInput::evaluateAutomaticCaptureText(elevenWords, limits);
        QVERIFY(!wordDecision.eligibleForAutomaticLookup);
        QVERIFY(wordDecision.eligibleForManualPopulation);
        QCOMPARE(wordDecision.text, elevenWords); // no silent truncation
        QCOMPARE(wordDecision.rejectionReason, QStringLiteral("exceeds_word_limit"));

        const QString longWord(181, QLatin1Char('k'));
        const auto charDecision = ClipboardInput::evaluateAutomaticCaptureText(longWord, limits);
        QVERIFY(!charDecision.eligibleForAutomaticLookup);
        QVERIFY(charDecision.eligibleForManualPopulation);
        QCOMPARE(charDecision.text, longWord);
        QCOMPARE(charDecision.rejectionReason, QStringLiteral("exceeds_char_limit"));
    }

    void manualEditIsNotOverwrittenByAutomaticCapture()
    {
        MainWindow window;
        auto *dashboard = window.findChild<SearchDashboard *>();
        auto *entry = dashboard ? dashboard->findChild<QLineEdit *>() : nullptr;
        QVERIFY(dashboard && entry);

        // Simulate user typing manually in the dashboard query box.
        QTest::keyClicks(entry, QStringLiteral("my manual query"));
        QVERIFY(dashboard->hasUserEditedQuery());

        QVERIFY(!dashboard->prepareCapturedQuery(QStringLiteral("background copy"), false, false));
        QCOMPARE(entry->text(), QStringLiteral("my manual query"));

        // Explicit capture overwrites even when forceOverwrite=true.
        QVERIFY(dashboard->prepareCapturedQuery(QStringLiteral("explicit capture"), true, true));
        QCOMPARE(entry->text(), QStringLiteral("explicit capture"));
        QVERIFY(!dashboard->hasUserEditedQuery());
    }

    void noAutoLookupWhenHiddenOrPausedAndDebounceCoalesces()
    {
        using namespace ClipboardPolicy;
        MainWindow window;
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("General"));
        Policy policy;
        policy.capture = Capture::Running;
        policy.trigger = Trigger::ClipboardChanged;
        policy.autoLookupEnabled = true;
        policy.autoDebounceMs = 80;
        write(group, policy);
        group.sync();

        auto *tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs);
        const int initial = tabs->count();

        // Hidden/tray-only window MUST NEVER launch automatic lookups.
        window.hide();
        QGuiApplication::clipboard()->setText(QStringLiteral("hidden no lookup"));
        QTest::qWait(150);
        QCOMPARE(tabs->count(), initial);

        // Visible window debounces rapid consecutive changes and only searches the latest value.
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QGuiApplication::clipboard()->setText(QStringLiteral("first rapid copy"));
        QTest::qWait(20);
        QGuiApplication::clipboard()->setText(QStringLiteral("second rapid copy"));
        QTest::qWait(180);
        QCOMPARE(tabs->count(), initial + 1);
        auto *debouncedTab = qobject_cast<SearchTab *>(tabs->widget(tabs->count() - 1));
        QVERIFY(debouncedTab);
        QCOMPARE(debouncedTab->query(), QStringLiteral("second rapid copy"));

        // Temporary pause suppresses automatic lookup without altering persistent policy.
        auto *pauseAction = window.actionCollection()->action(QStringLiteral("pause_auto_lookup"));
        QVERIFY(pauseAction && pauseAction->isCheckable());
        pauseAction->trigger();
        QVERIFY(window.isAutoLookupPaused());
        QGuiApplication::clipboard()->setText(QStringLiteral("paused query"));
        QTest::qWait(150);
        QCOMPARE(tabs->count(), initial + 1);
        QVERIFY(read(group).autoLookupEnabled);

        pauseAction->trigger();
        QVERIFY(!window.isAutoLookupPaused());
        QGuiApplication::clipboard()->setText(QStringLiteral("resumed query"));
        QTest::qWait(150);
        QCOMPARE(tabs->count(), initial + 1);
        QVERIFY(window.reusableAutoTab());
        QCOMPARE(window.reusableAutoTab()->query(), QStringLiteral("resumed query"));
    }

    void inputSourcesAreMutuallyExclusiveAndShortcutsStaySeparate()
    {
        using namespace ClipboardPolicy;
        MainWindow window;
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("General"));
        Policy policy;
        policy.capture = Capture::Visible;
        policy.trigger = Trigger::ClipboardChanged;
        policy.inputSource = InputSource::Clipboard;
        policy.autoLookupEnabled = true;
        policy.autoDebounceMs = 0;
        write(group, policy);
        group.sync();

        auto *tabs = window.findChild<QTabWidget *>();
        auto *dashboard = window.findChild<SearchDashboard *>();
        auto *entry = dashboard ? dashboard->findChild<QLineEdit *>() : nullptr;
        QVERIFY(tabs && dashboard && entry);

        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const int initial = tabs->count();

        // While Clipboard source is selected, PrimarySelection events are ignored (when supported).
        if (QGuiApplication::clipboard()->supportsSelection()) {
            QGuiApplication::clipboard()->setText(QStringLiteral("primary ignored"), QClipboard::Selection);
            QCoreApplication::processEvents();
            QCOMPARE(tabs->count(), initial);

            // Switch to PrimarySelection: now Clipboard copy events are ignored!
            policy.inputSource = InputSource::PrimarySelection;
            write(group, policy);
            group.sync();

            QGuiApplication::clipboard()->setText(QStringLiteral("clipboard ignored now"), QClipboard::Clipboard);
            QCoreApplication::processEvents();
            QCOMPARE(tabs->count(), initial);

            QGuiApplication::clipboard()->setText(QStringLiteral("primary accepted now"), QClipboard::Selection);
            QCoreApplication::processEvents();
            QCOMPARE(tabs->count(), initial + 1);
            auto *primaryTab = qobject_cast<SearchTab *>(tabs->widget(tabs->count() - 1));
            QVERIFY(primaryTab);
            QCOMPARE(primaryTab->query(), QStringLiteral("primary accepted now"));
        }

        // Verify two separate shortcuts: show_search (blank) vs search_clipboard (populate only by default).
        policy.inputSource = InputSource::Clipboard;
        policy.trigger = Trigger::Never;
        policy.autoLookupEnabled = false;
        policy.explicitCaptureImmediateSearch = false;
        write(group, policy);
        group.sync();

        auto *show = window.actionCollection()->action(QStringLiteral("show_search"));
        auto *capture = window.actionCollection()->action(QStringLiteral("search_clipboard"));
        QVERIFY(show && capture);

        const int beforeShortcuts = tabs->count();
        QGuiApplication::clipboard()->setText(QStringLiteral("explicit capture text"));
        capture->trigger();
        QCOMPARE(tabs->count(), beforeShortcuts);
        QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(entry->text(), QStringLiteral("explicit capture text"));

        show->trigger();
        QCOMPARE(tabs->count(), beforeShortcuts);
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(entry->text().isEmpty());
    }

    void policyPersistenceAndSettingsDialogRoundTrip()
    {
        using namespace ClipboardPolicy;
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("General"));
        write(group, Policy{});
        group.sync();

        SettingsDialog dialog({});
        auto *source = dialog.findChild<QComboBox *>(QStringLiteral("clipboardInputSource"));
        auto *autoLookup = dialog.findChild<QCheckBox *>(QStringLiteral("clipboardAutoLookupEnabled"));
        auto *immediate = dialog.findChild<QCheckBox *>(QStringLiteral("clipboardExplicitImmediateSearch"));
        auto *readOnOpen = dialog.findChild<QCheckBox *>(QStringLiteral("clipboardReadExistingOnOpen"));
        auto *maxChars = dialog.findChild<QSpinBox *>(QStringLiteral("clipboardAutoMaxChars"));
        auto *maxWords = dialog.findChild<QSpinBox *>(QStringLiteral("clipboardAutoMaxWords"));
        auto *debounce = dialog.findChild<QSpinBox *>(QStringLiteral("clipboardAutoDebounceMs"));
        auto *history = dialog.findChild<QComboBox *>(QStringLiteral("clipboardAutoHistoryMode"));
        QVERIFY(source && autoLookup && immediate && readOnOpen && maxChars && maxWords && debounce && history);

        QCOMPARE(source->currentIndex(), int(InputSource::Clipboard));
        QCOMPARE(maxChars->value(), 180);
        QCOMPARE(maxWords->value(), 10);
        QCOMPARE(debounce->value(), 300);
        QCOMPARE(history->currentIndex(), int(AutoHistoryMode::OnInteraction));

        source->setCurrentIndex(int(InputSource::PrimarySelection));
        autoLookup->setChecked(true);
        immediate->setChecked(true);
        readOnOpen->setChecked(true);
        maxChars->setValue(240);
        maxWords->setValue(15);
        debounce->setValue(450);
        history->setCurrentIndex(int(AutoHistoryMode::Never));
        dialog.button(QDialogButtonBox::Apply)->click();

        const Policy reloaded = read(group);
        QCOMPARE(reloaded.capture, Capture::Visible);
        QCOMPARE(reloaded.inputSource, InputSource::PrimarySelection);
        QVERIFY(reloaded.autoLookupEnabled);
        QVERIFY(reloaded.explicitCaptureImmediateSearch);
        QVERIFY(reloaded.readExistingOnOpen);
        QCOMPARE(reloaded.autoMaxChars, 240);
        QCOMPARE(reloaded.autoMaxWords, 15);
        QCOMPARE(reloaded.autoDebounceMs, 450);
        QCOMPARE(reloaded.autoHistoryMode, AutoHistoryMode::Never);
    }

    void restrictedScopeIntersectsPolicyAndIdentityBindingFailsClosed()
    {
        using namespace ClipboardPolicy;
        Policy policy;
        policy.autoOnlineEnabled = true;
        policy.autoOnlineAllowedProviderIds = {QStringLiteral("testonline")};
        policy.autoAcknowledgedOnlineProviderIds = {QStringLiteral("testonline")};
        policy.autoOnlineAllowedProviderTokens = {providerIdentityToken(QStringLiteral("testonline"))};
        policy.autoAcknowledgedOnlineProviderTokens = {providerIdentityToken(QStringLiteral("testonline"))};

        QVERIFY(policy.permitsAutoOnlineProvider(QStringLiteral("testonline"),
                                                 providerIdentityToken(QStringLiteral("testonline")),
                                                 {QStringLiteral("topic:general")}));
        // Scripted replacement or fingerprint change with the same ID must fail closed.
        QVERIFY(!policy.permitsAutoOnlineProvider(QStringLiteral("testonline"),
                                                  providerIdentityToken(QStringLiteral("testonline"), QStringLiteral("abc123")),
                                                  {QStringLiteral("topic:general")}));
        // Legacy ID-only approval must never silently approve a scripted override.
        Policy legacyPolicy;
        legacyPolicy.autoOnlineEnabled = true;
        legacyPolicy.autoOnlineAllowedProviderIds = {QStringLiteral("testonline")};
        legacyPolicy.autoAcknowledgedOnlineProviderIds = {QStringLiteral("testonline")};
        QVERIFY(legacyPolicy.permitsAutoOnlineProvider(QStringLiteral("testonline"),
                                                       providerIdentityToken(QStringLiteral("testonline")),
                                                       {}));
        QVERIFY(!legacyPolicy.permitsAutoOnlineProvider(QStringLiteral("testonline"),
                                                        providerIdentityToken(QStringLiteral("testonline"), QStringLiteral("deadbeef")),
                                                        {}));
    }

    void restrictedAutomaticLookupCannotBypassPolicyAndProcessClipboardRespectsLimits()
    {
        using namespace ClipboardPolicy;
        MainWindow window;
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("General"));
        Policy policy;
        policy.capture = Capture::Visible;
        policy.autoLookupEnabled = true;
        policy.autoOnlineEnabled = false;
        policy.autoMaxChars = 25;
        policy.autoMaxWords = 3;
        write(group, policy);
        group.sync();

        auto *tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs);
        const int before = tabs->count();

        // Direct processClipboardText(..., true) must still enforce automatic max chars/words limits.
        window.processClipboardText(QStringLiteral("one two three four"), true);
        QCOMPARE(tabs->count(), before);

        window.processClipboardText(QString(30, QLatin1Char('a')), true);
        QCOMPARE(tabs->count(), before);

        // Passing an arbitrary restricted scope must intersect with policy (unapproved online sources excluded).
        auto *onlineMock = new QObject();
        ProviderClassification onlineCls;
        onlineCls.facets = {QStringLiteral("topic:general")};
        delete onlineMock;
        struct LocalOnlineMock : public SearchProvider {
            int searches = 0;
            using SearchProvider::SearchProvider;
            QString name() const override { return QStringLiteral("Unapproved Online"); }
            QString id() const override { return QStringLiteral("unapproved_online"); }
            void search(const QString &) override { ++searches; emit searchCompleted(id(), {}); }
            SearchProvider *clone(QObject *parent = nullptr) const override {
                auto *c = new LocalOnlineMock(parent);
                connect(c, &SearchProvider::searchCompleted, this, [this]() { const_cast<LocalOnlineMock *>(this)->searches++; });
                return c;
            }
        };
        auto *prov = new LocalOnlineMock(&window);
        prov->setClassification(onlineCls);
        prov->setEnabled(true);
        window.m_providers.append(prov);
        const int beforeAuto = tabs->count();
        window.performAutomaticLookup(QStringLiteral("scoped bypass attempt"), QueryScope::only({QStringLiteral("unapproved_online")}));
        QCoreApplication::processEvents();
        QCOMPARE(tabs->count(), beforeAuto + 1);
        QCOMPARE(prov->searches, 0);

        const QueryScope policyScope = window.automaticLookupScopeForPolicy(policy);
        QVERIFY(!policyScope.includes(QStringLiteral("unapproved_online")));
    }

    void localdocsOverrideRemovalAndFallbackRestoreVerifiedLocalStatus()
    {
        QTemporaryDir tempPkgDir;
        QVERIFY(tempPkgDir.isValid());
        const QString overrideDir = tempPkgDir.path() + QStringLiteral("/localdocs");
        QVERIFY(QDir().mkpath(overrideDir));

        {
            QFile mf(overrideDir + QStringLiteral("/provider.json"));
            QVERIFY(mf.open(QIODevice::WriteOnly));
            mf.write("{\"apiVersion\":2,\"id\":\"localdocs\",\"name\":\"Fake Override\",\"version\":\"1.0.0\",\"runtime\":\"javascript\",\"entry\":\"main.mjs\",\"classification\":{\"version\":\"1.0.0\",\"facets\":[\"topic:general\"]}}\n");
        }
        {
            QFile js(overrideDir + QStringLiteral("/main.mjs"));
            QVERIFY(js.open(QIODevice::WriteOnly));
            js.write("module.exports = { search: function() { return []; } };\n");
        }

        const QString builtLocaldocs = QDir(QCoreApplication::applicationDirPath())
                                           .absoluteFilePath(QStringLiteral("../src/providers/provider_localdocs.so"));
        if (!QFileInfo::exists(builtLocaldocs))
            QSKIP("Built-in localdocs plugin not present in build test layout");

        QTemporaryDir fakeLibRoot;
        QVERIFY(fakeLibRoot.isValid());
        const QString builtinSubdir = fakeLibRoot.filePath(QStringLiteral("klookup/providers"));
        QVERIFY(QDir().mkpath(builtinSubdir));
        QVERIFY(QFile::copy(builtLocaldocs, builtinSubdir + QStringLiteral("/provider_localdocs.so")));
        QCoreApplication::addLibraryPath(fakeLibRoot.path());

        KConfigGroup provGroup(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Providers"));
        provGroup.writeEntry(QStringLiteral("localdocs_Enabled"), true);
        provGroup.sync();

        MainWindow window;
        window.loadPlugins({}, {});
        QVERIFY(window.provider(QStringLiteral("localdocs")) != nullptr);
        QVERIFY(window.isVerifiedLocalProvider(QStringLiteral("localdocs")));

        // Approve the override and reload -> scope:local must be revoked.
        window.loadPlugins({tempPkgDir.path()}, {});
        window.setPackageTrusted(QStringLiteral("localdocs"), true);
        window.loadPlugins({tempPkgDir.path()}, {});
        QVERIFY(!window.isVerifiedLocalProvider(QStringLiteral("localdocs")));
        QVERIFY(window.fallbackProvider(QStringLiteral("localdocs")) != nullptr);

        // Switching back to native fallback MUST restore verified local status and scope:local!
        QVERIFY(window.switchToFallback(QStringLiteral("localdocs")));
        QVERIFY(window.isVerifiedLocalProvider(QStringLiteral("localdocs")));
        QVERIFY(window.provider(QStringLiteral("localdocs"))->classification().facets.contains(
            QString::fromLatin1(ProviderClassification::LocalVirtualFacet)));

        // Re-apply scripted override, then remove package completely and reload -> must also restore!
        window.loadPlugins({tempPkgDir.path()}, {});
        QVERIFY(!window.isVerifiedLocalProvider(QStringLiteral("localdocs")));
        QVERIFY(QDir(overrideDir).removeRecursively());
        window.loadPlugins({tempPkgDir.path()}, {});
        QVERIFY(window.isVerifiedLocalProvider(QStringLiteral("localdocs")));
        QVERIFY(window.provider(QStringLiteral("localdocs"))->classification().facets.contains(
            QString::fromLatin1(ProviderClassification::LocalVirtualFacet)));

        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->deleteGroup(QStringLiteral("Providers"));
        KSharedConfig::openConfig(QStringLiteral("klookuprc"))->sync();
        QCoreApplication::removeLibraryPath(fakeLibRoot.path());
    }
};

QTEST_MAIN(ClipboardCaptureAndShortcutsTest)
#include "test_clipboard_capture_and_shortcuts.moc"
