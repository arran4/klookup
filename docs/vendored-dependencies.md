<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Third-Party and Vendored Dependencies Policy

KLookup minimizes vendored code and prohibits implicit runtime or build-time downloading of executable third-party packages. Any temporary vendored dependency must have documented provenance, license compatibility, integrity verification, and an explicit elimination/system-packaging path.

## Current Vendored Inventory

There are currently **zero** third-party libraries vendored in `third_party/`.

### Historical Record: Eliminated Dependency — Acorn (`third_party/acorn/acorn.js`)

Acorn (`8.15.0`, MIT) was previously vendored to inspect ES module `import` edges before calling `QJSEngine::importModule()` (see #167). As documented in [`docs/acorn-elimination-research.md`](acorn-elimination-research.md) (#204), KLookup eliminated Acorn completely by switching JavaScript providers to KLookup Provider API v2 classic-script evaluation (`QJSEngine::evaluate()`) over immutable in-memory verified package bytes, where ES `import`/`export` syntax and dynamic `import()` are structurally rejected by Qt's QV4 parser and optional relative `host.require()` is confined strictly to pre-hashed package files.
