// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ISOLATEDGUITEST_H
#define ISOLATEDGUITEST_H
#include <QApplication>
#include <QTemporaryDir>
#include <QTest>

template<typename Test> int runIsolatedGuiTest(int argc, char **argv)
{
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    // Must precede QApplication and every KSharedConfig initialization.
    qputenv("XDG_CONFIG_HOME", root.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", root.filePath(QStringLiteral("config-dirs")).toUtf8());
    qputenv("XDG_DATA_HOME", root.filePath(QStringLiteral("data")).toUtf8());
    qputenv("XDG_DATA_DIRS", root.filePath(QStringLiteral("data-dirs")).toUtf8());
    QApplication app(argc, argv);
    Test test;
    return QTest::qExec(&test, argc, argv);
}
#endif
