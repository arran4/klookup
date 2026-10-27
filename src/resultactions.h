#ifndef RESULTACTIONS_H
#define RESULTACTIONS_H

#include "searchprovider.h"
#include <QAction>

// The clipboard marker prevents KLookup's own Copy URL command from feeding
// the legacy automatic-capture path before the general classifier is integrated.
inline constexpr auto SourceUrlClipboardMime = "application/x-klookup-source-url";

class ResultActions : public QObject
{
    Q_OBJECT
public:
    ResultActions(const SearchResult &result, bool canRefresh, QObject *parent = nullptr);
    QAction *refresh;
    QAction *copyUrl;
    QAction *openUrl;
    QAction *copyTitle;
signals:
    void refreshRequested();
};
#endif
