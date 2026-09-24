# KLookup

**Find information from several sources without repeating the same search.** Enter a name or topic once and KLookup looks it up across the sources you have enabled. Browse results grouped by source, read the details, and open an interesting result in your browser.

KLookup is a desktop application for KDE Plasma. It brings together lookups for subjects such as books, films, TV shows, anime, manga, songs, and software projects in one window rather than making you search each service separately.

## Get started

1. Open **KLookup** from your application launcher (or [run a development build](#build-and-run-from-source)).
2. Type a title, topic, or project name in the search box and press **Enter** or select **Search**.
3. In the new tab, expand a source's results and select an entry to see its details. Select **Open in Browser** to visit the original page, when a link is available.
4. Keep other searches in their own tabs while you compare results. You can close a tab when you no longer need it.

As you start new searches from the search box, KLookup remembers up to 50 queries and suggests previous entries while you type. It does not currently provide a separate, browsable history list.

## Choose where to search

KLookup searches all **enabled** sources for each query; you do not need to search them one at a time. Open **Configure KLookup** and use the **Providers** page to turn individual sources on or off. Select **Apply** or **OK** to keep those changes.

The current build includes these sources:

| Source | What you can look up | Credentials |
| --- | --- | --- |
| AniList | Anime | None |
| DuckDuckGo | Instant-answer information | None |
| GitHub | Software repositories | Optional personal access token |
| Lyrics (OVH) | Songs and lyrics-related matches | None |
| MangaDex | Manga | None |
| MangaUpdates | Manga series | None |
| MyAnimeList (via Jikan) | Anime | None |
| OMDb | Films and TV titles | API key required |
| Open Library | Books | None |
| The Movie Database (TMDb) | Films and TV shows | API key required |
| TVMaze | TV shows | None |
| Wikipedia | Encyclopedia entries | None |

**API keys:** Searches can run without signing in to GitHub, although you may optionally enter a personal access token. OMDb and TMDb need their own API keys to return results. To enter a key or token, select the provider on the **Providers** page and choose **Authenticate Selected Provider...**. You do not need credentials for the other listed sources.

**Current limitations:** The MangaDex source is a MangaDex-backed substitute, **not** a MangaBaka integration ([issue #22](https://github.com/arran4/klookup/issues/22)). DuckDuckGo is still included, but a more focused lookup source is planned to replace it ([issue #14](https://github.com/arran4/klookup/issues/14)). Additional game-focused sources are proposed in [issue #15](https://github.com/arran4/klookup/issues/15).

## Privacy and everyday settings

**Searches contact third-party services.** When you submit a query, KLookup sends it to each enabled provider. Turn off providers you do not want to contact. The results and available details depend on what those services return.

### Clipboard shortcuts

Clipboard monitoring is **off by default**. In **Configure KLookup → General**, you can choose whether KLookup should:

- **Monitor Clipboard for Queries:** Read new clipboard text and put it in the search box, ready for you to decide whether to search.
- **Automatically Search Clipboard Text:** When monitoring is on, submit new clipboard text as a search without waiting for you to press Search.
- **Only Capture Clipboard When App is Focused:** Limit monitoring to times when KLookup is the active window. This option is enabled by default and matters only when clipboard monitoring is on.

**Be careful with automatic clipboard searching:** It may send copied text, including sensitive text, to every enabled third-party provider. KLookup does not filter or redact that text before searching. Leave automatic searching off if you prefer to inspect the query first.

### Window, tabs, and previous searches

- **Minimize to System Tray on Close** keeps KLookup running when you close its window, provided a system tray is available. You can change this on the **General** page or use **Quit** to exit the application.
- KLookup remembers provider selections, recent search-box queries, and the queries in open tabs between exits. When those tabs are restored, KLookup runs their searches again against the then-enabled providers; restoring tabs is not an offline cache of old results.
- Ordinary settings changes are staged until **Apply** or **OK**. **Cancel** discards those staged changes. Entering a provider credential is different: the credential takes effect immediately, and Cancel does not undo it.

### Credentials

On supported desktop systems KLookup attempts to save API keys and tokens in **KWallet**, rather than in its ordinary settings file. If KWallet is unavailable or the write fails, a credential may work for the current session but not survive a restart; the current credential dialog can still report that it was saved. Do not put API keys or passwords in plaintext application settings.

## Ways to start a search

After installing KLookup and its desktop integration, you can launch it from your application menu. With the companion KRunner plugin installed and available in your Plasma session, you can also use KRunner (`Alt` + `Space`) with `:q: your query` and choose the KLookup result.

If you start KLookup again while it is already running, a query supplied on the command line opens in the existing application window instead of requiring a second window. For example, from a terminal:

```bash
klookup "Linux kernel"
```

## Build and run from source

This section is for contributors and people building KLookup themselves. KLookup uses **C++17**, **CMake 3.16+**, **Qt 6** (Core, Gui, Widgets, Network, DBus, Qml), and **KDE Frameworks 6** (CoreAddons, Config, I18n, Runner, DBusAddons, WidgetsAddons, XmlGui, Wallet, Notifications), along with ECM and their development dependencies.

On a system with those dependencies installed, run the following from the repository root:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target klookup_with_plugins
./build/src/klookup
```

The build-tree executable discovers providers built alongside it; installation is not required just to try that executable. To run its test suite after building:

```bash
ctest --test-dir build --output-on-failure
```

For the repository's reproducible KDE development environment, dependency setup, and the `.jules/run.sh` command wrapper, see [AGENTS.md](AGENTS.md). This development-only workflow is not a prerequisite for using an installed KLookup application. Installing the application and KRunner plugin is required to use the installed desktop launcher, D-Bus activation, and KRunner integration described above.

## Technical reference and troubleshooting

### Settings and providers

Non-secret preferences, search history, and open-tab queries are stored in `klookuprc` using KDE's `KSharedConfig` (with migration from an earlier `QSettings` layout). Provider enabled/disabled preferences use stable provider IDs. The MangaDex-backed provider currently lives in the `providers/mangabaka` source directory, but its actual ID is `mangadex`.

Providers are C++ `SearchProvider` modules discovered at runtime with `QPluginLoader`. An external provider implements the interface in [`src/searchprovider.h`](src/searchprovider.h), including its display name, stable ID, search operation, and per-tab clone. The current plugin interface has **no promised binary compatibility** for third-party builds.

During development, built provider modules are staged next to the executable under `${CMAKE_BINARY_DIR}/src/providers`. KLookup also checks installation-relative and Qt library plugin paths. To inspect discovery paths, successful loads, and loader errors without enabling diagnostic output by default, launch the build-tree executable with:

```bash
QT_LOGGING_RULES="klookup.plugins.debug=true" ./build/src/klookup
```

Plugin discovery diagnostics do not intentionally log credentials. Do not include sensitive search terms, credentials, or clipboard content when sharing diagnostic output.

### D-Bus and KRunner activation

KLookup provides the session D-Bus service `org.kde.klookup`, object path `/`, interface `org.kde.klookup`, and method `performSearch`. In an installed KDE session, with KLookup not already running, you can check cold-start activation using:

```bash
qdbus6 org.kde.klookup / org.kde.klookup.performSearch "cold start"
```

The application should start and open the search; repeating the command should deliver another search to the existing instance. If KRunner activation fails, enable `QT_LOGGING_RULES="klookup.runner.warning=true"` for the **KRunner host** and inspect its reported D-Bus error. The installed `org.kde.klookup.service` file supplies D-Bus activation.
