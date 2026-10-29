<!--
SPDX-FileCopyrightText: 2026 KLookup contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Security Policy

## Supported Versions

KLookup is currently in active development. Security patches and fixes are applied to the latest development branch and released versions:

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |
| < 0.1   | :x:                |

## Reporting a Vulnerability

We appreciate the efforts of security researchers and users in making KLookup secure.

If you believe you have found a security vulnerability in KLookup, please report it privately:

**GitHub private vulnerability reporting is the only supported channel.** Use **Report a vulnerability** under [KLookup Security Advisories](https://github.com/arran4/klookup/security/advisories/new) when private reporting is enabled. Do not post exploit details, tokens, or proof-of-concept payloads in public issues or discussions. If the private reporting control is unavailable, the maintainer must enable it before inviting external vulnerability submissions; there is currently no published alternative contact channel.

Please include as much information as possible to help us reproduce and resolve the issue:
- Description of the vulnerability and its potential impact.
- Steps to reproduce or a minimal proof-of-concept.
- Affected components (e.g. specific provider plugin, clipboard listener, KWallet credential store, OAuth flow).
- Your operating environment (OS distribution, KDE Plasma version, Qt version).

### What to Expect

- **Acknowledgment**: Reports will be reviewed as resources permit; no fixed acknowledgment or resolution deadline is promised.
- **Assessment**: We will evaluate the report, verify its severity, and keep you informed of our progress.
- **Coordination**: We will coordinate with you on patch release and public disclosure timelines. We kindly request that you do not disclose the vulnerability publicly until a patch is released.

## Security Architecture & Considerations

KLookup is designed with several security layers:

1. **Credential Isolation (KWallet)**:
   - Built-in provider credential persistence uses KDE Wallet (KWallet) under the dedicated folder `KLookup` instead of writing saved tokens directly to `klookuprc`.
   - Restoring credentials from KWallet is an explicit user action, to avoid surprise wallet prompts during background startup.
   - Declarative custom HTTP sources are still under security review: secret-bearing headers/URL/body templates must not be persisted or exported as plaintext, and must not be described as KWallet-protected until that integration is verified (see PR #82).

2. **Clipboard Privacy**:
   - Passive capture is controlled by separate user-configurable capture modes (Never, Visible, Focused, Running) and search triggers; it is not universally focus-only.
   - Automatic capture uses plain-text classification and filters standalone URLs; an explicitly invoked clipboard search can bypass those automatic filters.
   - Clipboard activity and recent-history persistence have independent settings. Users must understand the privacy implications of the Running and automatic-search modes.
   - KLookup's own URL-copy actions mark clipboard data to avoid feedback loops.

3. **URL Validation & Navigation Safety**:
   - Results and source links strictly validate URLs, enforcing `http` and `https` schemes with full domain validation.
   - Dangerous pseudo-schemes (`javascript:`, `data:`, `file:`, `vbscript:`) are rejected.
   - Embedded HTML is sanitized and rendered without external resource loading or script execution (`QTextBrowser` with disabled external anchors).

4. **OAuth 2.0 PKCE**:
   - OAuth flows use Proof Key for Code Exchange (RFC 7636) with short-lived loopback listeners bound strictly to `127.0.0.1`.
   - State tokens prevent CSRF attacks during authorization handshakes.
