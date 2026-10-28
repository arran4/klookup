# Provider continuation contract

`SearchProvider` retains its existing virtual API, `SearchResult` layout and
legacy `searchCompleted` / `searchFailed` signals. Simple providers require no
source changes. Plugins should be rebuilt with the application; KLookup currently
links the core statically and does not promise cross-release binary compatibility.

Providers can additionally implement `SearchProviderContinuation` and declare
`Q_INTERFACES(SearchProvider SearchProviderContinuation)`. Its versioned IID is
`org.kde.klookup.SearchProviderContinuation/1.0`.

- `requestPage(query, token, requestId)` starts a first page with an empty token.
  Emit `pageCompleted(requestId, SearchPage)` or `requestFailed`. An empty
  `nextToken` is terminal. Tokens are opaque to the UI; providers validate them and
  construct their own endpoint rather than following arbitrary response URLs.
- Results need nonempty stable IDs scoped to the provider. The tab appends new
  IDs in response order and ignores duplicates. Provider groups use IDs rather
  than display names. Existing ID-less simple providers retain their results.
- `supportsDetails()` advertises the independent `requestDetails(resultId, token,
  requestId)` lane. Emit `detailsCompleted` with a complete replacement result
  for the same ID and an optional content continuation. For example, chapter two
  must return the accumulated article/chapter content rather than only a delta.
  An empty detail token is terminal. An empty request token also supports refresh.
- Echo the request ID unchanged. Never emit both the legacy and continuation
  completion for one request. The built-in adapters retain `search()` for callers
  outside SearchTab using request ID zero; normal tab requests use nonzero IDs.
- Page and detail lanes may run simultaneously; coalesce repeated requests in
  each lane. The tab rejects stale completions and caches details without replacing
  a different selected result. Failure retains the original token for retry.
- `requestFailed` may specify a bounded Retry-After delay in milliseconds. Retry
  is user initiated; neither a failure nor a 429 silently starts all providers.
- The tab owns the clone until closure. `cancelRequests()` must abort requests,
  disconnect callbacks and suppress completion/error signals. The tab disconnects
  and cancels before deleting providers, including during pending detail fetches.

`ProviderRequest::getJson` supplies separate reply slots, 30-second transfer
inactivity timeouts, an 8 MiB response bound, same-origin redirects, HTTP/JSON
validation, and 429/503 Retry-After handling. Error messages exclude bodies and
request parameters. No GUI or credential prompts occur in network callbacks.

## Built-in adapters and upstream references

MangaBaka uses the documented [`/v2/series/search`](https://mangabaka.org/developers)
`pagination.next` page parameter (limit 10, maximum 12,000-row window) and deferred
`/v2/series/{id}?schema=full` details. Search keeps the full available synopsis;
rich tag mapping is tracked separately in #43. Attribution remains as documented
in README and the [upstream terms](https://mangabaka.org/developers).

MangaDex uses [`/manga`](https://api.mangadex.org/docs/) limit/offset/total
continuation, stopping before its 10,000-row window, and `/manga/{id}` details.
No total or continuation is fabricated when absent from a response. Wikipedia
continues using the simple search path until its article/formatting implementation.

## Verification

`test_searchcontinuation` covers opaque cursors, numeric offset-like tokens,
no-more/empty results, deduplication, independent provider identities, errors,
Retry-After, stale pages, independent detail continuation, changed selection,
unsupported details, legacy providers, and destruction with a pending request.
The provider fixture tests cover three MangaBaka pages (30 results) through the
actual tab, lazy details, MangaDex offsets, malformed responses, invalid identities,
429, timeout, cancellation, foreign next URLs and oversized responses.
