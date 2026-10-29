<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Provider diagnostics v1

Settings → Provider health offers individual retry, Check all providers and
cancellation. Opening Settings or starting the application does not run checks.
The dedicated `klookup-provider-check` tool avoids GUI single-instance dispatch
and always writes `klookup.diagnostics.v1` JSON. Its exit status is 1 for failed
provider checks, 0 otherwise; unsupported checks are never reported as passes.

- `klookup-provider-check --all`: passive preflight; enumerate installed native
  modules and XDG packages, including configured missing/disabled packages.
  Validate manifests, integrity, approval and static module syntax/imports without
  executing provider code, HTTP or KWallet access. A preflight pass establishes
  only those properties, not a successful real query.
- `klookup-provider-check --provider tvmaze --offline`: explicitly execute approved
  provider-specific fixtures. Script hosts deny network and credential access,
  including during module initialization. Fixtures come from the verified private
  snapshot, not a later unverified file read. The normal operation CPU budget and
  a one-second deadline apply. Native checks run in an isolated helper process
  with a 1.5-second deadline; an unavailable helper is a dependency failure.
- `klookup-provider-check --all --live`: explicitly permit only a provider-exported
  `liveCheck` through normal origin permissions and limits. There is no guessed
  endpoint, automatic query, credential restore or wallet prompt. Providers
  without this optional operation report unsupported. No live probe is scheduled
  at startup or during ordinary CI.

Each report includes provider ID/type/version, status, diagnostic code, a fixed
safe message, remediation, UTC timestamp, elapsed milliseconds, mode,
capabilities and Qt/protocol dependencies. Provider error/log text is suppressed
in diagnostic reporting, so fixture failures do not expose tokens or queries.
Aggregate JSON includes pass/warn/fail/skipped/unsupported counts and a separate
shared-component inventory. Services without an implemented active shared probe
(IPC, authentication/network brokers, renderer) explicitly report unsupported;
a provider failure is not presented as a shared-service failure.

Native providers may implement the optional `org.kde.klookup.ProviderCheck/1.0`
interface without altering existing SearchProvider plugin vtables. Its
`selfTestV1()` returns `{passed: true|false}` and must be deterministic and offline.
Old plugins remain usable and report unsupported for this operation. GitHub's
example checks the production repository parser against synthetic valid, empty,
malformed, partial and API-error fixtures, without using stored authentication.
Native code is trusted; its offline contract is not an OS network sandbox.

Scripts optionally export `selfTest(fixture)` with the same passed boolean;
`diagnostics.json` is an approved, bounded package asset. TVMaze supplies an
example checking its production search mapper, Unicode, empty and wrong-shape
responses. New builtin script packages must supply dedicated fixtures/checks;
the provider integration tests exercise them without upstream dependencies.

The all-target build and klookup_with_plugins enumerate test executables instead
of maintaining a selective dependency list. Configuration fails for a test source
without an executable target or a test executable absent from CTest (helpers are
explicitly identified by the `_helper` suffix). A regression test adds both types
of omissions to a temporary checkout and verifies configuration fails.

Full production scripted credential provisioning/restore/revoke remains #85.
No diagnostics check claims to complete live Plasma or KWallet verification.
