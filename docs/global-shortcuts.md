# KDE global shortcuts

KLookup registers two stable global actions under component `org.kde.klookup`:

- `show_search`: Show/focus KLookup search. Opens the existing dashboard, focuses
  its query field and emits the configured automatic clipboard shortcut trigger.
  Capture permission and URL classification still apply to that automatic trigger.
- `search_clipboard`: Search clipboard now. An explicit user lookup of current
  plain text, including URLs, independent of automatic capture settings. Empty,
  oversized or binary/unsupported MIME input does not create a query tab. It does
  not enable monitoring or persist the clipboard into autocomplete history.

Both defaults are **unassigned**. Configure them with **Settings → Configure
Keyboard Shortcuts** or the equivalent tray action, using KDE's standard global
shortcut editor. Existing Plasma shortcuts are never stolen programmatically;
the native editor handles conflicts and user assignments. Saved assignments are
loaded by KGlobalAccel on subsequent runs. No X11 grabs or synthetic keystrokes
are used; normal KDE/Wayland focus activation rules apply.

When KGlobalAccel is absent/unsupported, registration remains passive and does
not prompt during background work. Explicit shortcut configuration explains the
unavailable session. A D-Bus service watcher re-registers actions after the daemon
starts or restarts. Headless/offscreen tests never activate the desktop service.

The process must normally be running for registered action callbacks. Installed
launcher actions and `klookup --show-search` / `klookup --search-clipboard` provide
cold-start entry points (including shortcuts assigned to those launcher actions
in Plasma). Both flags are handled on initial startup and by the existing
KDBusService unique-instance activation path. They reuse the current process;
when both explicit flags are supplied, Search clipboard takes precedence. Regular
D-Bus/background searches retain passive behavior. Live desktop-service activation
and compositor shortcut delivery remain desktop verification tasks.

An explicit clipboard invocation reserves its text before raising the window,
preventing a simultaneous focus-trigger automatic search from creating a duplicate
tab. The Show action's automatic trigger runs after activation and is deduplicated
using the shared clipboard policy. Repeated explicit Search clipboard actions are
intentional searches and may create additional tabs.

## Dependencies and verification

Build dependency: KF6::GlobalAccel (`libkf6globalaccel-dev` on Debian). The shared
rootfs already supplies it; CI explicitly ensures it is installed. Native APIs:
[KGlobalAccel](https://api.kde.org/kglobalaccel.html) and
[KShortcutsDialog](https://api.kde.org/kshortcutsdialog.html).

Mock registration tests exercise unavailable service, rejected registration,
reconnection and destroyed actions; component tests cover focus/search commands,
policy triggers, explicit URL lookup and duplicate prevention. A private D-Bus
session test launches the real binary, verifies both activation flags reuse its
PID, and exercises the cold-start Show flag. Real key delivery, conflict dialogs,
persistence in Plasma System Settings and Wayland activation require a desktop.
