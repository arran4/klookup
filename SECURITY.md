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

1. **GitHub Private Security Advisory**: Use the "Report a vulnerability" button on the [GitHub Advisories page](https://github.com/arran4/klookup/security/advisories/new) (recommended).
2. **Direct Contact**: If GitHub advisories are unavailable, contact the project maintainer via email at `arran4@users.noreply.github.com` or `arran4@gmail.com` with the subject tag `[KLookup Security]`.

Please include as much information as possible to help us reproduce and resolve the issue:
- Description of the vulnerability and its potential impact.
- Steps to reproduce or a minimal proof-of-concept.
- Affected components (e.g. specific provider plugin, clipboard listener, KWallet credential store, OAuth flow).
- Your operating environment (OS distribution, KDE Plasma version, Qt version).

### What to Expect

- **Acknowledgment**: You will receive an acknowledgment of your report within 48 hours.
- **Assessment**: We will evaluate the report, verify its severity, and keep you informed of our progress.
- **Coordination**: We will coordinate with you on patch release and public disclosure timelines. We kindly request that you do not disclose the vulnerability publicly until a patch is released.

## Security Architecture & Considerations

KLookup is designed with several security layers:

1. **Credential Isolation (KWallet)**:
   - Sensitive credentials (API keys, OAuth refresh tokens, secrets) are never written to plaintext configuration files (`klookuprc`).
   - Secrets are stored in KDE Wallet (KWallet) under the dedicated folder `klookup`.
   - Access to KWallet is explicitly guarded and never automatically prompted during background startup.

2. **Clipboard Privacy**:
   - Automatic clipboard search only operates on plain text, explicitly filtering out URLs unless configured or triggered manually (`Meta+Shift+K` or "Search clipboard now").
   - Clipboard capture respects active focus constraints (`ClipboardOnlyWhenFocused`) to prevent background surveillance.
   - Internal URL copies made by KLookup are tagged with a custom MIME format to prevent feedback loops.

3. **URL Validation & Navigation Safety**:
   - Results and source links strictly validate URLs, enforcing `http` and `https` schemes with full domain validation.
   - Dangerous pseudo-schemes (`javascript:`, `data:`, `file:`, `vbscript:`) are rejected.
   - Embedded HTML is sanitized and rendered without external resource loading or script execution (`QTextBrowser` with disabled external anchors).

4. **OAuth 2.0 PKCE**:
   - OAuth flows use Proof Key for Code Exchange (RFC 7636) with short-lived loopback listeners bound strictly to `127.0.0.1`.
   - State tokens prevent CSRF attacks during authorization handshakes.
