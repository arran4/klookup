#include <QApplication>
#include <QCommandLineParser>
#include <KAboutData>
#include <KLocalizedString>
#include <KDBusService>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addPositionalArgument(QStringLiteral("query"), QStringLiteral("Search query"));
    parser.process(app);

    MainWindow mw;
    mw.show();

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        mw.performSearch(args.first());
    }

    return app.exec();
}
