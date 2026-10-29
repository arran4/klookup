<!--
SPDX-FileCopyrightText: 2026 KLookup contributors

SPDX-License-Identifier: CC-BY-SA-4.0
-->

# KDE Plasma Autostart-on-Login

KLookup provides an in-app preference under **General Settings**:
**"Start KLookup when logging in"**.

## Integration Architecture

1. **Standards-compliant XDG Autostart**:
   - Manages a per-user desktop entry at `~/.config/autostart/org.kde.klookup.desktop`
     (resolved dynamically via `QStandardPaths::GenericConfigLocation`).
   - Reuses the canonical application identity `org.kde.klookup` and single-instance D-Bus registration.

2. **Startup Visibility Policy**:
   - The desktop entry specifies `Exec=klookup --background`.
   - Starting in background registers the D-Bus service and system tray icon without opening, raising, or focusing the main search window.
   - Users are not interrupted at login by unexpected focus stealing or window popups.
   - KWallet is never opened on login; credential restoration is deferred to explicit user interaction.
   - Clipboard searches are not triggered automatically on login.

3. **External and Administrator Configuration Preservation**:
   - If configured externally (e.g., via KDE Plasma System Settings Autostart KCM or GNOME Tweaks), the in-app checkbox accurately reflects the effective status.
   - Enabling or disabling autostart updates `Hidden=false` / `Hidden=true` and `X-GNOME-Autostart-enabled=true` / `false`.
   - Any custom desktop entry keys, comments, or locale translations previously written by the user or external tools are preserved.
