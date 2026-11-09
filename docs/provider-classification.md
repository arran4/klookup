<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Provider classification

Classification describes a **provider**, never a result's story tags, user
location, approval or source enablement. Native providers expose
`SearchProvider::classification()`; custom native metadata uses `setClassification()`
(usually during construction). This access is nonvirtual and stored as QObject
data so the unversioned legacy native-provider vtable is unchanged. Application
`cloneForSearch()` copies metadata and enablement even for old plugins. Provider API v1 and both adapters carry the
same descriptor. Script packages expose it declaratively in `provider.json`:

```json
"classification": {
  "version": "1.0.0",
  "facets": ["topic:games", "platform:pc", "currency:USD"],
  "requirements": [],
  "labels": {}
}
```

Facets accept multiple `topic`, `audience`, `platform`, `region`, `currency`
and `language` IDs. IDs remain stable across translated labels. Topics and
platforms use lowercase slugs; regions use lowercase ISO territory codes,
currencies uppercase three-letter codes, and languages BCP 47 shaped codes.
`audience:general` describes broad relevance and can coexist with topics.
`currency:USD` means **USD pricing**, never US-only availability. No shipped
provider receives a geographic restriction without documented grounds; no
locale, IP lookup or geographic inference occurs.

The independent requirements list accepts `requirement:login`, `subscription`,
`territory`, `language` and `terms` (each with the `requirement:` prefix). These
are descriptive applicability notices, not executable credential methods or
authorization. Existing approval, permissions and credential checks remain
mandatory. Changing metadata never enables a provider or initiates a query.
An unsupported requirement is rejected rather than silently dropped. New
eligibility policies can consume this contract separately.

Known IDs have translated host labels. Optional labels map an included ID to
language-code/text pairs, e.g. `"topic:science": {"en": "Science", "fr": "Sciences"}`.
Display chooses exact UI locale, then language, then English, then the host's
label/code fallback. All labels are plain text; tooltips escape markup.
Unknown topics in supported namespaces remain visible with a code fallback;
unknown facet namespaces, fields and unsupported versions are rejected.

Version 1.0.0 limits each list to 32 entries, IDs to 64 characters, labels to
64 IDs, translations to 16 per ID and text to 160 characters. Duplicates are
removed in order. Invalid descriptors leave packages visible as invalid,
without evaluating code or falling back to a lower-precedence duplicate.
Legacy manifests remain valid and use the host's shipped-ID classification
(or an empty descriptor for unknown providers). Discovery reads bounded package
files only, including for unapproved packages; it never runs entry points or
requests network data to obtain metadata.

Settings displays the descriptor for native and script sources, and live result
groups carry it as typed data with a readable tooltip. Interactive display
filtering and source eligibility controls are separate follow-up changes.

A binary fixture compiles a real module against the pre-change SearchProvider
header, then loads it into the new host and verifies search/clone dispatch and
metadata copying. Provider API v1 metadata also uses nonvirtual QObject data;
no JavaScript executes to read a descriptor.
