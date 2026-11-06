<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# CheapShark PC game deals

`cheapshark` is a credential-free ECMAScript provider for explicit PC-game title
lookups. Search displays ten store/deal records with game/store/deal IDs, sale
and regular prices labelled **USD**, available savings/release/score metadata,
and CheapShark attribution. A game may have several store-specific deals.
Search only requests lightweight deals and the store reference list. Selecting
a result lazily requests the documented deal details and lowest recorded price.
No invented publisher/platform/full-content data is advertised.

The official [API documentation](https://apidocs.cheapshark.com/) and its
[published collection](https://apidocs.cheapshark.com/api/collections/530355/RWTspajP?environment=530355-45dba261-4cf3-4cf2-aaa8-cc00b8ae4533&segregateAuth=true&versionTag=latest)
were reviewed for request parameters, data shapes and terms. Prices are always
USD and date fields use Unix seconds. Deal IDs are already URL-encoded; KLookup
normalizes them once to avoid broken double encoding. All deal links use the
required `https://www.cheapshark.com/redirect?dealID=...` page. The host never
fetches that redirect page: it is a user-facing link, not an automation endpoint.

Requests use the documented `/api/1.0/deals` and `/stores` endpoints, an explicit
HTTPS origin and a descriptive KLookup User-Agent. No API key or secret is
required. A small one-minute user-query cache and 24-hour fulfilled store-reference
cache avoid repeat calls. Pending store promises are not cached, so cancelling a
query cannot poison later searches. HTTP 429 uses Retry-After with no automatic
retry loop. No request runs at startup, and there is no catalog synchronization.
Store-reference failures can still show grounded store IDs; rate limits are
propagated. This focused first page does not advertise pagination.

Synthetic fixtures and offline selfTest cover actual production mapping, empty,
partial, invalid and detail data, including an encoded deal ID. Dedicated
provider tests cover Unicode query encoding, errors, cooldown, cancellation,
lazy details, caching, approval/enablement and MainWindow/SearchTab activation.
The protected staged install verifies assets and first activation without seeded
approval. Ordinary CI never calls CheapShark or its redirect pages.

This adds a game source; it does not remove DuckDuckGo or complete issue #14.
The README integration update is coordinated in existing PR #78. Issue #15 can
close only after the resulting provider documentation/README and tested game
lookup are included in the final batch.
