Address PR feedback

- Implement AbstractRunner properly with QueryMatch.
- Change runner plugin DBus API to C++ API since it inherits AbstractRunner.
- Use one QNetworkAccessManager for all provider instances to reuse connections.
- Ensure SearchDashboard removes stale identical queries and clamps to 50 max history elements.
- Fix MangaUpdates link construction to use series ID instead of parsing the API payload blindly.
- Convert SearchDashboard history list to a QCompleter autocomplete dropdown on the search bar.
- Restructure SearchTab results list to a QTreeWidget grouped by provider.
- Transform backend providers into dynamic QPlugins.
- Add additional open API providers: AniList (Anime), TVMaze (TV Shows), LyricsOVH (Song Lyrics), DuckDuckGo (General), OpenLibrary (Books).
- Add support for KWallet secure credential storage and authentication logic to SettingsDialog.
- Add additional auth providers: OMDb and TMDb.
