#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QLineEdit>
#include <KSharedConfig>
#include <KConfigGroup>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>

#include <KActionCollection>
#include <KStandardAction>

#include "../src/mainwindow.h"
#include "../src/searchdashboard.h"

namespace {
QStringList openQueries(MainWindow &window)
{
    auto *tabs = window.findChild<QTabWidget *>();
    if (!tabs) {
        return {};
    }

    QStringList queries;
    for (int index = 1; index < tabs->count(); ++index) {
        queries.append(tabs->tabText(index));
    }
    return queries;
}

QStringList history(MainWindow &window)
{
    auto *dashboard = window.findChild<SearchDashboard *>();
    return dashboard ? dashboard->saveHistory() : QStringList {};
}

bool savedStateIs(MainWindow &window, const QStringList &queries, const QStringList &expectedHistory)
{
    Q_UNUSED(window);
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    config->reparseConfiguration(); // Make sure we read the latest changes from disk
    KConfigGroup tabGroup(config, QStringLiteral("OpenTabs"));
    QStringList savedQueries;
    const int count = tabGroup.readEntry(QStringLiteral("Count"), 0);
    for (int index = 0; index < count; ++index) {
        savedQueries.append(tabGroup.readEntry(QStringLiteral("Tab%1").arg(index), QString()));
    }
    KConfigGroup historyGroup(config, QStringLiteral("History"));
    const QStringList savedHistory = historyGroup.readEntry(QStringLiteral("SearchHistory"), QStringList());
    return savedQueries == queries && savedHistory == expectedHistory;
}

void printState(MainWindow &window)
{
    QTextStream(stdout) << "STATE tabs=" << openQueries(window).join(QLatin1Char('|'))
                        << ";history=" << history(window).join(QLatin1Char('|')) << Qt::endl;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    if (app.arguments().size() != 2) {
        return 64;
    }
    const QString mode = app.arguments().at(1);
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));

    if (mode == QStringLiteral("restore")) {
        MainWindow window;
        printState(window);
        return 0;
    }

    config->deleteGroup("General");
    config->deleteGroup("OpenTabs");
    config->deleteGroup("History");
    KConfigGroup genGroup(config, QStringLiteral("General"));
    genGroup.writeEntry(QStringLiteral("MinimizeToTray"), mode != QStringLiteral("close"));
    genGroup.writeEntry(QStringLiteral("MigratedToKConfig"), true);
    config->sync();

    MainWindow window;
    QAction *quitAction = window.actionCollection()->action(KStandardAction::name(KStandardAction::Quit));
    if (!quitAction) {
        return 65;
    }
    auto *dashboard = window.findChild<SearchDashboard *>();
    auto *searchBar = dashboard ? dashboard->findChild<QLineEdit *>() : nullptr;
    if (!searchBar) {
        return 69;
    }

    const QStringList expectedQueries = { mode + QStringLiteral("-one"), mode + QStringLiteral("-two") };
    for (const QString &query : expectedQueries) {
        searchBar->setText(query);
        QMetaObject::invokeMethod(searchBar, "returnPressed");
    }
    const QStringList expectedHistory = { expectedQueries.at(1), expectedQueries.at(0) };
    window.show();

    QTimer::singleShot(0, &app, [&]() {
        if (mode == QStringLiteral("hide") || mode == QStringLiteral("tray-quit")) {
            window.close();
            if (window.isVisible() || !savedStateIs(window, {}, {})) {
                // No persisted state is expected until a genuine application quit.
                QCoreApplication::exit(66);
                return;
            }
        } else if (mode == QStringLiteral("close")) {
            window.close();
            KSharedConfig::openConfig(QStringLiteral("klookuprc"))->sync();
            if (!savedStateIs(window, expectedQueries, expectedHistory)) {
                QCoreApplication::exit(67);
                return;
            }
        }

        quitAction->trigger();
    });

    const int result = app.exec();
    if (result != 0 || !savedStateIs(window, expectedQueries, expectedHistory)) {
        return result != 0 ? result : 68;
    }
    printState(window);
    return 0;
}
