// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "isolatedguitest.h"
#include "settingsdialog.h"
#include "searchprovider.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QListWidget>
#include <QScrollArea>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QLineEdit>
class SettingsPaneProvider : public SearchProvider {
public:
    using SearchProvider::SearchProvider;
    QString id() const override { return QStringLiteral("settings_fixture"); }
    QString name() const override { return QStringLiteral("Settings fixture"); }
    void search(const QString &) override { ++requests; }
    SearchProvider *clone(QObject *parent) const override { return new SettingsPaneProvider(parent); }
    int requests = 0;
};
class SettingsPanesTest : public QObject {
    Q_OBJECT
private slots:
    void resizingRestorationSelectionAndNarrowDialog() {
        SettingsPaneProvider provider;
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
        QMap<QString, QList<int>> saved;
        {
            SettingsDialog dialog({&provider}); dialog.resize(680, 520); dialog.show();
            auto *tabs = dialog.findChild<QTabWidget *>(); QVERIFY(tabs);
            const auto splitters = dialog.findChildren<QSplitter *>(); QCOMPARE(splitters.size(), 3);
            for (auto *splitter : splitters) {
                // Show the page containing the splitter before interacting with its handle.
                for (int i = 0; i < tabs->count(); ++i) if (tabs->widget(i)->isAncestorOf(splitter)) tabs->setCurrentIndex(i);
                QCoreApplication::processEvents();
                QCOMPARE(splitter->orientation(), Qt::Vertical);
                auto *list = splitter->widget(0)->findChild<QListWidget *>();
                if (!list) list = qobject_cast<QListWidget *>(splitter->widget(0));
                QVERIFY(list);
                if (list->count()) list->setCurrentRow(0);
                const int selected = list->currentRow();
                auto *handle = splitter->handle(1);
                QCOMPARE(handle->focusPolicy(), Qt::StrongFocus);
                QVERIFY(!handle->accessibleName().isEmpty());
                const auto before = splitter->sizes();
                QTest::keyClick(handle, Qt::Key_Down);
                QVERIFY(splitter->sizes()[0] > before[0]);
                QCOMPARE(list->currentRow(), selected);
                saved.insert(splitter->objectName(), group.readEntry(splitter->objectName(), QList<int>()));
                QCOMPARE(saved.value(splitter->objectName()), splitter->sizes());
                auto *scroll = qobject_cast<QScrollArea *>(splitter->widget(1)); QVERIFY(scroll && scroll->widgetResizable());
                QVERIFY(splitter->sizes()[0] >= 100 && splitter->sizes()[1] >= 100);
                if (qEnvironmentVariableIsSet("KLOOKUP_TEST_SCREENSHOTS"))
                    QVERIFY(dialog.grab().save(qEnvironmentVariable("KLOOKUP_TEST_SCREENSHOTS") + QLatin1Char('/') + splitter->objectName() + QStringLiteral(".png")));
            }
            QVERIFY(dialog.findChild<QDialogButtonBox *>());
            QCOMPARE(provider.requests, 0); // Resize and selection never run provider checks/search.
        }
        SettingsDialog restored({&provider}); restored.resize(1024, 768); restored.show();
        auto *tabs = restored.findChild<QTabWidget *>(); QVERIFY(tabs);
        for (auto *splitter : restored.findChildren<QSplitter *>()) {
            for (int i = 0; i < tabs->count(); ++i) if (tabs->widget(i)->isAncestorOf(splitter)) tabs->setCurrentIndex(i);
            QCoreApplication::processEvents();
            const auto expected = saved.value(splitter->objectName()); QVERIFY(expected.size() == 2);
            const auto actual = splitter->sizes();
            const double ratio = double(actual[0]) / (actual[0] + actual[1]);
            const double savedRatio = double(expected[0]) / (expected[0] + expected[1]);
            QVERIFY(qAbs(ratio - savedRatio) < 0.03);
            QTest::keyClick(splitter->handle(1), Qt::Key_Return);
            QVERIFY(qAbs(splitter->sizes()[0] - splitter->sizes()[1]) <= 2);
        }
        QCOMPARE(provider.requests, 0);
    }
};
int main(int argc, char **argv) { return runIsolatedGuiTest<SettingsPanesTest>(argc, argv); }
#include "test_settingspanes.moc"
