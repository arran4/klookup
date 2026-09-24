# KLookup

**KLookup** is a domain-specific lookup tool natively built for KDE Plasma environments utilizing Qt6 and KDE Frameworks 6 (KF6).

Unlike generic web browsers, KLookup provides a focused model for querying structured data across various information providers globally, all from a single window. Its UI centers around a dashboard, a search history list, and dynamically managed query tabs that allow you to view multiple active, non-blocking searches simultaneously. The application embraces KDE's standard desktop integration logic via D-Bus, KConfig, KNotification, and KWallet.

## Supported Providers

KLookup queries a wide variety of services. The current build includes 12 backend providers. Note that provider enablement persists via their stable provider IDs.

| Provider ID | User-Visible Name | Upstream Service/API | Requires Credentials? |
| --- | --- | --- | --- |
| `anilist` | AniList | `graphql.anilist.co` | No |
| `duckduckgo` | DuckDuckGo | `api.duckduckgo.com` | No |
| `github` | GitHub | `api.github.com/search/repositories` | Yes |
| `lyrics_ovh` | Lyrics (OVH) | `api.lyrics.ovh/suggest` | No |
| `mangadex` | MangaDex (Baka equivalent) | `api.mangadex.org/manga` | No |
| `mangaupdates` | MangaUpdates | `api.mangaupdates.com/v1/series/search` | No |
| `myanimelist` | MyAnimeList (via Jikan) | `api.jikan.moe/v4/anime` | No |
| `omdb` | OMDb API | `www.omdbapi.com` | Yes |
| `openlibrary` | OpenLibrary | `openlibrary.org/search.json` | No |
| `themoviedb` | The Movie Database (TMDb) | `api.themoviedb.org/3/search/multi` | Yes |
| `tvmaze` | TVMaze | `api.tvmaze.com/search/shows` | No |
| `wikipedia` | Wikipedia | `en.wikipedia.org/w/api.php` | No |

**Note on Provider Implementation Details:**
*   The source directory currently named `providers/mangabaka` contains a MangaDex-backed substitute. It actually queries MangaDex, not MangaBaka. For the genuine MangaBaka provider and naming cleanup, see [Issue #22](https://github.com/arran4/klookup/issues/22).
*   DuckDuckGo remains present pending [Issue #14](https://github.com/arran4/klookup/issues/14).
*   Game-specific providers remain future work under [Issue #15](https://github.com/arran4/klookup/issues/15).

## Settings and Privacy

KLookup provides configurable clipboard integration and system tray behaviors.

Changes made in the Settings dialog are staged and do not alter the runtime state or persist to storage until you explicitly click **Apply** or **OK**. Clicking **Cancel** fully discards all modifications.

### Clipboard Integration
KLookup can interact with your clipboard to streamline searching:
*   **Monitor Clipboard for Queries:** When enabled, KLookup watches for clipboard changes.
*   **Automatically Search Clipboard Text:** Automatically triggers searches based on new clipboard text.
*   **Only Capture Clipboard When App is Focused:** Restricts clipboard monitoring to when the application has active focus.

**Privacy Implications:** When clipboard monitoring and automatic searching are enabled, KLookup may implicitly transmit your clipboard contents to the aforementioned third-party network providers. There are no built-in safeguards, such as content-filtering or obfuscation, beyond these user-controlled toggles. Please use these features carefully.

### Minimise to Tray
*   **Minimize to System Tray on Close:** Keeps KLookup running in the background.

## Configuration and Credentials

Non-secret persistent application settings and state—such as provider enablement, search history, and open-tab session state—are stored using KDE's `KSharedConfig` and saved to `klookuprc` (migrated from the prior `QSettings` storage).

**Security Warning:** Do not put API tokens or passwords directly into `klookuprc` or any other plaintext files. KLookup uses `KWallet` to securely store and manage all user-provided secret credentials.

## Build Requirements

- CMake 3.16+
- C++17 Compiler
- Qt 6 (Core, Gui, Widgets, Network, DBus, Qml)
- KDE Frameworks 6 (CoreAddons, Config, I18n, Runner, DBusAddons, WidgetsAddons, XmlGui, Wallet, Notifications)

## Building

The recommended KDE development workflow relies on a shared public KDE/Qt Debian rootfs (chroot/proot). Please see [`AGENTS.md`](AGENTS.md) for details on provisioning this environment using `.jules/bootstrap.sh` and running commands using `.jules/run.sh`. Do not use Docker.

To build KLookup and all default backend plugins (the `klookup_with_plugins` target) from the build tree, use the following commands wrapped in `.jules/run.sh` as needed:

```bash
mkdir build && cd build
cmake ..
cmake --build . --target klookup_with_plugins
```

## Running

Launch the main executable from the build directory to start the dashboard:

```bash
./build/src/klookup
```

Alternatively, invoke a quick search directly from the command line:

```bash
./build/src/klookup "Linux kernel"
```

## D-Bus and Single-Instance Behavior

KLookup operates as a single unique instance per session, enforced via `KDBusService`.

If KLookup is already running, launching the executable again with a query will securely forward the request to the existing instance over D-Bus and raise its window.

### Verifying D-Bus and KRunner Activation
The companion KRunner D-Bus plugin allows you to run KLookup directly from KDE's KRunner (`Alt` + `Space` > `:q: query`).

To verify activation in an installed KDE session:
1.  Ensure KLookup is not running.
2.  Run the following command, which matches the installed `org.kde.klookup.service`:
    ```bash
    qdbus6 org.kde.klookup / org.kde.klookup.performSearch "cold start"
    ```
    Alternatively, select a KLookup result in KRunner.
3.  KLookup should start, raise its window, and show the query.
4.  Repeat the process while KLookup is running to confirm that the existing instance receives the new tab.

If activation fails, enable `QT_LOGGING_RULES="klookup.runner.warning=true"` for the runner host and inspect the reported D-Bus error.

### Plugin Diagnostics
Provider plugins are built as CMake `MODULE` libraries and are staged into a common `${CMAKE_BINARY_DIR}/src/providers` directory during development so the `klookup` executable can automatically discover them at runtime without installation.

Diagnostics for provider discovery are opt-in. Start KLookup with the following environment variable to log search paths, candidates, successful loads, and safe loader errors:
```bash
QT_LOGGING_RULES="klookup.plugins.debug=true"
```
These diagnostics do not log credentials.

## Plugin Interface
KLookup features an extensible `SearchProvider` C++ plugin interface (`org.kde.klookup.SearchProvider`). Every backend is an independent, dynamic module loaded using `QPluginLoader`.

This architecture facilitates the addition of third-party provider integrations by simply implementing the pure virtual methods (e.g., `search()`, `name()`, `id()`). While custom integrations are straightforward to create, the application does not make any unsupported ABI stability promises for the internal plugin interface.
