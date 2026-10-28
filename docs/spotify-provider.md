# Spotify Provider

`ProviderSpotify` (`id: spotify`) queries the official [Spotify Web API](https://developer.spotify.com/documentation/web-api) catalogue (`/v1/search` and `/v1/{type}s/{id}`) for artists, albums, tracks, playlists, shows, episodes and audiobooks.

## Developer Application Setup & OAuth 2.0 PKCE

Spotify requires an authenticated bearer token for all Web API requests. KLookup uses the desktop OAuth 2.0 Authorization Code with PKCE flow (`AuthenticationMode::OAuth2Pkce`) and **never bundles a client secret**:

1. Create an application in the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard).
2. Add the loopback Redirect URI:
   ```text
   http://127.0.0.1:43821/callback
   ```
3. In KLookup **Settings → Providers → Spotify**, enter your application's public **OAuth client ID** and click **Connect / reauthorize in browser…**.
4. Public catalogue searches do not request private user-library scopes by default (`scopes: []`). Tokens are kept in memory for the current session or stored in KWallet when opted in, and are only restored from KWallet when explicitly requested in Settings.

## Catalogue Types, Identifiers & Limits

- Default searched types: `artist`, `album`, `track`.
- Additional supported catalogue types (`Provider-spotify` → `Types` in `klookuprc`): `playlist`, `show`, `episode`, `audiobook`.
- Result identifiers are prefixed with their Spotify catalogue type (`<type>:<id>`, for example `track:11dFghVXANMlKmJXsNCbNl`) so multi-type results and deferred detail requests (`GET /v1/{type}s/{id}`) stay unambiguous.
- HTTP `401` responses trigger `authenticationRejected()` so expired tokens are invalidated or refreshed; `403` reports application/account policy restrictions; `429` and `503` honour `Retry-After` backoff without automatic retry loops.
