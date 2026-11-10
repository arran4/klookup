<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Preferred title languages

Settings → Languages manages an ordered list of language codes. Add, remove or
move entries, then Apply. Use UI-language default removes the explicit override.
An English UI starts with English. An empty explicit list uses the source-original
title when supplied, then the first available title. No language or translation
is invented; missing originals stay unknown.

The central registry structurally validates BCP 47 tags and normalizes language,
script and region case, common ISO 639-2/3 and deprecated aliases. It accepts
bounded variants/extensions/private-use, de-duplicates choices, and retains
unknown valid codes as codes. Labels use native language names when Qt knows
them; matching always uses codes, never translated strings. Provider mappings
are explicit: Open Library `/languages/eng` maps to `en`, for example. It does
not treat arbitrary provider strings or country codes as inferred languages.

Selection prefers exact tags, then base-language fallback without crossing an
explicitly requested script, then a declared original, then the first supplied
title. Regional preferences remain ordered and exact variants win. The API is
shared by native and script-result consumers. Providers may supply an optional
`additionalData.localizedTitles` array of `{language, title, original}` objects;
old results remain compatible. At most 128 titles, 32 KiB per title and 16
preferred language tags of at most 64 characters are retained.

MangaBaka title selection now uses this preference list and preserves known
original titles. Result context menus expose All titles when multiple titles
are supplied, in a safe plain-text, selectable/copyable dialog. No API request is
needed to open that dialog. Unknown or missing metadata does not fabricate title
variants. Source metadata remains complete; shared semantic layout and MangaBaka
rich presentation remain separate #134/#135 work.
