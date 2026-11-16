// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LOCALDOCSCANNER_H
#define LOCALDOCSCANNER_H

#include "resultcontent.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <atomic>

class KConfigGroup;

struct LocalScanRoot {
    QString id;
    QString label;
    QString path;
    int maxDepth = 3;
    bool enabled = true;
};

struct LocalScanPolicy {
    int maxFileBytes = 512 * 1024;
    int maxFiles = 2000;
    qint64 maxTotalBytes = 32 * 1024 * 1024;
    int maxResults = 50;
    int maxDurationMs = 3000;
};

struct LocalScanPreview {
    int fileCount = 0;
    qint64 totalBytes = 0;
    bool truncated = false;
    QStringList sampleFiles;
    QStringList warnings;
};

struct LocalDocMatch {
    QString id;
    QString title;
    QString snippet;
    QString relativePath;
    QString rootLabel;
    ResultContent::Format format = ResultContent::Format::PlainText;
    QString body;
    int score = 0;
};

namespace LocalDocScanner
{
bool isAcceptedExtension(const QString &fileName);
bool isBroadOrHomeRoot(const QString &path);
QList<LocalScanRoot> readRoots(const KConfigGroup &group);
void writeRoots(KConfigGroup &group, const QList<LocalScanRoot> &roots);

LocalScanPreview preview(const QList<LocalScanRoot> &roots, const LocalScanPolicy &policy = LocalScanPolicy());
QList<LocalDocMatch> search(const QString &query,
                            const QList<LocalScanRoot> &roots,
                            const LocalScanPolicy &policy = LocalScanPolicy(),
                            const std::atomic_bool *cancelled = nullptr,
                            bool *wasCancelled = nullptr);
ResultContent toResultContent(const LocalDocMatch &match);
} // namespace LocalDocScanner

#endif // LOCALDOCSCANNER_H
