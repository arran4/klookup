#include <QApplication>
#include <QCommandLineParser>
#include <KAboutData>
#include <KLocalizedString>
#include <KDBusService>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QGuiApplication::setDesktopFileName(QStringLiteral("org.kde.klookup"));
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("klookup");

    KAboutData aboutData(
        QStringLiteral("klookup"),
        i18n("KLookup"),
        QStringLiteral("0.1"),
        i18n("Dictionary-style multi-provider search tool"),
        KAboutLicense::GPL,
        i18n("(c) 2026"),
        QString(),
        QStringLiteral("https://github.com/arran4/klookup")
    );
    aboutData.addAuthor(i18n("Arran"), i18n("Maintainer"), QStringLiteral("arran4@users.noreply.github.com"));
    KAboutData::setApplicationData(aboutData);

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    parser.addPositionalArgument(QStringLiteral("query"), i18n("Search query to execute"));
    QCommandLineOption backgroundOption(QStringList() << QStringLiteral("b") << QStringLiteral("background"),
                                        i18n("Start in background."));
    parser.addOption(backgroundOption);

    parser.process(app);
    aboutData.processCommandLine(&parser);

    KDBusService service(KDBusService::Unique);

    MainWindow mw;

    QObject::connect(&service, &KDBusService::activateRequested, [&mw, backgroundOption](const QStringList &arguments, const QString &workingDirectory) {
        Q_UNUSED(workingDirectory);

        QCommandLineParser p;
        p.addPositionalArgument(QStringLiteral("query"), QStringLiteral("Search query"));
        p.addOption(backgroundOption);
        p.process(arguments);

        if (!p.isSet(backgroundOption)) {
            mw.show();
            mw.raise();
            mw.activateWindow();
        }

        const QStringList args = p.positionalArguments();
        if (!args.isEmpty()) {
            mw.performSearch(args.first());
        }
    });

    if (!parser.isSet(backgroundOption)) {
        mw.show();
    }

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        mw.performSearch(args.first());
    }

    return app.exec();
}
