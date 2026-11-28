<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# User-Provided JavaScript Providers

KLookup supports discovering scripted **Provider API v2** JavaScript providers placed in your user data directory (`${XDG_DATA_HOME:-~/.local/share}/klookup/providers.d`) or linked into explicit user-configured directories (for example via GNU Stow, `chezmoi`, Nix Home Manager, or Git checkouts).

## Configuring User Provider Directories

Open **Settings → Script packages → Configured user provider directories**:

1. Click **Add directory…** and choose the directory containing your provider packages (each immediate subdirectory with a `provider.json` manifest represents one provider).
2. Assign an optional **provenance label** (for example `User`, `Dotfiles`, `Stow`, or `Nix`) to distinguish its origin in the **Script packages** list.
3. Click **Apply and retry discovery** (or **OK**) to scan configured directories immediately.

Configuration is stored in `klookuprc` under `[ProviderDirectories]` (`Count`, `PathN`, `LabelN`, `EnabledN`).

## Discovery & Rescan Behavior

- **Explicit scanning only:** KLookup scans configured directories at application startup and when you click **Apply and retry discovery** (or invoke `MainWindow::rescanProviders()`). It never spawns filesystem watchers or background polling timers.
- **External files are read-only:** KLookup never modifies, chmods, or deletes files inside your source directories.
- **Precedence & collisions:** Custom paths, enabled `[ProviderDirectories]` entries (in configured order), user XDG data paths, and system XDG paths are scanned in order. If two locations provide the same package `id`, the lower-precedence package is marked **Shadowed by** the active path rather than silently overwriting it.

## Symlink & Trust Rules

- **Supported symlinks:** Both top-level symlinked package directories (`~/.local/share/klookup/providers.d/myprov -> ~/providers/myprov`) and Stow-style regular-file symlinks (`provider.json`, `main.mjs`, etc. inside a package directory pointing to regular files) are resolved and discovered.
- **Rejected symlinks:** Broken/dangling symlinks, cyclic symlinks, internal directory symlinks inside a package directory, and symlinks resolving to non-regular files (FIFOs, sockets, devices) are rejected fail-closed.
- **Fingerprint & re-approval:** KLookup hashes each asset's resolved canonical target path together with its byte contents into `approvalFingerprint()`. Editing a script or retargeting a symlink automatically invalidates prior approval so you review changes before execution.
- **Provenance does not bypass trust:** Provenance labels are informational only. User-provided and symlinked packages always require explicit manual approval (no provenance-based auto-trust exemptions are granted).

## Minimal HTTP Example Provider

See `examples/providers/minimal-http/` for a ready-to-copy **Provider API v2** template with `provider.json`, `main.mjs`, and `diagnostics.json`.
