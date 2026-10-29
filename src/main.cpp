// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include <KAboutData>
#include <KDBusService>
#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QGuiApplication::setDesktopFileName(QStringLiteral("org.kde.klookup"));
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("klookup");

    KAboutData aboutData(QStringLiteral("klookup"), i18n("KLookup"), QStringLiteral("0.1"),
                         i18n("Dictionary-style multi-provider search tool"), KAboutLicense::GPL_V3, i18n("(c) 2026"), QString(),
                         QStringLiteral("https://github.com/arran4/klookup"));
    aboutData.setLicense(KAboutLicense::GPL_V3, KAboutLicense::OrLaterVersions);
    aboutData.addAuthor(i18n("Arran"), i18n("Maintainer"), QStringLiteral("arran4@users.noreply.github.com"));
    KAboutData::setApplicationData(aboutData);

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    parser.addPositionalArgument(QStringLiteral("query"), i18n("Search query to execute"));
    QCommandLineOption backgroundOption(QStringList() << QStringLiteral("b") << QStringLiteral("background") << QStringLiteral("hidden"), i18n("Start in background / hidden."));
    parser.addOption(backgroundOption);
    QCommandLineOption showSearchOption(QStringLiteral("show-search"), i18n("Show and focus search (global shortcut action)."));
    QCommandLineOption searchClipboardOption(QStringLiteral("search-clipboard"),
                                             i18n("Explicitly search current plain clipboard text, including URLs."));
    QCommandLineOption newWindowOption(QStringList() << QStringLiteral("n") << QStringLiteral("new-window"),
                                       i18n("Open search in a new window."));
    parser.addOption(showSearchOption);
    parser.addOption(searchClipboardOption);
    parser.addOption(newWindowOption);

    parser.process(app);
    aboutData.processCommandLine(&parser);

    KDBusService service(KDBusService::Unique);

    MainWindow mw;

    auto dispatch = [&mw, backgroundOption, showSearchOption, searchClipboardOption, newWindowOption](const QCommandLineParser &p) {
        if (p.isSet(searchClipboardOption)) {
            mw.searchClipboardNow();
        } else if (p.isSet(showSearchOption)) {
            mw.showSearchViaShortcut();
        } else if (p.isSet(newWindowOption)) {
            const QStringList args = p.positionalArguments();
            const QString query = args.isEmpty() ? QString() : args.first();
            mw.openSearchInNewWindow(query);
        } else {
            if (!p.isSet(backgroundOption)) {
                mw.show();
                mw.raise();
                mw.activateWindow();
            }
            const QStringList args = p.positionalArguments();
            if (!args.isEmpty())
                mw.performSearch(args.first());
        }
    };
    QObject::connect(&service, &KDBusService::activateRequested, &mw,
                     [dispatch, backgroundOption, showSearchOption, searchClipboardOption, newWindowOption](const QStringList &arguments, const QString &) {
                         QCommandLineParser p;
                         p.addPositionalArgument(QStringLiteral("query"), QStringLiteral("Search query"));
                         p.addOption(backgroundOption);
                         p.addOption(showSearchOption);
                         p.addOption(searchClipboardOption);
                         p.addOption(newWindowOption);
                         p.process(arguments);
                         dispatch(p);
                     });
    dispatch(parser);
    return app.exec();
}
