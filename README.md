# KLookup

**Find information from multiple sources without jumping between websites.**

KLookup is a native KDE Plasma 6 desktop application for looking things up in encyclopedias, books, software projects, music, film and TV, anime, and manga. Enter a question, title, or keyword once and explore results from the sources you choose, all in one place.

Rather than trying to replace your web browser or act as a general-purpose search engine, KLookup brings **specialised information sources together** in a keyboard-friendly desktop interface. Open the original website whenever you need more.

## Why use KLookup?

- **One search, several sources.** Look up a topic across enabled providers instead of repeating the same search on different websites.
- **Results with context.** Browse results grouped by source, inspect descriptions and available details, then follow the original link if you want to read further.
- **Keep your place.** Use tabs for different searches, open separate search or detail windows, and revisit recent activity.
- **Quick access from Plasma.** Search from KRunner or configure your own global shortcuts without changing applications.
- **Control what gets searched.** Choose your providers, decide whether KLookup may read the clipboard, and control when an automatic search happens.

For example, you could research *Linux* using Wikipedia and GitHub, check a novel in Open Library, compare film and television listings, or find a series across anime and manga catalogues. The available results depend on which providers are enabled and whether a service requires credentials.

<!-- Screenshots: add a main search/results image here, followed by an optional
     result-details image and a KRunner/Plasma integration image.
     Keep images illustrative; do not add broken image links before assets exist. -->

## What can you look up?

| Interest | Available sources |
| --- | --- |
| General knowledge and quick answers | Wikipedia, DuckDuckGo |
| Software projects | GitHub repositories |
| Books | Open Library |
| Movies and television | TMDb, OMDb, TVMaze |
| Music and lyrics | Spotify, Lyrics.ovh |
| Anime and manga | AniList, MyAnimeList (via Jikan), MangaUpdates, MangaDex, MangaBaka |

KLookup includes **14 built-in provider plugins**. You can enable or disable them in **Settings → Providers**. Not every service supports the same kind of information: some return brief matches, while others offer further details or more results on demand.

Most sources work without an account. **OMDb and TMDb require API credentials; Spotify requires an OAuth connection.** GitHub can also use a personal access token. Configure these through the provider settings; you do not need to put secrets in a configuration file.

## Getting started

KLookup is currently available from its source repository; there are no published GitHub release binaries yet. See [Build from source](#build-from-source) below to get started.

Once installed or built:

1. **Open KLookup** from your application launcher, or run `klookup`.
2. **Type a query** in the dashboard and choose **Search**. Matches appear grouped by provider.
3. **Select a result** to inspect its description and available details. When supported, use **Load full details** or **Load more results** for additional information.
4. **Open the original source** in your browser when you need the full website. Result context menus also offer actions such as copying a title or URL.
5. **Start another search** to keep exploring in tabs, or use **Search in New Window** when you want two searches side by side.

The dashboard also offers recent activity and search completion, making it easier to return to something you explored earlier. Recent-activity and clipboard-history options can be controlled separately in settings.

## Works with your KDE desktop

- **KRunner:** With KLookup and its runner plugin installed, open KRunner (usually **Alt+Space**) and enter `:q: KDE Plasma` to send a lookup to KLookup.
- **Global shortcuts:** Assign keys for **Show/focus KLookup search** and **Search clipboard now** through **Settings → Configure Keyboard Shortcuts**. KLookup does not claim default key combinations.
- **System tray:** Optionally keep KLookup available in the tray when you close its window.
- **Result actions:** Open original links in a browser, copy source URLs or titles, or drag a source link into another application where supported.
- **Multiple windows:** Open a search in a separate window or detach result details while keeping other searches available.

KLookup uses one application instance per desktop session, so sending it a new search normally reuses the running application.

## Privacy and provider access

**Clipboard monitoring is off by default.** You can leave it off, allow capture only while the window is visible or focused, or allow it while KLookup runs. Separately choose whether captured text merely fills the search field or triggers a search on an event such as a clipboard change. Automatic capture filters out URL-only text and other unsupported input; deliberate searches can still include URLs.

In **Settings → General**, you can control recent activity and whether captured clipboard activity is recorded or saved between sessions. Clipboard activity recording and persistence are off by default.

Some services require authentication. KLookup can use **KWallet** for saved credentials or keep them for the current session only. Connecting or restoring credentials is an explicit action. Your searches are still sent to whichever third-party providers you enable, under those services' own terms and privacy policies.

See the [provider authentication guide](docs/provider-authentication.md) and [clipboard capture policy](docs/clipboard-trigger-policy.md) for further detail.

## Help, feedback and contributing

If a source returns no results, check that its provider is enabled and, if applicable, that its API key or connection has been configured. Some providers can be temporarily unavailable or rate-limited by their upstream services.

For bugs, feature requests, or questions, use [GitHub Issues](https://github.com/arran4/klookup/issues) or [Discussions](https://github.com/arran4/klookup/discussions).

---

## For developers and packagers

The sections below cover building, testing, internals and provider development. You do not need these details to use an installed KLookup application.

### Build from source

You will need CMake 3.16+, a C++17 compiler, Qt 6 (Core, GUI, Widgets, Network, D-Bus and QML), KDE Extra CMake Modules, and KDE Frameworks 6 (including Runner, GlobalAccel, Config, Wallet, Notifications, DBusAddons, CoreAddons, I18n, WidgetsAddons and XmlGui). Ninja is recommended.

With the development dependencies installed:

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build --target klookup_with_plugins
ctest --test-dir build --output-on-failure
./build/src/klookup "KDE Plasma"
```

The `klookup_with_plugins` target builds the application and included providers. To install KLookup and its desktop/KRunner components, use the normal CMake installation step appropriate to your distribution and install prefix. A fresh Plasma session may be needed for KRunner to pick up the installed plugin.

Project-specific development environment instructions are in [AGENTS.md](AGENTS.md).

### Command-line use

```sh
klookup                     # Open the dashboard
klookup "Linux kernel"      # Run a lookup
klookup --new-window "Qt"   # Open another search window
klookup --show-search       # Show/focus search
klookup --search-clipboard  # Explicitly search current clipboard text
klookup --background        # Start without raising a window
```

When running directly from the build tree, replace `klookup` with `./build/src/klookup`.

### Provider architecture and diagnostics

Built-in sources are separate C++ plugins loaded by Qt. Additional providers can implement the `SearchProvider` interface, with optional pagination, detail fetching and authentication capabilities. Provider discovery supports both build-tree and installed plugin paths.

Useful implementation references:

- [Provider pagination and continuation](docs/provider-continuation.md)
- [Structured result metadata](docs/structured-metadata.md)
- [Provider authentication](docs/provider-authentication.md)
- [Global shortcuts](docs/global-shortcuts.md)
- [Result actions](docs/result-actions.md)
- [Source-link drag and drop](docs/source-link-drag.md)

To inspect provider loading without logging credentials:

```sh
QT_LOGGING_RULES="klookup.plugins.debug=true" ./build/src/klookup
```

For KRunner activation diagnostics and manual verification, see [global shortcuts](docs/global-shortcuts.md) and the installed D-Bus service `org.kde.klookup`.

### Licensing and attribution

KLookup is free and open-source software. Its application, providers and tests are licensed under **GPL-3.0-or-later**; build and CI files use **BSD-2-Clause**, documentation **CC-BY-SA-4.0**, and original icons **CC0-1.0**. See [LICENSES/](LICENSES/), [REUSE.toml](REUSE.toml), and [LICENSE](LICENSE) for details.

Data provided by MangaBaka and other upstream services remains subject to their respective terms. See the [MangaBaka API documentation](https://mangabaka.org/data/api) for its attribution and data conditions; the project license does not relicense third-party content.
