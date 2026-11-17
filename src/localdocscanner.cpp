// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "localdocscanner.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace
{
constexpr int kMaxRootCount = 32;
constexpr int kMaxDepthLimit = 8;

struct ScannedFile {
    QString canonicalPath;
    QString relativePath;
    QString rootLabel;
    QString fileName;
    qint64 size = 0;
    ResultContent::Format format = ResultContent::Format::PlainText;
};

ResultContent::Format formatForFile(const QString &fileName)
{
    const QString lower = fileName.toLower();
    if (lower.endsWith(QLatin1String(".md")) || lower.endsWith(QLatin1String(".markdown")))
        return ResultContent::Format::Markdown;
    if (lower.endsWith(QLatin1String(".html")) || lower.endsWith(QLatin1String(".htm")))
        return ResultContent::Format::LimitedHtml;
    return ResultContent::Format::PlainText;
}

QString htmlToSearchText(const QString &raw)
{
    QString stripped = raw;
    stripped.remove(QRegularExpression(QStringLiteral("<script\\b[^>]*>.*?</script>"),
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    stripped.remove(QRegularExpression(QStringLiteral("<style\\b[^>]*>.*?</style>"),
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    stripped.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QStringLiteral(" "));
    stripped.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return stripped.trimmed();
}

void collectFiles(const QList<LocalScanRoot> &roots,
                  const LocalScanPolicy &policy,
                  QList<ScannedFile> *outFiles,
                  LocalScanPreview *previewOut,
                  const std::atomic_bool *cancelled,
                  bool *wasCancelled)
{
    QDeadlineTimer deadline(qBound(100, policy.maxDurationMs, 30000));
    QSet<QString> seenPaths;
    qint64 totalBytes = 0;

    for (const auto &root : roots) {
        if (!root.enabled || root.path.trimmed().isEmpty())
            continue;
        if (cancelled && cancelled->load()) {
            if (wasCancelled)
                *wasCancelled = true;
            return;
        }
        const QFileInfo rootInfo(root.path);
        if (!rootInfo.exists() || !rootInfo.isDir() || rootInfo.isSymLink()) {
            if (previewOut)
                previewOut->warnings.append(i18n("Unavailable or symlinked root skipped: %1", root.label.isEmpty() ? root.path : root.label));
            continue;
        }
        const QString canonicalRoot = rootInfo.canonicalFilePath();
        if (canonicalRoot.isEmpty())
            continue;
        if (LocalDocScanner::isBroadOrHomeRoot(canonicalRoot) && previewOut) {
            previewOut->warnings.append(i18n("Broad root '%1' is depth- and count-bounded.", root.label.isEmpty() ? canonicalRoot : root.label));
        }

        const int maxDepth = qBound(0, root.maxDepth, kMaxDepthLimit);
        const QString rootLabel = root.label.trimmed().isEmpty() ? QFileInfo(canonicalRoot).fileName() : root.label.trimmed();

        struct PendingDir {
            QString path;
            int depth;
        };
        QList<PendingDir> queue{{canonicalRoot, 0}};
        QSet<QString> visitedDirs{canonicalRoot};

        while (!queue.isEmpty()) {
            if (cancelled && cancelled->load()) {
                if (wasCancelled)
                    *wasCancelled = true;
                return;
            }
            if (deadline.hasExpired() || outFiles->size() >= policy.maxFiles || totalBytes >= policy.maxTotalBytes) {
                if (previewOut)
                    previewOut->truncated = true;
                return;
            }

            const PendingDir current = queue.takeFirst();
            const QDir dir(current.path);
            const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
                                                            QDir::Name | QDir::DirsFirst);
            for (const QFileInfo &entry : entries) {
                if (cancelled && cancelled->load()) {
                    if (wasCancelled)
                        *wasCancelled = true;
                    return;
                }
                // Never follow symlinks or enter hidden directories/files.
                if (entry.isSymLink() || entry.fileName().startsWith(QLatin1Char('.')))
                    continue;
                const QString canonicalEntry = entry.canonicalFilePath();
                if (canonicalEntry.isEmpty() ||
                    (canonicalEntry != canonicalRoot && !canonicalEntry.startsWith(canonicalRoot + QLatin1Char('/'))))
                    continue;

                if (entry.isDir()) {
                    if (current.depth < maxDepth && !visitedDirs.contains(canonicalEntry)) {
                        visitedDirs.insert(canonicalEntry);
                        queue.append({canonicalEntry, current.depth + 1});
                    }
                    continue;
                }

                if (!entry.isFile() || !LocalDocScanner::isAcceptedExtension(entry.fileName()))
                    continue;
                if (entry.size() <= 0 || entry.size() > policy.maxFileBytes)
                    continue;
                if (seenPaths.contains(canonicalEntry))
                    continue;
                if (outFiles->size() >= policy.maxFiles || totalBytes + entry.size() > policy.maxTotalBytes) {
                    if (previewOut)
                        previewOut->truncated = true;
                    return;
                }

                seenPaths.insert(canonicalEntry);
                totalBytes += entry.size();
                const QString relPath = QDir(canonicalRoot).relativeFilePath(canonicalEntry);
                outFiles->append({canonicalEntry, relPath, rootLabel, entry.fileName(), entry.size(), formatForFile(entry.fileName())});
                if (previewOut) {
                    ++previewOut->fileCount;
                    previewOut->totalBytes = totalBytes;
                    if (previewOut->sampleFiles.size() < 10)
                        previewOut->sampleFiles.append(relPath);
                }
            }
        }
    }
}
} // namespace

bool LocalDocScanner::isAcceptedExtension(const QString &fileName)
{
    const QString lower = fileName.toLower();
    return lower.endsWith(QLatin1String(".txt")) || lower.endsWith(QLatin1String(".md")) ||
           lower.endsWith(QLatin1String(".markdown")) || lower.endsWith(QLatin1String(".html")) ||
           lower.endsWith(QLatin1String(".htm"));
}

bool LocalDocScanner::isBroadOrHomeRoot(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    const QString home = QDir::cleanPath(QDir::homePath());
    return clean == QLatin1String("/") || clean == home || clean == QLatin1String("/usr") || clean == QLatin1String("/etc");
}

QList<LocalScanRoot> LocalDocScanner::readRoots(const KConfigGroup &group)
{
    QList<LocalScanRoot> roots;
    const int count = qBound(0, group.readEntry(QStringLiteral("RootCount"), 0), kMaxRootCount);
    for (int i = 0; i < count; ++i) {
        const QString prefix = QStringLiteral("Root_%1_").arg(i);
        const QString path = group.readEntry(prefix + QStringLiteral("Path"), QString()).trimmed();
        if (path.isEmpty())
            continue;
        LocalScanRoot root;
        root.id = group.readEntry(prefix + QStringLiteral("Id"), QStringLiteral("root-%1").arg(i));
        root.label = group.readEntry(prefix + QStringLiteral("Label"), QFileInfo(path).fileName());
        root.path = path;
        root.maxDepth = qBound(0, group.readEntry(prefix + QStringLiteral("MaxDepth"), 3), kMaxDepthLimit);
        root.enabled = group.readEntry(prefix + QStringLiteral("Enabled"), true);
        roots.append(root);
    }
    return roots;
}

void LocalDocScanner::writeRoots(KConfigGroup &group, const QList<LocalScanRoot> &roots)
{
    const int oldCount = qBound(0, group.readEntry(QStringLiteral("RootCount"), 0), kMaxRootCount);
    for (int i = 0; i < oldCount; ++i) {
        const QString prefix = QStringLiteral("Root_%1_").arg(i);
        group.deleteEntry(prefix + QStringLiteral("Id"));
        group.deleteEntry(prefix + QStringLiteral("Label"));
        group.deleteEntry(prefix + QStringLiteral("Path"));
        group.deleteEntry(prefix + QStringLiteral("MaxDepth"));
        group.deleteEntry(prefix + QStringLiteral("Enabled"));
    }
    const int count = qMin(roots.size(), kMaxRootCount);
    group.writeEntry(QStringLiteral("RootCount"), count);
    for (int i = 0; i < count; ++i) {
        const QString prefix = QStringLiteral("Root_%1_").arg(i);
        group.writeEntry(prefix + QStringLiteral("Id"), roots[i].id.isEmpty() ? QStringLiteral("root-%1").arg(i) : roots[i].id);
        group.writeEntry(prefix + QStringLiteral("Label"), roots[i].label);
        group.writeEntry(prefix + QStringLiteral("Path"), roots[i].path);
        group.writeEntry(prefix + QStringLiteral("MaxDepth"), qBound(0, roots[i].maxDepth, kMaxDepthLimit));
        group.writeEntry(prefix + QStringLiteral("Enabled"), roots[i].enabled);
    }
}

LocalScanPreview LocalDocScanner::preview(const QList<LocalScanRoot> &roots,
                                          const LocalScanPolicy &policy,
                                          const std::atomic_bool *cancelled,
                                          bool *wasCancelled)
{
    LocalScanPreview result;
    QList<ScannedFile> files;
    collectFiles(roots, policy, &files, &result, cancelled, wasCancelled);
    return result;
}

QList<LocalDocMatch> LocalDocScanner::search(const QString &query,
                                             const QList<LocalScanRoot> &roots,
                                             const LocalScanPolicy &policy,
                                             const std::atomic_bool *cancelled,
                                             bool *wasCancelled)
{
    if (wasCancelled)
        *wasCancelled = false;
    const QString trimmedQuery = query.trimmed();
    if (trimmedQuery.isEmpty())
        return {};

    QList<ScannedFile> files;
    collectFiles(roots, policy, &files, nullptr, cancelled, wasCancelled);
    if (wasCancelled && *wasCancelled)
        return {};

    QList<LocalDocMatch> matches;
    const QString lowerQuery = trimmedQuery.toLower();
    QDeadlineTimer deadline(qBound(100, policy.maxDurationMs, 30000));

    for (const ScannedFile &file : files) {
        if (cancelled && cancelled->load()) {
            if (wasCancelled)
                *wasCancelled = true;
            return {};
        }
        if (deadline.hasExpired())
            break;

        QFile f(file.canonicalPath);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray rawBytes = f.read(policy.maxFileBytes + 1);
        if (rawBytes.size() > policy.maxFileBytes || rawBytes.contains('\0'))
            continue; // Ignore oversized or binary files.

        const QString rawText = QString::fromUtf8(rawBytes);
        const QString searchText = file.format == ResultContent::Format::LimitedHtml ? htmlToSearchText(rawText) : rawText;

        int score = 0;
        if (file.fileName.toLower().contains(lowerQuery))
            score += 50;
        const int hitPos = searchText.indexOf(trimmedQuery, 0, Qt::CaseInsensitive);
        if (hitPos >= 0)
            score += 30;
        if (score == 0)
            continue;

        QString snippet;
        if (hitPos >= 0) {
            const int start = qMax(0, hitPos - 60);
            const int len = qMin(searchText.size() - start, 200);
            snippet = searchText.mid(start, len).simplified();
        } else {
            snippet = searchText.left(200).simplified();
        }

        LocalDocMatch match;
        match.id = file.rootLabel + QLatin1Char(':') + file.relativePath;
        match.title = file.relativePath;
        match.snippet = snippet;
        match.relativePath = file.relativePath;
        match.rootLabel = file.rootLabel;
        match.format = file.format;
        match.body = rawText.left(64 * 1024);
        match.score = score;
        matches.append(match);
    }

    std::stable_sort(matches.begin(), matches.end(), [](const LocalDocMatch &a, const LocalDocMatch &b) {
        return a.score > b.score;
    });
    if (matches.size() > policy.maxResults)
        matches.resize(policy.maxResults);
    return matches;
}

ResultContent LocalDocScanner::toResultContent(const LocalDocMatch &match)
{
    ResultContent content;
    content.description = match.body;
    content.format = match.format;
    return content;
}
