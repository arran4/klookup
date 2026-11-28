<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Script package approval and installation

Discovery reads manifests and bounded package files only. It does not evaluate
provider code, make HTTP requests or open KWallet. Settings keeps package records
separately from active providers, including disabled, unapproved, unsupported,
missing and failed packages. Native provider settings remain on the Providers
page. Script packages have their own page with identity, version, canonical path,
provenance, origins, secret capabilities and failure status.

Approval and enablement are separate. Review and approve the displayed package,
then Apply/OK to activate it. Cancelling either the review or Settings saves no
approval. Revoke persists until deliberate reapproval; disabling does not erase
approval. Apply and retry discovery reloads the package list and retires operations
in older scripted instances, including open tabs and independent windows.

Approval stores a SHA-256 fingerprint over every package file, canonical package
path and discovery provenance. The manifest is included, so changing origins,
secret capabilities, entry points or metadata requires review again. Legacy
ID-only approval cannot authorize a package; a legacy explicit rejection remains
revoked. Approval does not transfer to a copied path or a user override.

Packages are limited to 512 entries, 5 MiB per file and 16 MiB total; manifests
are limited to 1 MiB. Unreadable files, symlinks and special files are rejected.
At activation the package is read again and compared with its approved snapshot.
Execution evaluates verified scripts in memory via `QJSEngine`, including `host.require()` relative module imports within the verified package snapshot (extensionless and subpath modules included). ES module `import` and `export` statements are unsupported; modules must use `module.exports` and `host.require()`. Bare, absolute, escaping, and dynamic imports are rejected. `QJSEngine` is trusted in-process execution, not an OS sandbox; approval must only be granted to code the user trusts.

Search locations are explicit custom directories, XDG user data, XDG system
data, the configured installation data directory and the executable's adjacent
installed share directory. Repeating an XDG path as an explicit path does not
change its provenance. There is no implicit source-tree or current-directory
search. A broken higher-precedence package remains visible and does not quietly
fall back to a lower-precedence package with the same ID.

A fresh Unix distribution install may activate automatically only from a known
installed data root: `/usr/share`, `/usr/local/share`, the configured data prefix,
or the executable's adjacent `../share`. The package, assets and every parent
must be root-owned and not group/other writable. Arbitrary XDG system paths do
not receive distribution trust. The first activation records the exact approval
fingerprint; subsequent changed contents or permissions require review. Explicit
revocation and saved enablement persist across upgrades. Other platforms require
explicit approval.

Packagers must install both `provider.json` and `main.mjs` from `providers.d/`.
Obsolete installed native libraries matching migrated scripted provider IDs are
ignored automatically by the host.
