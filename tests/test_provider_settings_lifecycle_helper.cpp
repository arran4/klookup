#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QTextStream>
#include <QPluginLoader>
#include <QDir>
#include <QTabWidget>
#include <QTreeWidget>
#include <KSharedConfig>
#include <KConfigGroup>
#include <KActionCollection>
#include <KStandardAction>

#include "../src/mainwindow.h"
#include "../src/searchtab.h"
#include "../src/settingsdialog.h"
#include "../src/searchprovider.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    if (app.arguments().size() != 2) {
        return 64;
    }
    const QString mode = app.arguments().at(1);
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("klookuprc"));
    KConfigGroup providersGroup(config, QStringLiteral("Providers"));

    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());

    // By default MainWindow searches a hardcoded list of paths in loadPlugins
    // including QCoreApplication::applicationDirPath() + "/providers"
    // But our mocks are in QCoreApplication::applicationDirPath() (the root of the build/tests bin dir).
    // The loop in loadPlugins does this:
    // for (const QString &libPath : QCoreApplication::libraryPaths()) {
    //     pluginPaths.append(libPath + QStringLiteral("/klookup/providers"));
    //     pluginPaths.append(libPath + QStringLiteral("/klookup"));
    // }
    // It doesn't load directly from libPath unless it's in a subfolder like /klookup/providers.

    // Oh wait, MainWindow uses the following default paths:
    // QStringList pluginPaths = {
    //     QCoreApplication::applicationDirPath() + QStringLiteral("/../lib64/plugins/klookup"),
    //     QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/plugins/klookup"),
    //     QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/klookup"),
    //     QCoreApplication::applicationDirPath() + QStringLiteral("/providers"),
    // };
    // Let's create a symlink or directory for providers in the helper
    QDir().mkpath(QCoreApplication::applicationDirPath() + "/providers");
    if (QFile::exists(QCoreApplication::applicationDirPath() + "/provider_mock_alpha.so")) {
        QFile::copy(QCoreApplication::applicationDirPath() + "/provider_mock_alpha.so", QCoreApplication::applicationDirPath() + "/providers/provider_mock_alpha.so");
    }
    if (QFile::exists(QCoreApplication::applicationDirPath() + "/provider_mock_beta.so")) {
        QFile::copy(QCoreApplication::applicationDirPath() + "/provider_mock_beta.so", QCoreApplication::applicationDirPath() + "/providers/provider_mock_beta.so");
    }

    MainWindow window;

    if (mode == QStringLiteral("disable-alpha")) {
        providersGroup.writeEntry(QStringLiteral("mock_alpha_Enabled"), false);
        config->sync();

        QTextStream(stdout) << "STATE OK" << Qt::endl;
        return 0;
    } else if (mode == QStringLiteral("verify-alpha-disabled")) {
        window.performSearch("test2");
        auto *tabs = window.findChild<QTabWidget *>();
        if (!tabs || tabs->count() < 2) return 70;
        tabs->setCurrentIndex(1);
        auto *searchTab = qobject_cast<SearchTab*>(tabs->currentWidget());
        if (!searchTab) return 71;
        auto *treeWidget = searchTab->findChild<QTreeWidget*>();
        if (!treeWidget) return 72;

        bool foundAlpha = false;
        bool foundBeta = false;
        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
            QString name = treeWidget->topLevelItem(i)->text(0);
            if (name == QStringLiteral("Mock Alpha")) foundAlpha = true;
            if (name == QStringLiteral("Mock Beta")) foundBeta = true;
        }

        if (foundAlpha) {
            return 73;
        }
        if (!foundBeta) {
            return 74;
        }

        QTextStream(stdout) << "STATE OK" << Qt::endl;
        return 0;
    } else if (mode == QStringLiteral("enable-alpha")) {
        providersGroup.writeEntry(QStringLiteral("mock_alpha_Enabled"), true);
        config->sync();

        QTextStream(stdout) << "STATE OK" << Qt::endl;
        return 0;
    } else if (mode == QStringLiteral("verify-alpha-enabled")) {
        window.performSearch("test3");
        auto *tabs = window.findChild<QTabWidget *>();
        if (!tabs || tabs->count() < 2) return 75;
        tabs->setCurrentIndex(1);
        auto *searchTab = qobject_cast<SearchTab*>(tabs->currentWidget());
        if (!searchTab) return 76;
        auto *treeWidget = searchTab->findChild<QTreeWidget*>();
        if (!treeWidget) return 77;

        bool foundAlpha = false;
        bool foundBeta = false;
        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
            QString name = treeWidget->topLevelItem(i)->text(0);
            if (name == QStringLiteral("Mock Alpha")) foundAlpha = true;
            if (name == QStringLiteral("Mock Beta")) foundBeta = true;
        }

        if (!foundAlpha) {
            return 78;
        }
        if (!foundBeta) {
             return 79;
        }

        QTextStream(stdout) << "STATE OK" << Qt::endl;
        return 0;
    }

    return 80;
}
