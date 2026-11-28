<!--
SPDX-FileCopyrightText: 2026 KLookup contributors

SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Shipped ECMAScript Providers and Provider API v2

KLookup runs approved ECMAScript packages in memory inside a resource-bounded `QJSEngine` using **Provider API v2**.

## Provider API v2 Contract

A scripted package directory contains a `provider.json` manifest, an entry file (`main.mjs` by default), and any local helper modules or diagnostic fixtures (no symlinks inside the package, up to 512 entries, 5 MiB per file, 16 MiB total).

- **Module format:** Scripts execute in strict function scope with `module.exports` and `host.require("./relative-path")`. Static/dynamic ES module `import` / `export` syntax and `import.meta` are unsupported and rejected.
- **`host` interface:**
  - `host.apiVersion` (`2`)
  - `host.fetch(url, options)` — bounded HTTP GET/POST to declared `permissions.origins` only (5 MiB compressed/decompressed limit, 10s default deadline)
  - `host.getSecret(key)` — retrieves declared secrets where host policy permits
  - `host.require(specifier)` — loads another file from the verified in-memory package snapshot
  - `host.log(message)` — redacted debug logging
- **Exported functions:** `search(query)` (required), plus optional `getPage(query, pageToken)`, `getDetails(resultId, contentToken)`, `getContent(resultId, contentToken)`, `selfTest(fixture)`, and `liveCheck()`.

### Minimal Working Example

`provider.json`:

```json
{
  "apiVersion": 2,
  "id": "example_http",
  "name": "Example HTTP",
  "description": "Minimal Provider API v2 lookup example",
  "version": "1.0.0",
  "license": "GPL-3.0-or-later",
  "runtime": "javascript",
  "entry": "main.mjs",
  "permissions": {
    "origins": ["https://api.example.org"],
    "secrets": []
  },
  "capabilities": {
    "pagination": false,
    "details": false,
    "separateContent": false
  },
  "classification": {
    "version": "1.0.0",
    "facets": ["topic:general"],
    "requirements": []
  }
}
```

`main.mjs`:

```javascript
// SPDX-License-Identifier: GPL-3.0-or-later

function search(query) {
    var url = "https://api.example.org/search?q=" + encodeURIComponent(query);
    return host.fetch(url).then(function (resp) {
        if (!resp.ok) {
            throw new Error("HTTP " + resp.status);
        }
        return resp.json();
    }).then(function (data) {
        if (!Array.isArray(data.items)) {
            return [];
        }
        return data.items.map(function (item) {
            return {
                id: String(item.id),
                title: String(item.title || ""),
                description: String(item.summary || ""),
                url: String(item.url || "")
            };
        });
    });
}

module.exports = { search: search };
```

Validate any package before installing via `klookup-provider-check --all` or `klookup-provider-check --provider <id> --offline`.

---

## Shipped ECMAScript Packages

KLookup installs five built-in scripted packages under `providers.d/`:

| Package ID | Upstream Service & Documentation | Permitted Origins | Default State | Notes & Upstream Terms |
| --- | --- | --- | --- | --- |
| `tvmaze` | [TVMaze API](https://www.tvmaze.com/api) | `https://api.tvmaze.com` | Enabled | Show search and lazy show summary/metadata (`details`); HTML stripped via common renderer. |
| `lyrics_ovh` | [Lyrics.ovh API](https://github.com/NTag/lyrics.ovh) | `https://api.lyrics.ovh` | Enabled | Song suggestions and Deezer links via `/suggest/{term}`; does not fetch copyrighted full lyrics. |
| `openlibrary` | [Open Library Search API](https://openlibrary.org/dev/docs/api/search) | `https://openlibrary.org` | Enabled | Book search with numeric offset pagination; honors 1 req/s shared rate spacing and HTTP 429 cooldowns per Open Library guidelines. |
| `anilist` | [AniList GraphQL API](https://github.com/AniList/docs/blob/master/docs/guide/graphql/index.md) | `https://graphql.anilist.co` | Enabled | Unofficial anime lookup using a fixed GraphQL query + POST variables; noncommercial lookup only per AniList terms, with 60s query cache and 429 backoff. |
| `cheapshark` | [CheapShark API](https://apidocs.cheapshark.com/) | `https://www.cheapshark.com` | Disabled | Opt-in USD PC-game deals (`/deals`, `/stores`) and lazy deal details; links point to upstream `redirect?dealID=...` pages without fetching them automatically. |

Upstream API terms, rate limits, and attribution notices apply to each provider's data.
