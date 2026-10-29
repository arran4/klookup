<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Open Library

The `openlibrary` ECMAScript package preserves work/book keys, Unicode titles,
authors, canonical Open Library links and first-publication years. It requests
only those search fields and ten records per page. The documented offset/limit
pagination is exposed through Provider API v1; tokens contain validated numeric
offsets, never arbitrary remote URLs. It does not claim full book content.

The official [Search API](https://openlibrary.org/dev/docs/api/search) documents
work/edition keys, selected fields, Unicode queries and pagination. The
[usage guidelines](https://openlibrary.org/developers/api) prioritize human book
lookup, request caching and application identification; default unidentified
traffic is limited to one request per second. KLookup sends a descriptive
User-Agent with its project URL, uses that default per-instance request spacing
without claiming the higher identified/email quota, keeps a small one-minute
query/page cache and honors HTTP 429 Retry-After. There is no background harvest,
bulk synchronization, scraping or automatic retry loop. Attribution identifies
Open Library / Internet Archive.

Only `https://openlibrary.org` is permitted. No credentials are requested.
Malformed JSON, API/transport errors and wrong shapes fail clearly; incomplete
rows retain available metadata while invalid keys/titles are skipped. Synthetic
fixtures and `selfTest` cover production mapping and Unicode. Provider tests
exercise offset pagination, request encoding, caching, rate limiting,
cancellation, MainWindow/SearchTab activation and staged installation.

The former native sources/target are removed; stale native `openlibrary` modules
are ignored while normal C++ plugin support remains. Package managers should
remove their own obsolete files on upgrade. Saved enablement retains the stable
ID, and the shared Settings page controls approval and revocation.
