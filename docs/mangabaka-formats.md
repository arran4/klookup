# MangaBaka format choices

The provider defaults to **All formats**, including novels. In Settings →
Providers, select MangaBaka to show its options. Uncheck All formats and choose
one or more formats; selecting none also means All. Apply/OK saves the selection,
while Cancel discards drafts. Other providers have no MangaBaka controls.

The optional `SearchProviderOptions/1.0` settings interface keeps this UI owned by
the provider. KConfig stores only nonsecret format identifiers in
`Provider-mangabaka/Formats`. Provider roots take applied changes; each search clone
snapshots them for pagination and detail requests for that tab's lifetime.
Existing searches are not reinterpreted after a settings change.

Verified 2026-10-28 against the [current OpenAPI schema](https://mangabaka.org/api.json)
and live public endpoint: `/v2/series/search` accepts an optional array-valued
`type` with `manga`, `novel`, `manhwa`, `manhua`, `oel`, `other`. Multiple formats
use repeated parameters, for example `type=novel&type=manhwa`; omit `type` for All.
Live requests using `type=novel` and `type=novel&type=manga` returned HTTP 200 with
appropriately different result counts. Bracketed `type[]` and comma-separated
values returned HTTP 400. Pagination rebuilds the original query with the same
format snapshot; it does not follow or trust arbitrary next URLs.

Known types display as Manga, Novel, Manhwa, Manhua, OEL and Other. The exact
upstream value remains in additional information and the typed Format field.
Unknown/new upstream types remain visible verbatim under All formats; invalid
saved filter values are ignored rather than silently hiding the catalogue.
MangaDex is unchanged.

Deterministic tests cover no filter, supported combinations and wire parameters,
page continuity, clone snapshots, Apply/Cancel/reload, clearing the filter, invalid
saved choices, every documented label, a novel and an unknown future type. Live
API checks supplement, not replace, those fixtures.
