// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clipboardpolicy.h"
#include "searchprovider.h"
#include "selectedtextpolicy.h"
#include "selectionpopupwindow.h"

#include <KConfig>
#include <KConfigGroup>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
class MockPopupProvider : public SearchProvider
{
    Q_OBJECT
public:
    MockPopupProvider(QString id, QString name, bool local, int *callCount, QObject *parent = nullptr)
        : SearchProvider(parent)
        , m_id(std::move(id))
        , m_name(std::move(name))
        , m_local(local)
        , m_callCount(callCount)
    {
        ProviderClassification cls;
        cls.facets = local ? QStringList{QStringLiteral("local")} : QStringList{QStringLiteral("network")};
        setClassification(cls);
    }

    QString name() const override { return m_name; }
    QString id() const override { return m_id; }

    void search(const QString &query) override
    {
        if (m_callCount)
            ++(*m_callCount);
        SearchResult res;
        res.id = m_id + QLatin1Char(':') + query;
        res.title = QStringLiteral("Definition of %1").arg(query);
        res.description = QStringLiteral("From %1").arg(m_name);
        emit searchCompleted(m_id, {res});
    }

    SearchProvider *clone(QObject *parent = nullptr) const override
    {
        return new MockPopupProvider(m_id, m_name, m_local, m_callCount, parent);
    }

private:
    QString m_id;
    QString m_name;
    bool m_local;
    int *m_callCount;
};
} // namespace

class TestSelectedTextLookup : public QObject
{
    Q_OBJECT

private slots:
    void defaultsAreAllOffAndIndependentOfClipboardPolicy()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        KConfig cfg(dir.filePath(QStringLiteral("klookuprc")), KConfig::SimpleConfig);

        KConfigGroup genGroup(&cfg, QStringLiteral("General"));
        ClipboardPolicy::write(genGroup, {ClipboardPolicy::Capture::Running, ClipboardPolicy::Trigger::ClipboardChanged});

        KConfigGroup selGroup(&cfg, QStringLiteral("SelectedText"));
        const SelectedTextPolicy policy = SelectedTextPolicy::read(selGroup);

        QVERIFY(!policy.enabled);
        QVERIFY(!policy.autoPopup);
        QVERIFY(!policy.recordHistory);
        QVERIFY(policy.approvedRemoteProviderIds.isEmpty());
        QVERIFY(!policy.isEligibleSelection(QStringLiteral("word"), false));
        QVERIFY(!policy.isEligibleSelection(QStringLiteral("word"), true));
        QVERIFY(!policy.isProviderAllowed(QStringLiteral("localdocs"), true, false));
        QVERIFY(!policy.isProviderAllowed(QStringLiteral("wikipedia"), false, false));
    }

    void autoModeIsStrictlyLocalOnlyAndRejectsSensitiveInputs()
    {
        SelectedTextPolicy policy;
        policy.enabled = true;
        policy.autoPopup = true;
        policy.approvedRemoteProviderIds = {QStringLiteral("wikipedia")};

        // Even with explicit remote approval stored, automatic mode MUST block remote providers.
        QVERIFY(policy.isProviderAllowed(QStringLiteral("localdocs"), true, true));
        QVERIFY(!policy.isProviderAllowed(QStringLiteral("wikipedia"), false, true));

        // Explicit hotkey allows explicitly approved remote provider.
        QVERIFY(policy.isProviderAllowed(QStringLiteral("wikipedia"), false, false));
        QVERIFY(!policy.isProviderAllowed(QStringLiteral("github"), false, false));

        // Automatic mode rejects URLs, emails, paths, multiline, over-length, and secret MIME hints.
        QVERIFY(policy.isEligibleSelection(QStringLiteral("recursion"), true));
        QVERIFY(!policy.isEligibleSelection(QStringLiteral("https://example.com"), true));
        QVERIFY(!policy.isEligibleSelection(QStringLiteral("user@example.com"), true));
        QVERIFY(!policy.isEligibleSelection(QStringLiteral("/etc/passwd"), true));
        QVERIFY(!policy.isEligibleSelection(QStringLiteral("line1\nline2"), true));

        QMimeData secretMime;
        secretMime.setText(QStringLiteral("hunter2"));
        secretMime.setData(QStringLiteral("x-kde-passwordManagerHint"), QByteArrayLiteral("secret"));
        QVERIFY(!policy.isEligibleSelection(QStringLiteral("hunter2"), false, &secretMime));
        QVERIFY(!policy.isEligibleSelection(QStringLiteral("hunter2"), true, &secretMime));
    }

    void popupRespectsLocalOnlyAutoAndExplicitRemoteConsentAndFallback()
    {
        int localCalls = 0;
        int remoteCalls = 0;
        MockPopupProvider localProv(QStringLiteral("localdocs"), QStringLiteral("Local Docs"), true, &localCalls);
        MockPopupProvider remoteProv(QStringLiteral("wikipedia"), QStringLiteral("Wikipedia"), false, &remoteCalls);

        SelectedTextPolicy policy;
        policy.enabled = true;
        policy.autoPopup = true;
        policy.approvedRemoteProviderIds = {QStringLiteral("wikipedia")};

        SelectionPopupWindow popup;
        // 1. Automatic mode queries only local provider, never remote even if approved for explicit hotkey
        popup.lookupText(QStringLiteral("Qt"), {&localProv, &remoteProv}, policy, true);
        QCOMPARE(localCalls, 1);
        QCOMPARE(remoteCalls, 0);
        QCOMPARE(popup.resultCount(), 1);

        // 2. Automatic mode with no local provider explains unavailability and never calls remote
        localProv.setEnabled(false);
        popup.lookupText(QStringLiteral("Qt"), {&localProv, &remoteProv}, policy, true);
        QCOMPARE(remoteCalls, 0);
        QCOMPARE(popup.resultCount(), 0);
        QVERIFY(popup.statusText().contains(QStringLiteral("local"), Qt::CaseInsensitive));

        // 3. Explicit hotkey with unapproved remote shows one-time consent button instead of sending
        policy.approvedRemoteProviderIds.clear();
        popup.lookupText(QStringLiteral("Qt"), {&localProv, &remoteProv}, policy, false);
        QCOMPARE(remoteCalls, 0);
        auto *consentBtn = popup.findChild<QPushButton *>(QStringLiteral("selectionOneTimeRemoteBtn"));
        QVERIFY(consentBtn != nullptr);
        QVERIFY(!consentBtn->isHidden());
        consentBtn->click();
        QCOMPARE(remoteCalls, 1);
        QCOMPARE(popup.resultCount(), 1);

        // 4. Fallback prompt lets user enter a term deliberately
        popup.openFallbackPrompt(QStringLiteral("Selection unavailable on Wayland"), {&localProv, &remoteProv}, policy);
        auto *fallbackInput = popup.findChild<QLineEdit *>(QStringLiteral("selectionFallbackInput"));
        auto *fallbackBtn = popup.findChild<QPushButton *>(QStringLiteral("selectionFallbackSearchBtn"));
        QVERIFY(fallbackInput != nullptr && fallbackBtn != nullptr);
        localProv.setEnabled(true);
        fallbackInput->setText(QStringLiteral("CMake"));
        fallbackBtn->click();
        QCOMPARE(localCalls, 2);
        QCOMPARE(remoteCalls, 1);
    }

    void adversarialLocalSpoofingProviderNeverReceivesAutomaticSelection()
    {
        int adversarialCalls = 0;
        // Adversarial provider claims "local" facet and "local-remote" ID while actually performing remote I/O
        MockPopupProvider spoofingProv(QStringLiteral("local-remote"), QStringLiteral("Spoofed Local"), true, &adversarialCalls);
        int trustedCalls = 0;
        MockPopupProvider trustedLocal(QStringLiteral("localdocs"), QStringLiteral("Local Docs"), true, &trustedCalls);

        SelectedTextPolicy policy;
        policy.enabled = true;
        policy.autoPopup = true;
        policy.approvedRemoteProviderIds = {QStringLiteral("local-remote")};

        QVERIFY(!SelectedTextPolicy::isLocalProvider(&spoofingProv));
        QVERIFY(SelectedTextPolicy::isLocalProvider(&trustedLocal));

        SelectionPopupWindow popup;
        popup.lookupText(QStringLiteral("secret_selection"), {&spoofingProv, &trustedLocal}, policy, true);
        QCOMPARE(adversarialCalls, 0);
        QCOMPARE(trustedCalls, 1);
    }
};

QTEST_MAIN(TestSelectedTextLookup)
#include "test_selected_text_lookup.moc"
