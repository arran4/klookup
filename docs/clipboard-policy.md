<!--
SPDX-FileCopyrightText: 2026 KLookup contributors

SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Clipboard Privacy and Automatic Search Policy

KLookup employs a defense-in-depth, two-tier architecture for clipboard handling to protect user privacy while offering responsive desktop integration:

1. **Content Eligibility (`ClipboardInput`)**: Strict classification and sanitization of clipboard payloads before any processing occurs.
2. **Operational Policies (`ClipboardPolicy`)**: Independent user preferences controlling *when* clipboard monitoring is permitted and *which events* may trigger an automatic search.

---

## 1. Content Eligibility (`ClipboardInput`)

`ClipboardInput` evaluates incoming clipboard data before it can prepopulate the dashboard, trigger an automatic query, or enter search history. This classification makes no network or DNS requests and never stores rejected content:

- **MIME Constraints**: Only explicit `text/plain` MIME data is accepted. HTML-only, URI-list-only, image, and binary formats are ignored. Rich clipboards with plain-text alternatives use only the plain text.
- **Length & Encoding**: Up to 4,096 UTF-16 code units are accepted. Binary control characters, NUL bytes, and Unicode replacement characters are rejected. Normal multiline text, tabs, and Unicode characters are supported.
- **URL & Scheme Exclusion**: Pure URLs and recognizable URI schemes are rejected (including `http`, `https`, `file`, `mailto`, `tel`, `magnet`, `urn`, `data`, and `javascript`). URI validation is conservative; malformed `https://` is excluded.
- **Domain & Address Filtering**: Bare `www.` addresses, common domain suffixes, domains with explicit paths, bracketed IPv6 addresses, and `localhost:<port>` are excluded. Bare alphanumeric names (e.g. `QtCore.QString`, `Dr.Smith`, `readme.md`, dot-separated version strings) remain eligible text unless accompanied by an explicit scheme or path.
- **Local Paths & Email Addresses**: Bare email addresses and local filesystem paths are unsupported. Multiline text containing mixed prose with an inline link remains eligible; a multiline list consisting solely of URLs is rejected.

---

## 2. Capture and Trigger Policies (`ClipboardPolicy`)

Clipboard behaviour is governed by two independent settings in `klookuprc` (`[General]` group):

### Capture Policy (`ClipboardCaptureMode`)
Determines when KLookup is permitted to inspect clipboard changes:

| Mode | Value | Description |
|---|---|---|
| `Never` | `0` (default) | Passive clipboard monitoring is disabled. No automatic reads occur. |
| `Visible` | `1` | Clipboard is inspected only when the KLookup main window is visible and not minimized. Focus is not required. |
| `Focused` | `2` | Clipboard is inspected only when KLookup is visible, not minimized, and actively holds keyboard focus. |
| `Running` | `3` | Permitted whenever the KLookup process is active, including background and tray-hidden operation. |

### Search Trigger Policy (`ClipboardSearchTrigger`)
Determines which desktop event may trigger an automatic search after content passes the capture mode and `ClipboardInput` filters:

| Trigger | Value | Description |
|---|---|---|
| `Never` | `0` (default) | Capture quietly prepares the query field; no automatic search is executed. |
| `OpenedFromTray` | `1` | Triggers a search when restored from the system tray. |
| `ClipboardChanged` | `2` | Triggers a search immediately when eligible clipboard text changes. |
| `WindowGainedFocus` | `3` | Triggers a search when the window transitions from inactive to active. |
| `GlobalShortcut` | `4` | Triggers a search when the global show/focus shortcut is pressed. |

### Invariants
- The configured capture mode is always evaluated first; a trigger never implicitly grants capture permission.
- Automatic searches run silently and never steal focus or activate windows.
- Clipboard search tabs are session-only and are not persisted across application restarts unless explicit history recording is enabled.
- An explicit "Search clipboard now" action (`--search-clipboard` or global shortcut) bypasses automatic filtering and searches current plain text directly on explicit user request.
