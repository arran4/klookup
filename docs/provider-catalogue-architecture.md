# Secure Opt-In Third-Party Provider Catalogue Architecture (#181)

## 1. Separation of Responsibilities

| Layer | Responsibility | Trust Boundary |
| --- | --- | --- |
| **KLookup Core (`arran4/klookup`)** | Host runtime (`ProviderHostService`), manifest/fingerprint verification (`ProviderPackage`), catalogue index parser & safe installer (`ProviderCatalogue`), explicit approval UI | Trusted local application; never bundles third-party provider code |
| **Independent Catalogue Repository** | Reviewed JSON index (`catalogue.v1.json`) listing immutable versions, repository URLs, SHA-256 digests, declared API origins/secrets, compatibility ranges, licence/provenance, and revocations | Reviewed metadata PRs; zero executable code in the index |
| **Third-Party Provider Repositories** | Independent GitHub/source repositories hosting `provider.json` + JS modules | Untrusted until explicitly installed, SHA-256 verified, and separately approved by the user |

## 2. Zero-Execution Discovery & Two-Step Consent

1. **Browse:** KLookup reads the catalogue JSON index without downloading or executing any third-party provider code.
2. **Inspect:** Users view provider ID, author, licence, repository URL, API compatibility, requested network origins, and requested secrets before installing.
3. **Explicit Install:** Installing downloads or copies the immutable release package, verifies its SHA-256 digest against the catalogue entry, validates `provider.json` against declared origins/ID/version, backs up any prior version for rollback, and writes to the user package directory.
4. **Separate Approval & Enable:** Newly installed third-party providers remain **unapproved and disabled by default**, especially when requesting network origins, secrets, or execution. Changing permissions or content hash invalidates `ProviderPackage::approvalFingerprint()` and requires fresh user approval.

## 3. Threat Model & Mitigations

- **Compromised upstream plugin repo:** Package SHA-256 mismatch against the immutable catalogue entry aborts installation before moving into the active package directory.
- **Silent permission escalation on update:** `ProviderCatalogue` verifies manifest origins/ID/version against the catalogue entry, and `ProviderPackage::approvalFingerprint()` invalidates stored approval whenever content or permissions change.
- **Malicious/compromised plugin post-install:** Catalogue revocations (`revoked: true` or `revokedIds`) flag installed versions; users can disable, remove, or `rollbackPackage()` to the previous verified backup snapshot.
- **Offline / air-gapped environments:** Local/offline providers never require a remote catalogue; local/mock `file://` catalogues are supported only when explicitly enabled for local testing or offline mirrors.
- **Parser boundary (#167 Acorn):** `third_party/acorn/acorn.js` remains in core until an equivalent verified module-parsing boundary exists.

## 4. Open Maintainer Questions

1. Should the independent catalogue repository live under `arran4/klookup-catalogue` with PR review + CI schema/digest checks?
2. Should catalogue index releases be signed (e.g., Sigstore/minisign/GPG) in addition to HTTPS + per-package SHA-256?
3. What moderation/namespace-ownership policy should govern provider IDs (e.g., reverse-DNS or maintainer-reviewed registry)?
