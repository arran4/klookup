<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Dotfiles-Managed JavaScript Providers

KLookup supports discovering scripted JavaScript providers stored in dotfiles repositories and linked into user-configured provider directories (such as GNU Stow, Nix Home Manager, or `chezmoi` setups).

## Configuring Provider Directories

Open **Settings → Script packages → Configured dotfiles / user provider directories**:

1. Click **Add directory…** and choose the directory containing your provider packages (each immediate subdirectory with a `provider.json` represents one provider).
2. Assign an optional **provenance label** (for example `Dotfiles`, `Nix`, or `Stow`) to distinguish its origin in the **Script packages** list.
3. Click **Apply and retry discovery** (or **OK**) to scan configured directories immediately.

Configuration is stored in `klookuprc` under `[ProviderDirectories]` (`Count`, `PathN`, `LabelN`, `EnabledN`).

## Discovery & Rescan Behavior

- **Explicit scanning only:** KLookup scans configured directories at application startup and when you click **Apply and retry discovery** (or invoke `MainWindow::rescanProviders()`). It never spawns filesystem watchers or background polling timers.
- **External files are read-only:** KLookup never writes, chmods, or deletes files inside your dotfiles repository.
- **Precedence & collisions:** Custom paths, enabled `[ProviderDirectories]` entries (in configured order), user XDG data paths, and system XDG paths are scanned in order. If two locations provide the same package `id`, the lower-precedence package is marked **Shadowed by** the active path rather than silently overwriting it.

## Symlink & Trust Rules

- **Supported symlinks:** Both top-level symlinked package directories (`~/.local/share/klookup/providers.d/myprov -> ~/dotfiles/myprov`) and GNU Stow-style regular-file symlinks (`provider.json`, `main.mjs`, etc. inside a package directory pointing to regular files in a stow tree) are resolved and discovered.
- **Rejected symlinks:** Broken/dangling symlinks, cyclic symlinks, internal directory symlinks inside a package directory, and symlinks resolving to non-regular files (FIFOs, sockets, devices) are rejected fail-closed.
- **Fingerprint & re-approval:** KLookup hashes each asset's resolved canonical target path together with its byte contents into `approvalFingerprint()`. Updating a dotfiles script or retargeting a symlink automatically invalidates prior approval so you review changes before execution.
- **Provenance does not bypass trust:** Provenance labels (`Dotfiles`, `Nix`, etc.) are informational only. User and dotfiles providers always require explicit manual approval (no provenance-based auto-trust exemptions are granted).

## Minimal HTTP Example Provider

See `examples/providers/minimal-http/` (and `examples/providers/minimal-http/README.md` in the repository) for a ready-to-copy template with `provider.json`, `main.mjs`, and `diagnostics.json`.
