<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Lyrics.ovh song suggestions

The `lyrics_ovh` package preserves the former native provider's song-suggestion
lookup, stable ID and saved enablement. It returns song titles, artists, albums
and canonical Deezer track links with Lyrics.ovh / Deezer attribution. It does
not fetch full lyrics, advertise pagination or invent lazy full content.

The official [API source and README](https://github.com/NTag/lyrics.ovh) documents
`/suggest/{term}` as a Deezer-backed suggestion endpoint, separate from its
artist/title lyrics endpoint. The [current route](https://github.com/NTag/lyrics.ovh/blob/main/index.js)
proxies a limited Deezer search and returns its `data` list. Requests encode the
whole Unicode query as one path component. Only `https://api.lyrics.ovh` is
permitted; no credentials are requested.

Only explicit user queries fetch. Results have a small, 60-second per-instance
cache; HTTP 429 applies Retry-After without automatic retries. HTTP errors,
invalid JSON and wrong response shapes fail clearly. Incomplete rows are skipped
or retain available artist/album information. Fixtures contain synthetic song
metadata, never copyrighted lyrics or live-service dependencies. `selfTest`
checks the production mapper against approved valid/empty/partial/invalid data.

The native implementation is removed. Obsolete installed native `lyrics_ovh`
modules are ignored, including while the script is disabled or unapproved.
Package managers should remove their own obsolete files on upgrade. Approval,
revocation and enablement use the shared Script packages settings.
