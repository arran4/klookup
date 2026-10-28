# Provider authentication

Providers may implement the optional `ProviderAuthentication/1.0` interface and
return an `AuthenticationDescriptor`: None, ManualToken, or OAuth2Pkce. Existing
`supportsAuthentication()` providers are treated as manual-token providers without
source changes. `SearchResult` and the existing credential setter are unchanged;
OAuth providers receive only the current access token through `setCredentials`.
No provider-specific identity (including Spotify) is hard-coded into the core.

An OAuth descriptor supplies HTTPS authorization/token endpoints, fixed scopes,
and a registered loopback callback path/port. Port zero means the provider permits
an ephemeral loopback port; otherwise configure/register the fixed port. KLookup
binds only `127.0.0.1`, uses the system browser and does not embed a client secret.
Settings shows the auth mode, scopes, callback, public client-ID field, current
state, Connect/Reauthorize, Restore, Cancel and Disconnect/Forget actions.

## Protocol and transport

The small public-client implementation follows
[RFC 6749](https://www.rfc-editor.org/rfc/rfc6749),
[PKCE / RFC 7636](https://www.rfc-editor.org/rfc/rfc7636), and
[native applications / RFC 8252](https://www.rfc-editor.org/rfc/rfc8252).
It uses existing Qt Core/Network primitives and adds no new Qt dependency or
declared minimum version. KLookup currently advertises GPLv2 through KAboutData; using Qt
NetworkAuth's GPLv3/commercial module would require a separate licensing decision,
so that dependency is intentionally avoided.

- A fresh 256-bit system-generated state and 384-bit system-generated PKCE
  verifier are generated for each connection. Only S256 is supported. State,
  code, verifier and token response bodies are never logged or persisted.
- Authorization parameters and form bodies use explicit UTF-8 percent encoding.
  Code exchange includes the exact registered redirect and verifier; refresh
  exchange uses the cached refresh token and public client ID, with no secret.
- Callback requests are GET-only, with the exact path and loopback Host header,
  no body/transfer encoding, no duplicate state/code/error parameters, an 8 KiB
  header bound, at most four clients, and a five-second socket deadline. The
  listener closes after a valid callback; it also closes on cancellation,
  expiration or destruction. Replies are generic plaintext with no-store caching.
- State is validated before code exchange. Denied consent, mismatch, missing code,
  timeouts and malformed callbacks produce redacted status messages. Untrusted
  descriptions and request URLs are never forwarded to diagnostic handlers.
- Token endpoints must be HTTPS with default certificate validation. Redirects
  are not followed, preventing code/verifier/refresh-token forwarding. Token
  transfers have a 30-second timeout, a 64 KiB bound and strict JSON validation.
  Only valid bearer token fields reach the session; provider error bodies remain
  private. No general scripting, implicit grant, password grant or embedded
  browser is supported. This is OAuth authorization, not an OIDC identity API.

## Ownership, expiry and lookup behavior

One session is shared by stable provider ID. Tabs retain their own provider clones
but share auth readiness for first pages, subsequent pages and detail requests.
Token changes update those clones. Simultaneous requests coalesce on one refresh;
callbacks whose tab/context was destroyed are discarded. Authorization and wallet
work use generation guards so cancelled or superseded operations cannot overwrite
new state. Providers can emit `authenticationRejected()` on a current-token 401;
this invalidates access and lets Retry refresh without launching a browser.
Providers should suppress stale rejection signals from superseded network replies.

Access is considered expiring 30 seconds early. Refresh happens on demand, using
only in-memory credentials. An idle expiry updates status without making network
requests or showing windows. Rotated refresh tokens replace prior tokens; omitted
refresh tokens retain the prior one. `invalid_grant` requires explicit reconnection.
Disconnect clears session credentials and removes saved credentials; failure to
remove is reported accurately. Disconnect does not revoke upstream grants on the
user's account (the provider's account/developer UI may do that separately).

## KWallet and privacy

Only explicit Settings actions may open/authorize/unlock KWallet. Even opening an
already-unlocked wallet can prompt, as documented by the
[KWallet API](https://api.kde.org/kwallet-wallet.html), so startup and background
lookups do not open it. Use Restore saved credentials after restart when needed.
Background refresh writes only through a previously authorized cached handle; if
that handle is unavailable, refreshed credentials remain session-only with clear
status. There is no plaintext fallback.

Keys are namespaced by base64url-encoded stable provider ID and strategy, under the
KLookup wallet folder. OAuth bundles bind tokens to provider ID, endpoints, client
ID and scopes. Different IDs/names cannot alias credentials. Explicit manual-token
restoration supports legacy raw keys for conventional old provider IDs; Forget
removes both forms. Pending saves are cancelled before disconnect or replacement,
so a delayed wallet unlock cannot resurrect forgotten credentials.

KConfig stores only nonsecret client IDs. Search history, result metadata,
clipboard, exports and logs never receive auth tokens. The Save in KWallet option
can be disabled for session-only use. Connect/Restore/Forget are explicit immediate
actions; Settings Apply/Cancel controls the nonsecret field drafts and ordinary
preferences, rather than undoing an already-requested browser authorization.

## Verification and remaining desktop checks

Deterministic tests use an intercepted browser, real local callback sockets,
scripted HTTPS token replies and a memory-only credential-store double. They cover
PKCE hash/form parameters, connect/search/refresh/disconnect, concurrent refresh,
state mismatch, denied consent, invalid callbacks, expiry, refresh rotation,
provider/client isolation, legacy manual keys, malformed responses, wallet errors,
pending-save cancellation, object destruction, auth-mode UI, public-config-only
persistence and redaction of adversarial error descriptions.

Actual KWallet dialogs/storage, provider-registered app setup and system-browser
return behavior require a signed-in Plasma desktop and an appropriate provider
client ID. No live accounts or credentials are needed by the automated suite.
