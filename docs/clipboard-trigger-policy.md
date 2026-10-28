# Clipboard capture and automatic search policies

Two independent KConfig preferences replace the three legacy booleans:

| Capture (`ClipboardCaptureMode`) | Eligibility |
|---|---|
| Never (0, default) | No passive reads/capture |
| Visible (1) | Main window shown and not minimized; focus is irrelevant |
| Focused (2) | Shown, not minimized, and active |
| Running (3) | Includes background / tray-hidden operation |

| Automatic search (`ClipboardSearchTrigger`) | Event |
|---|---|
| Never (0, default) | Capture only; quietly prepare entry |
| Opened from tray (1) | Hidden → shown through tray Restore/activation |
| Clipboard changed (2) | Clipboard's dataChanged signal |
| Window gains focus (3) | Inactive → active window transition |
| Global shortcut used (4) | Shortcut integration invokes the explicit trigger hook |

The configured capture mode is always checked first, then #56's plain-text
classification. URLs and unsupported input remain excluded from all automatic
triggers. A selected trigger does not implicitly enable capture. Global shortcut
registration is delivered separately by #51; without it the trigger has no source
and monitoring is never enabled implicitly. A deliberate Search clipboard now
command belongs to #51 and can use a documented explicit-search policy instead.

Clipboard changes accepted without the configured trigger update the dashboard
entry without selecting its tab, focusing a widget, showing a window or searching.
Tray/focus/shortcut events read the current clipboard only when permitted at that
moment, rather than searching a stale cached value. Generic show/restore, D-Bus
lookups and hiding the window are not tray-open events. Restoring an already shown
window is not a new open event. Focus-gained events are independent of visibility.

Consecutive automatic searches of identical normalized text are deduplicated
across repeated clipboard and activation events. A newly captured different query
resets that chain. Empty/unsupported clipboard contents do not erase a prepared
query or authorize a new automatic search. Deliberate searches remain unrestricted
by this automatic policy. Capture itself does not add autocomplete history.

## Migration and settings transactions

`ClipboardPolicy::read` is pure; opening/cancelling Settings does not write values.
Application startup migrates missing new keys after the existing QSettings →
KConfig migration. Existing new preferences always win, independently per key.

- MonitorClipboard=false → Never capture and Never automatic search.
- MonitorClipboard=true + ClipboardOnlyWhenFocused=true (or absent) → Focused.
- MonitorClipboard=true + ClipboardOnlyWhenFocused=false → Running, preserving
  the user's previous explicit background-capture permission.
- AutoSearchClipboard=true with monitoring enabled → Clipboard changed;
  otherwise Never. No ambiguous legacy value enables a new activation trigger.
- Invalid new enum values are read conservatively as Never.

Legacy keys remain for old-version compatibility but are ignored once the new
keys exist. Apply/OK writes the new keys; Cancel leaves the stored policy intact.
Runtime events read current KConfig, so Apply takes effect without restarting.

Tests cover the capture/trigger matrix, migration/persistence, invalid settings,
Apply/Cancel/reopen, hidden/visible/background operation, tray-open semantics,
shortcut/focus hooks, URL rejection and repeated-event deduplication. Full Plasma
focus/compositor and global-shortcut integration checks require a desktop session.
