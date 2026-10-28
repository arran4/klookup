# Wikipedia content and the limited HTML boundary

Wikipedia uses the shared continuation interface: `list=search` returns snippets
and an opaque offset, and selecting one row invokes `action=parse` for that page
ID. No article is prefetched for unselected hits. The supported Action API is used
rather than the deprecated REST routes or the unmaintained TextExtracts extension.
See [search](https://www.mediawiki.org/wiki/API:Search) and
[page parsing](https://www.mediawiki.org/wiki/API:Parsing_wikitext).

`SearchResult` stays plain text and retains its layout. `SearchPage.content`, keyed
by stable result ID, and `SearchDetails.content` opt into `ResultContent` for the
description field only. Title, type, metadata and other provider fields remain
plain text. All continuation plugins must be rebuilt with the application when
these response structures change. Legacy search consumers get readable plain
snippets. Duplicate pages never replace an already-loaded article with a snippet.

The GUI sanitizes every LimitedHtml value when rendering, irrespective of provider
identity. A resource-blocked QTextDocument decodes entities and searchmatch spans;
its text runs are rebuilt as paragraphs, line breaks, bold, italic and validated
HTTP(S) anchors. No source tag/attribute, CSS, image, object, table, script, frame or
resource reference is copied to QTextBrowser. Relative links resolve against an
explicit safe base URL. Credentials and non-web schemes are rejected. The parser's
resource hooks and provider are disabled before parsing, including stylesheet and
local-file loads. This uses existing QtGui/Widgets, with no active browser engine.

Input is capped at 256 Ki UTF-16 units, reconstruction at 4000 blocks/12000 runs
and approximately 1 MiB output; an explicit notice links the user back to the
source when limited. The shared transport caps JSON at 8 MiB and times out at
30 seconds. Complex article layouts are flattened; images do not load. Article
links open externally through the existing validated link handler.

Loading uses the shared detail control. Missing/deleted/malformed pages report a
sanitized error, and empty articles show a source fallback. Selection changes
cannot replace another row's content: completed details cache against the requested
provider/result identity, then the newly selected result is loaded. Tab deletion
cancels both network lanes and suppresses stale callbacks. Attribution remains in
the typed metadata pane and the canonical article is always available.

Deterministic tests cover highlighting and plain descriptions, entities, permitted
emphasis/links, unusual and malicious markup, resource blocking, display bounds,
query parameters/continuation, malformed/mismatched/empty articles, cancellation,
replacement, selection changes, cached articles and tab destruction. Live API and
Plasma visual checks remain separate from these fixtures.
