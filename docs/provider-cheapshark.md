<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# CheapShark PC game deals

`cheapshark` is a credential-free ECMAScript provider for explicit PC-game title
lookups. It is **disabled by default**. Enable it in **Settings → Script packages**
to include US-dollar game deals. Approval and enablement are separate choices;
approving or rediscovering the package does not enable it. Existing explicit
enabled/disabled preferences survive migration and restart. USD is a pricing
currency, not a claim of US-only availability. There is no geolocation or price
conversion. Disabled packages perform no search or reference-list requests.

Search displays ten store/deal records with game/store/deal IDs, sale
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

## Default-policy audit

CheapShark is the only default-off exception: USD-priced PC-game store deals
are an opt-in special-purpose source. The other shipped native and scripted
sources retain their existing default. No documented geographic restriction was
established for those sources; authentication requirements remain separate.
All restoration, Settings, diagnostics and missing-package paths use the shared
provider-default policy. Only stored `_Enabled` keys migrate from QSettings;
absent values remain absent. Applying unrelated settings does not turn inherited
defaults into explicit provider overrides.
