# KLookup

**KLookup** is a domain-specific, multi-provider lookup tool natively built for KDE Plasma environments utilizing Qt 6 and KDE Frameworks 6 (KF6).

Unlike generic web browsers or monolithic search engines, KLookup provides a **focused lookup silos** model. It enables users to rapidly query structured knowledge services—dictionaries, technical references, encyclopedias, media databases, and developer repositories—directly from a unified, keyboard-friendly desktop interface.

---

## Key Features

- **Multi-Provider Architecture**: Query data concurrently across specialized domain providers.
- **Dynamic Tabs and Independent Windows**: Manage multiple active searches in non-blocking tabs or separate search windows, with detachable result details.
- **Hierarchical Structured Metadata**: Inspect typed result details, nested category trees, and safe image links without third-party tracking.
- **Asynchronous Continuation**: Transparent pagination for multi-page result sets and on-demand article/detail fetching.
- **Recent Activity & Autocomplete**: Instant search dashboard completion with privacy controls.
- **Plasma Desktop Integration**:
  - Native **KRunner** search syntax (`:q: query`) with D-Bus activation.
  - Global desktop shortcuts (`show_search` and `search_clipboard`).
  - Per-user **XDG autostart-on-login** preference starting quietly in the system tray.
  - Context menus, canonical URL copying, and native drag-and-drop links into browsers or bookmarks.
- **Configurable Privacy & Clipboard Policies**: Granular, two-tier control over clipboard inspection and search triggers.
- **Secure Credential Storage**: Native KDE Wallet (`KWallet`) integration and desktop OAuth 2.0 PKCE flow.
- **Modular Plugin Engine**: Extensible C++ plugin architecture loaded dynamically via `QPluginLoader`.

---

## Supported Built-in Providers

KLookup queries a wide variety of public and authenticated services. The application builds with 14 built-in provider plugins:

| Provider ID | Display Name | Domain / Service | Upstream API | Credentials Required? | Capabilities |
|---|---|---|---|---|---|
| `anilist` | AniList | Anime & Manga | `graphql.anilist.co` | No | Search |
| `duckduckgo` | DuckDuckGo | General Web / Instant Answers | `api.duckduckgo.com` | No | Search, disambiguation |
| `github` | GitHub | Code & Repositories | `api.github.com/search/repositories` | Optional (Personal Access Token) | Search, continuation |
| `lyrics_ovh` | Lyrics (OVH) | Song Lyrics | `api.lyrics.ovh/suggest` | No | Search |
| `mangadex` | MangaDex | Manga & Comics | `api.mangadex.org/manga` | No | Search, continuation, structured metadata |
| `mangabaka` | MangaBaka | Manga, novels & comics | `api.mangabaka.org/v2/series/search` | No | Search, continuation, deferred details, format filters, structured metadata |
| `mangaupdates` | MangaUpdates | Manga Database | `api.mangaupdates.com/v1/series/search` | No | Search, continuation |
| `myanimelist` | MyAnimeList (Jikan) | Anime & Manga | `api.jikan.moe/v4/anime` | No | Search, continuation |
| `omdb` | OMDb API | Film & Television | `www.omdbapi.com` | **Required** (API key) | Search, deferred details |
| `openlibrary` | OpenLibrary | Books & Literature | `openlibrary.org/search.json` | No | Search, continuation |
| `spotify` | Spotify | Music & Podcasts | `api.spotify.com/v1/search` | **Required** (OAuth 2.0 PKCE) | Search, deferred details, continuation |
| `themoviedb` | The Movie Database (TMDb) | Film & Television | `api.themoviedb.org/3/search/multi` | **Required** (API read access token) | Search, deferred details, continuation |
| `tvmaze` | TVMaze | Television Schedule & Shows | `api.tvmaze.com/search/shows` | No | Search |
| `wikipedia` | Wikipedia | Encyclopedia | `en.wikipedia.org/w/api.php` | No | Search, Action API parse, safe HTML rendering |

### Provider Implementation Notes & Roadmap
- **MangaBaka and MangaDex**: These are separate providers with independent upstream APIs. The original substitution was resolved under [Issue #22](https://github.com/arran4/klookup/issues/22).
- **DuckDuckGo Backend**: DuckDuckGo's generic backend is scheduled for specialized replacement under [Issue #14](https://github.com/arran4/klookup/issues/14).
- **Game Lookup Backlogs**: Dedicated video game catalog providers are tracked under [Issue #15](https://github.com/arran4/klookup/issues/15).

---

## Authentication and Credential Security

KLookup supports both **Manual API Key/Token** authentication and desktop **OAuth 2.0 Authorization Code with PKCE**:

- **KWallet Integration**: API keys and tokens entered in **Settings → Providers** are stored securely in the local KDE Wallet under folder `KLookup`.
- **Session-Only Option**: Users can choose to use credentials only for the current session without persisting them to disk or KWallet.
- **Never Plaintext Secrets**: Passwords, bearer tokens, and client secrets are never written to `klookuprc` or any other configuration files.
- **OAuth 2.0 PKCE Loopback**: For OAuth-enabled providers such as Spotify, KLookup utilizes a local loopback listener (`http://127.0.0.1:<port>/callback`) and system browser authentication. No client secrets are bundled in the binary.
- **Passive Startup**: Starting KLookup (including login autostart or global shortcuts) never raises unsolicited KWallet password dialogs. Credential restoration is explicit and user-directed.

For detailed developer setup, see the [Provider Authentication Guide](docs/provider-authentication.md) and [Spotify Setup Guide](docs/spotify-provider.md).

---

## Clipboard Privacy and Search Policies

KLookup features a robust, two-tier clipboard privacy model configured in **Settings → General**:

1. **Content Eligibility (`ClipboardInput`)**:
   - Only explicit `text/plain` MIME data up to 4,096 UTF-16 code units is accepted.
   - Pure URLs, recognized URI schemes (`http`, `https`, `file`, `mailto`, `data`, etc.), and bare domain formats are strictly rejected from automatic processing.
   - Internal MIME markers prevent KLookup's own copy actions from feeding back into search triggers.

2. **Capture Policy (`ClipboardCaptureMode`)**:
   - `Never` (default): Passive monitoring is disabled.
   - `Visible`: Captures clipboard only when the main window is visible.
   - `Focused`: Captures clipboard only when the window has active input focus.
   - `Running`: Monitors clipboard while running (including background/tray).

3. **Search Trigger Policy (`ClipboardSearchTrigger`)**:
   - `Never` (default): Quietly prepares the query input without searching.
   - `Opened from tray`: Triggers search upon restoring window from tray.
   - `When clipboard changes`: Triggers immediate search on eligible changes.
   - `When window gains focus`: Triggers search when window becomes active.
   - `When global shortcut is used`: Triggers search via global shortcut.

For details of content classification and operational capture/search policies, see the [Clipboard Privacy and Search Policy](docs/clipboard-policy.md).

---

## Desktop and Plasma Integration

### Global Keyboard Shortcuts
KLookup registers two global desktop actions via KDE's native `KGlobalAccel`:
- **Show/Focus KLookup Search** (`show_search`): Opens or raises the search dashboard and focuses the query field.
- **Search Clipboard Now** (`search_clipboard`): Immediately searches current plain-text clipboard contents (including URLs) on explicit user request.

Shortcuts can be configured via **Settings → Configure Keyboard Shortcuts** or Plasma System Settings. See [Global Shortcuts Guide](docs/global-shortcuts.md).

### KRunner Integration
KLookup installs a D-Bus KRunner plugin (`org.kde.klookup`). Type `:q: <query>` in KRunner (`Alt` + `Space`) to execute a query directly into KLookup:
```bash
# Verify cold-start D-Bus activation:
qdbus6 org.kde.klookup / org.kde.klookup.performSearch "KDE Plasma"
```

### Result Actions and Drag-and-Drop
- Right-click any result row (or press `Menu` / `Shift+F10`) to **Open in Web Browser**, **Copy Canonical URL**, **Copy Title**, or **Refresh Details**.
- The **Open in Browser** source button supports mouse dragging (`QDrag`) offering `text/uri-list` and `text/plain`. Drag links directly into browser tabs, bookmark bars, or desktop folders. See [Result Interactions](docs/result-interactions.md).

### Independent Search and Result Windows
Use **Search in New Window** from the dashboard or the tab context menu to open a search in its own window; select **Open Details in New Window** to detach a result's details. The application remains a single D-Bus instance. Use `klookup --new-window "search terms"` to open a query separately.

### Autostart on Login
An in-app preference in **Settings → General** allows enabling or disabling **Start KLookup when logging in**:
- Installs/updates `~/.config/autostart/org.kde.klookup.desktop` following standard XDG desktop entry specifications.
- Runs `klookup --background` (or `--hidden`) to initialize silently in the tray without stealing window focus or triggering wallet prompts at login.

---

## Build Requirements

- **CMake** 3.16 or higher
- **C++17** compatible compiler (GCC or Clang)
- **Ninja** build system (recommended)
- **Qt 6** (Core, Gui, Widgets, Network, DBus, Qml, Test)
- **KDE Frameworks 6 (KF6)**:
  - `KF6CoreAddons`
  - `KF6Config` (ConfigCore, ConfigGui)
  - `KF6GlobalAccel`
  - `KF6I18n`
  - `KF6Runner`
  - `KF6DBusAddons`
  - `KF6WidgetsAddons`
  - `KF6XmlGui`
  - `KF6Wallet`
  - `KF6Notifications`

---

## Building and Testing

### Recommended Containerized Development Workflow
The repository provides a shared KDE/Qt development rootfs using `.jules/run.sh`. **Do not use Docker.** For initial environment setup and maintenance instructions, see [`AGENTS.md`](AGENTS.md).

To build KLookup and all plugins using the shared environment:
```bash
.jules/run.sh cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
.jules/run.sh cmake --build build --parallel
.jules/run.sh ctest --test-dir build --output-on-failure
```

### Local Host Build
If all required Qt 6 and KF6 development packages are installed locally:
```bash
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build --target klookup_with_plugins
ctest --test-dir build --output-on-failure
```

### Memory and Sanitizer Verification
To build with AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan):
```bash
.jules/run.sh cmake -S . -B build-san -G Ninja -DBUILD_TESTING=ON -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
.jules/run.sh cmake --build build-san --parallel
.jules/run.sh ctest --test-dir build-san --output-on-failure
```

---

## Running and CLI Usage

Launch KLookup from the build directory:
```bash
./build/src/klookup
```

Search directly from the command line:
```bash
./build/src/klookup "Free Software Foundation"
```

Start silently in the background / system tray:
```bash
./build/src/klookup --background
# or
./build/src/klookup --hidden
```

Explicitly search the clipboard without raising the window:
```bash
./build/src/klookup --search-clipboard
```

Open a separate search window with:
```bash
./build/src/klookup --new-window "KDE Plasma"
```

### Plugin Discovery & Diagnostics
Provider plugins are built as CMake `MODULE` libraries and output to `build/src/providers/`. At runtime, `klookup` searches for plugins in:
1. The application's runtime directory (`./providers` or `../lib/klookup/providers`).
2. Standard KDE library directories (`${KDE_INSTALL_PLUGINDIR}/klookup`).

To enable verbose plugin loader diagnostics:
```bash
QT_LOGGING_RULES="klookup.plugins.debug=true" ./build/src/klookup
```

---

## Extending KLookup (Plugin Interface)

KLookup features an extensible C++ plugin interface (`SearchProvider`, `org.kde.klookup.SearchProvider`). New backends inherit from `SearchProvider` and implement core methods:
- `id()`: Unique identifier used in settings and persistence.
- `name()`: User-visible display name.
- `search(const QString &query)`: Executes asynchronous queries via `QNetworkAccessManager`.
- `supportsContinuation()` / `fetchPage()`: Optional multi-page continuation support.
- `supportsDetails()` / `fetchDetails()`: Optional on-demand record detail fetching.

See the [Pagination and Continuation Contract](docs/provider-continuation.md) and [Structured Metadata Guide](docs/structured-metadata.md) for full implementation details.

---

## License and REUSE Compliance

KLookup is free and open-source software compliant with the [REUSE Specification 3.3](https://reuse.software/):

- **Application Code, Plugins, Runner, Tests**: [GNU General Public License v3.0 or later (`GPL-3.0-or-later`)](LICENSES/GPL-3.0-or-later.txt).
- **CMake Build Scripts & CI Definitions**: [BSD 2-Clause Simplified License (`BSD-2-Clause`)](LICENSES/BSD-2-Clause.txt).
- **Documentation**: [Creative Commons Attribution-ShareAlike 4.0 International (`CC-BY-SA-4.0`)](LICENSES/CC-BY-SA-4.0.txt).
- **Icons & Artwork**: [Creative Commons Zero v1.0 Universal (`CC0-1.0`)](LICENSES/CC0-1.0.txt).

Machine-readable license declarations are maintained per-file and in [`REUSE.toml`](REUSE.toml). Canonical license texts are located in [`LICENSES/`](LICENSES/).
