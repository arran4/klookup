# Typed result metadata

`SearchResult` and simple-provider source APIs remain unchanged. The optional
`SearchDetails` response now carries `ResultMetadata`: labelled sections with
multi-valued text, typed links/image references and recursive child categories.
The representation has no manga-specific requirements and can support additional
catalogues and custom HTTP mappings. Rebuild plugins together with the application;
there is no cross-release ABI guarantee for the statically linked core.

Selecting a result fetches only that record's details when the provider supports
it. Results and metadata are cached in the tab. A different selection is rendered
immediately while the previous request completes; its completion cannot replace
the new selection. The current selection's first details are then requested.
Content continuation remains explicit, separate from hit pagination. Failed
details can be retried; Retry-After is scoped to the original provider.

The detail tree supports keyboard navigation and expanding nested categories.
Text is always plain; source links open only after HTTP(S), host and credential
validation. Cover artwork is represented by an optional image link, opened only
on explicit activation. There is no automatic third-party image download or active
HTML rendering. Legacy `additionalData` maps and lists have a recursive escaped
fallback. Untrusted nesting/collection bounds protect the UI (12 levels and 200
children per legacy container; structured rendering stops at depth 16 / 4,000
nodes). Synopses retain their complete supplied text.

## Upstream mapping

The official [MangaBaka OpenAPI schema](https://mangabaka.org/api.json) describes
`SeriesV2` list/default records and `SeriesV2Full`. The lean search response
includes descriptions, staff, publication, ratings, original language and the best
per-language title. `/v2/series/{id}?schema=full` additionally supplies the complete
title list, tags, links and related series. We use the latter only on selection.

MangaBaka tags provide `name_path` (for example `Activities > Leisure > Travel`),
`parent_id`, `is_genre`, relevance and other attributes. The mapping constructs
nested categories, preserving genres, paths and per-tag details. When a path is
missing, it follows available parent IDs with depth/cycle guards. Orphaned and new
tags remain visible under their own names. Metadata includes publishers, source
IDs/ratings, release dates, content rating, formats and source links. Attribution
to MangaBaka and underlying sources remains visible; upstream terms still apply.

The official [MangaDex OpenAPI schema](https://api.mangadex.org/docs/static/api.yaml)
provides localized titles/descriptions, status, original language, year,
demographic, tags and link references in manga attributes. Tags have `content`,
`format`, `genre` and `theme` groups; new group names remain visible. Detail
requests add `includes[]=author`, `artist`, and `cover_art` to resolve relationship
names and cover references in the same request. Publication/publisher information
and ratings unavailable from this endpoint are not fabricated. Known external
source IDs are mapped to their respective catalogue links; unknown references
remain readable text. No extra statistics or per-tag requests are needed.

Synthetic fixtures reflect these schemas and exercise non-English titles, complete
synopses, taxonomy paths, missing parents, cycles, unknown groups, relationships,
unsafe URLs and null/missing fields. UI tests cover lazy selection, caching,
selection changes, nested legacy fields, plain-text safety and typed links.
