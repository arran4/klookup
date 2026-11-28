<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Minimal HTTP Example Provider

This directory provides a complete, self-contained **Provider API v2** template for a user-provided KLookup scripted provider.

## Files

- `provider.json` — Provider API v2 manifest (`"apiVersion": 2`) declaring metadata, classification facets, and explicit HTTPS origin allowlist (`api.example.invalid`).
- `main.mjs` — Script implementation exporting `search`, `getPage`, `getDetails`, `getContent`, and `selfTest` via `module.exports`.
- `diagnostics.json` — Offline self-test fixture used by `klookup-provider-check --provider example-minimal-http --offline` and **Settings → Provider health**.

## Installing as a User Provider (Copy or Symlink / GNU Stow)

Copy or symlink/stow this directory into `${XDG_DATA_HOME:-~/.local/share}/klookup/providers.d/` or any directory configured in **Settings → Script packages → Configured user provider directories** (optional dotfiles tools such as GNU Stow, `chezmoi`, or Git checkouts work cleanly), then click **Apply and retry discovery** and review/approve the package.
