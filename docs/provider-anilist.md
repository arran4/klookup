<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# AniList anime lookup

The credential-free `anilist` package preserves anime-only search, stable IDs,
English/romaji title fallback, plain short descriptions, canonical AniList links,
status, episode count and score. It sends a fixed GraphQL document plus separate
POST JSON variables; user text cannot change the document. Ten lightweight
results are requested. Pagination and full content are not advertised.

The official [GraphQL guide](https://github.com/AniList/docs/blob/master/docs/guide/graphql/index.md)
requires POST requests to `https://graphql.anilist.co` with query and variables.
Only that HTTPS origin is permitted. The [rate-limit guide](https://github.com/AniList/docs/blob/master/docs/guide/rate-limiting.md)
documents changing quotas, burst limits, HTTP 429 and Retry-After. KLookup caches a
small set of explicit user-query results for one minute, applies Retry-After
cooldown and does not automatically retry or synchronize catalogs. GraphQL rate
errors inside HTTP 200 also apply a cooldown.

GraphQL errors are checked independently of HTTP success. Missing/null data,
wrong response shapes and malformed JSON fail explicitly. Usable partial data
retains available rows with a visible metadata warning. Null optional fields
remain absent, rather than becoming invented zero values. Descriptions use the
common renderer and do not contain active upstream HTML.

The official [terms](https://github.com/AniList/docs/blob/master/docs/guide/terms-of-use.md)
permit noncommercial uses, restrict bulk collection/data storage and competing
tracking services, and specify naming/commercial-use rules. This is an unofficial
lookup integration in KLookup, not a media tracker or database mirror. Attribution
and links identify AniList. Distributors of commercial variants should assess
those terms for their own use.

Synthetic fixtures include Unicode, valid/empty/partial/error/null data and the
production mapper's offline selfTest. Provider tests verify POST variables,
HTTP/transport/GraphQL/rate failures, caching, cancellation, approval/enablement,
MainWindow/SearchTab rendering and fresh staged installation. Native sources and
target are removed; obsolete installed native `anilist` modules are ignored.
Package managers should remove their own obsolete files on upgrade.
