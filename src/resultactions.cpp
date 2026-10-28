#include "resultactions.h"
#include "sourceurl.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMimeData>

ResultActions::ResultActions(const SearchResult &result, bool canRefresh, QObject *parent) : QObject(parent)
{
    refresh = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")), QStringLiteral("Refresh details"), this);
    refresh->setObjectName(QStringLiteral("refresh_details"));
    refresh->setEnabled(canRefresh);
    connect(refresh, &QAction::triggered, this, &ResultActions::refreshRequested);
    const QUrl url = validatedSourceUrl(result.url);
    copyUrl = new QAction(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Copy URL"), this);
    copyUrl->setObjectName(QStringLiteral("copy_url"));
    copyUrl->setEnabled(!url.isEmpty());
    openUrl = new QAction(QIcon::fromTheme(QStringLiteral("internet-web-browser")), QStringLiteral("Open in Web Browser"), this);
    openUrl->setObjectName(QStringLiteral("open_url"));
    openUrl->setEnabled(!url.isEmpty());
    copyTitle = new QAction(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Copy Title"), this);
    copyTitle->setObjectName(QStringLiteral("copy_title"));
    copyTitle->setEnabled(!result.title.isEmpty());
    connect(copyUrl, &QAction::triggered, this, [url] {
        if (url.isEmpty())
            return;
        if (auto *mime = urlMimeData(url))
            QGuiApplication::clipboard()->setMimeData(mime);
    });
    connect(openUrl, &QAction::triggered, this, [url] {
        if (!url.isEmpty())
            QDesktopServices::openUrl(url);
    });
    connect(copyTitle, &QAction::triggered, this, [title = result.title] { QGuiApplication::clipboard()->setText(title); });
}

QMimeData *ResultActions::urlMimeData(const QUrl &source)
{
    const auto url = validatedSourceUrl(source.toString(QUrl::FullyEncoded));
    if (url.isEmpty())
        return nullptr;
    auto *mime = new QMimeData;
    mime->setText(url.toString(QUrl::FullyEncoded));
    mime->setUrls({url});
    mime->setData(QString::fromLatin1(SourceUrlClipboardMime), "1");
    return mime;
}
