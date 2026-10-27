# Dragging canonical source links

Selected results expose a keyboard-focusable Open in Browser source button. A
normal click uses QDesktopServices; dragging beyond Qt's native start-drag distance
transfers a QDrag with text/uri-list (QMimeData::setUrls) and canonical text/plain
fallback. Copy/Link drag actions are offered for desktop/browser targets. Dragging
suppresses the button click, so it does not also launch a browser.

The source button shares URL validation and MIME serialization with result context
commands. Missing, malformed, non-HTTP(S) and credential-bearing URLs are hidden /
disabled. Only the original external SearchResult.url is exported; no provider API
request, headers, authentication state or internal request token is included. The
URL is snapshotted on mouse press so asynchronously updated details cannot replace
the user's drag target mid-gesture.

The source button supports Ctrl+C and a Copy URL context action. The detail text
also displays the selectable canonical source URL. Own-copy MIME marking retains
the clipboard-loop protection from #55. Widgets, menus and QDrag instances are
owned by the tab/source button and guarded across the drag's nested event loop.

Automated tests cover URI/text MIME contents, valid/unsafe URLs, click vs drag
threshold, no browser launch on drag, original source identity during updates,
subsequent clicks, keyboard copy and source visibility across selections. Actual
drops into Firefox tabs/bookmarks, KDE apps and a Wayland desktop require manual
verification in a graphical session.
