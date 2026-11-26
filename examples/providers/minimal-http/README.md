<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Minimal HTTP Example Provider

This directory provides a complete, self-contained template for a dotfiles-managed
scripted KLookup provider.

## Files

- `provider.json` — Provider manifest declaring metadata, classification facets, and explicit HTTPS origin allowlist (`api.example.invalid`).
- `main.mjs` — Script implementation exporting `search`, `getPage`, `getDetails`, `getContent`, and `selfTest`.
- `diagnostics.json` — Offline self-test fixture used by **Settings → Provider health → Offline fixtures**.

> **Note:** On `main` prior to PR #209 (`#204`), `main.mjs` uses static ESM exports (`export { ... }`). Once Provider API v2 (`#204`) lands, update exports to `module.exports = { ... }` for CommonJS evaluation.

## Symlink / GNU Stow Usage

Copy or stow this folder into a configured directory in **Settings → Script packages → Configured dotfiles / user provider directories** (for example `~/.dotfiles/klookup/example-minimal-http` symlinked into `~/.config/klookup-providers`), then click **Apply and retry discovery** and review/approve the package.
