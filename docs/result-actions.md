# Indexed-result commands

An ordinary result row offers Refresh details, Copy URL, Open in Web Browser and
Copy Title. Mouse context menus select the row under the pointer. Keyboard
Menu/Shift+F10 uses the current result. Provider headings, progress/error rows and
empty states do not expose result actions.

`ResultActions` contains reusable commands with a copied result snapshot, avoiding
accidental use of the previously selected row or mutable global selection. Source
links use the shared `validatedSourceUrl` HTTP(S)/host/no-credentials policy. Copy
URL places the canonical encoded URL in text/plain and text/uri-list without
opening it. It marks its MIME payload so KLookup's own copy does not feed back
into legacy clipboard auto-search. The broader #56 classifier excludes all pure
URLs when integrated; this marker is only the explicit own-copy suppression rule.

Refresh is offered only through #48's detail capability and is disabled while a
detail request or retry delay is active. It requests only the original result ID
with an empty detail continuation (refresh), retaining its selection. Page lists
and unrelated providers are not restarted. Errors remain visible with retry.
Independent result-window commands are intentionally added with #52.

Menus are nonblocking, parented to the tab, and disposed when hidden or the tab
closes. Actions do not retain QTreeWidgetItem pointers. Tests exercise pointer and
keyboard targeting, clipboard MIME/text, safe/unsafe URLs, browser dispatch,
unsupported/busy refresh, error/retry and original-provider isolation. Live desktop
menu placement/appearance remains a Plasma verification item.
