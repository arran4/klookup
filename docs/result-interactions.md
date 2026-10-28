<!--
SPDX-FileCopyrightText: 2026 KLookup contributors

SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Result Actions and Canonical Link Interactions

KLookup provides context menus, keyboard actions, and drag-and-drop integration for search results and source links.

---

## 1. Context Menu Actions (`ResultActions`)

Right-clicking an individual search result row (or pressing the Menu key / `Shift+F10`) opens a context menu with actions bound to a copied snapshot of that result:

- **Refresh Details**: Re-queries details for the selected record without resetting pagination, restarting other provider queries, or disrupting other search tabs. Disabled while a detail request or retry backoff is in progress.
- **Open in Web Browser**: Validates and opens the external canonical URL using `QDesktopServices::openUrl()`.
- **Copy URL**: Copies the validated canonical URL to the system clipboard in both `text/plain` and `text/uri-list` formats.
- **Copy Title**: Copies the plain-text title of the result item.

Provider headers, progress indicators, error rows, and empty placeholders do not expose result actions. Menus are non-blocking and automatically destroyed when closed or when their parent tab is closed.

---

## 2. Source Link Drag and Drop (`SourceLinkButton`)

When a result provides a canonical source URL, the detail view displays an interactive **Open in Browser** source button:

- **Click**: Opens the validated URL in the default desktop browser via `QDesktopServices`.
- **Drag & Drop**: Dragging beyond Qt's native drag-distance threshold initiates a standard `QDrag` operation offering `Qt::CopyAction | Qt::LinkAction`. Both `text/uri-list` (`QMimeData::setUrls`) and plain-text fallback are supplied, allowing smooth dragging directly into web browser tab bars, bookmark managers, text editors, or desktop panels.
- **Click Suppression on Drag**: Initiating a drag operation cleanly suppresses the click event, preventing unintended browser launches.
- **Snapshot Isolation**: The exported URL is snapshotted immediately on mouse press, ensuring that asynchronous detail completions cannot alter the drag payload mid-gesture.

---

## 3. URL Safety and Clipboard Loop Protection

All URL operations adhere to strict security policies:

- **URL Validation (`SourceUrl::validatedSourceUrl`)**: Only HTTP and HTTPS URLs with valid hostnames and no embedded credentials (`user:password@host`) are permitted. Non-web schemes (e.g. `javascript:`, `file:`, `data:`) and invalid URLs are rejected and their actions disabled.
- **Sensitive Data Containment**: Only public canonical URLs (`SearchResult::url`) are exposed. Internal API endpoints, request headers, authentication tokens, and session identifiers are never exported.
- **Clipboard Loop Prevention**: URLs copied via KLookup context actions or buttons include an internal MIME marker so that KLookup's own copy actions never trigger unintended automatic searches.
