<p align="center">
  <img src="icons/256x256/apps/org.kde.klookup.png" alt="KLookup application icon" width="128">
</p>

# KLookup

**Look something up, not somewhere else.** KLookup brings specialised search sources together in a native **KDE Plasma 6** desktop application. Search once, explore results from multiple providers, read available details, and open a source website only when you need to.

Find encyclopaedia articles, books, software projects, films, shows, music, anime, manga and PC-game deals without repeating a search in a dozen browser tabs.

KLookup is designed around **focused lookups**: rather than trying to become another browser or a full web search engine, it asks dedicated information sources for relevant results and presents them together.

> **Project status:** [v0.1.1](https://github.com/arran4/klookup/releases/tag/v0.1.1) is published, including a Linux amd64 archive alongside source builds. See [Planned improvements](#planned-improvements) for future roadmap proposals.

## At a glance

| If you want to… | KLookup helps you… |
| --- | --- |
| Check a concept, title or name | Search enabled encyclopaedia and catalogue sources together |
| Research a library or project | Find matching GitHub repositories, with links back to GitHub |
| Explore a film, album, book or series | Compare catalogue entries and inspect the details those services provide |
| Keep references open while you work | Use search tabs, separate windows and detachable result details |
| Start a lookup without opening the app first | Use KRunner or a configurable Plasma global shortcut |
| Limit what leaves your computer | Choose providers, disable clipboard capture, and explicitly control authentication |

### Why KLookup?

- **Less context switching.** Enter one query and examine results from different specialised sources in a single desktop window.
- **Keep the structure of the source.** Where supported, results include descriptions, metadata, related categories and further detail on demand rather than a list of bare links.
- **Stay in control.** Enable only the providers you want. Some work without accounts; others can be connected through their own authentication flows.
- **Follow your own workflow.** Keep several searches in tabs; split work between windows; use a result's original URL when you need the full website.
- **Integrate with KDE.** Launch searches through KRunner, configure global shortcuts and optionally keep the application in the system tray.
- **Privacy choices are visible.** Clipboard monitoring and automatic clipboard searches are opt-in and configured separately.

## Screenshots — current interface (November 2026)

These images depict **KLookup running on KDE Plasma 6** (November 2026) and show the core interface layout (tabs, provider-grouped results, resizable panes, script package approval, diagnostics, shortcuts, and recent activity).

### Search results and source details

[![KLookup search results for "One piece" grouped by provider, showing MangaBaka details and structured metadata](docs/images/klookup-search-results.webp)](docs/images/klookup-search-results.webp)

*Searching for “One piece”: provider-grouped matches on the left, with the selected MangaBaka summary, source URL, and structured detail tree on the right.*

### Providers and plugins

| Native provider settings | Script packages |
| --- | --- |
| [![KLookup native provider configuration showing individual enable/disable checkboxes](docs/images/klookup-provider-settings.webp)](docs/images/klookup-provider-settings.webp) | [![KLookup script packages settings showing JavaScript providers and approval status](docs/images/klookup-script-packages.webp)](docs/images/klookup-script-packages.webp) |
| Native lookup providers can be enabled or disabled individually | JavaScript providers with source paths, permissions, status, and approval controls |

KLookup supports both native C++ lookup plugins and JavaScript script packages (`.mjs`) loaded from `providers.d` directories. Native sources can be toggled on or off individually. Script packages require explicit user review and approval (verifying source location, declared origin permissions, and package fingerprint) before execution, displaying their readiness status and offering revoke or retry controls. Note that scripted KWallet credential provisioning remains incomplete ([#85](https://github.com/arran4/klookup/issues/85)); shipped script packages require no credentials.

### Provider diagnostics

[![KLookup provider health tab showing native and scripted providers with diagnostic check controls](docs/images/klookup-provider-health.webp)](docs/images/klookup-provider-health.webp)

*Provider health diagnostics: inspect native and scripted provider readiness, run passive preflights or explicit checks, and view diagnostic status.*

The **Provider health** tab allows diagnosing both native and scripted providers. Passive preflight checks inspect provider registration and configuration without sending network traffic or requesting wallet access, while explicit checks can be run on demand to report status, unsupported operations, or connectivity failures.

### Settings and integration

| General preferences | Keyboard shortcuts |
| --- | --- |
| [![KLookup general preferences dialog showing tray, clipboard capture, and search history settings](docs/images/klookup-general-settings.webp)](docs/images/klookup-general-settings.webp) | [![KLookup keyboard shortcut configuration showing application actions and global shortcuts](docs/images/klookup-keyboard-shortcuts.webp)](docs/images/klookup-keyboard-shortcuts.webp) |
| Tray behavior, clipboard capture policies, automatic search, and recent activity options | Application actions and configurable Plasma global shortcuts |

Clipboard monitoring and automatic searches are strictly controlled by user preferences and are turned off by default. Configurable Plasma global shortcuts (such as raising search or looking up clipboard content) are opt-in and unassigned by default, ensuring existing desktop hotkeys are never overridden without user configuration.

<details>
<summary>Recent activity dashboard</summary>

[![KLookup dashboard showing recent activity with searched query and viewed result](docs/images/klookup-recent-activity.webp)](docs/images/klookup-recent-activity.webp)

*Recent activity after a search and result selection, showing the query, timestamp, and viewed item with options to revisit, remove, or clear history.*

</details>

All screenshots depict KLookup itself running on KDE Plasma; external third-party website interfaces are not included.

## Getting started

### 1. Install or build KLookup

If you have a distribution package, use the package's normal installation procedure. Otherwise, follow [Build from source](#build-from-source) below. GitHub [releases](https://github.com/arran4/klookup/releases) include a Linux amd64 archive. Check its compatibility with your distribution; Gentoo users can install from the separate [arrans_overlay](https://github.com/arran4/arrans_overlay) source ebuild once it has been updated to the desired release.

The installed application should be available as **KLookup** from your KDE application launcher, or as `klookup` from a terminal.

### 2. Run your first search

1. Open **KLookup** to see the dashboard.
2. Type a title, word, phrase or topic — for example, **KDE Plasma**, **Pride and Prejudice**, or **Spirited Away**.
3. Select **Search**. KLookup asks the enabled providers and displays their matches in source-specific groups.
4. Choose a result to read its description and the information the provider supplies.
5. Where available, select **Load full details** for additional information or **Load more results** to retrieve more matches.
6. Use **Open in Browser** or the result context menu when you want the original website, or copy the title/source URL for later.

Each provider has its own catalogue and matching rules. Searching a topic does not guarantee that every source will return something, and a search provider is not necessarily a general-purpose web engine.

### 3. Keep researching

- Start a **new search** to preserve your earlier query in another tab.
- Choose **Search in New Window** from the dashboard to work with two searches side by side.
- Right-click an existing search tab to open that search in its own window.
- Use **Open Details in New Window** for a selected result you want to leave visible.
- Return to recent searches or viewed results through the dashboard's **Recent activity** area, when enabled.

KLookup can restore query tabs between application sessions. Result history, autocomplete and clipboard-capture activity have separate privacy and retention behaviour; see [History and clipboard controls](#history-and-clipboard-controls).

## Example uses

### Look up a term while writing or reading

Search a subject in **Wikipedia** or your configured **Local Documentation** directories, or use **DuckDuckGo's Instant Answer API** for short definitions. You can also opt in to automatic clipboard/selection lookups when KLookup is visible (scoped to verified local sources unless you explicitly approve specific online providers in **Settings → General**).

### Compare books and media catalogues

Find a book via **Open Library**, or search a film/show in **TMDb**, **OMDb** and **TVMaze**. You can compare the available entries, follow the upstream links and keep results open while researching.

### Explore anime, manga and related works

Search across **AniList**, **MyAnimeList (Jikan)**, **MangaUpdates**, **MangaDex** and **MangaBaka**. The available source metadata varies; some entries expose additional series details, categories, publication information or related links when selected.

### Research code

Search **GitHub repositories** from KLookup and open the original repository when you need code, issues or documentation. This is repository discovery, not a promise of full GitHub code search.

### Look up music

Find catalogue information through **Spotify** (after connecting it) or find song suggestions with artist/album metadata and Deezer links through **Lyrics.ovh**. KLookup is a reference/lookup application, not an audio player.

## Search providers

KLookup ships **16 lookup providers**: **11 native C++ modules** (including **Local Documentation** for offline Markdown/HTML/plain-text scan roots) and **5 ECMAScript packages** (TVMaze, Lyrics.ovh, Open Library, AniList and CheapShark). Providers can be enabled or disabled independently via **Settings → Source selection**, **Settings → Providers**, or **Settings → Script packages**. CheapShark is **off by default** (USD-priced PC-game deals). Explicit saved choices survive upgrades.

| Area | Provider | What it looks up | Access |
| --- | --- | --- | --- |
| Local files | Local Documentation (`localdocs`) | Plain text, Markdown, and rendered HTML files in configured directories | Offline (local) |
| Knowledge | Wikipedia | Encyclopaedia entries and available article details | No account |
| General | DuckDuckGo | Instant answers and supported related information | No account |
| Development | GitHub | Repositories | Public search; token optional |
| Games | CheapShark | PC-game store/deal prices in USD and CheapShark deal links | No account |
| Books | Open Library | Books and publication records | No account |
| Film/TV | The Movie Database (TMDb) | Film and TV catalogue entries | API token required |
| Film/TV | OMDb | Film and TV catalogue entries | API key required |
| Television | TVMaze | Television series information | No account |
| Music | Spotify | Artists, albums and other supported catalogue records | OAuth connection required |
| Music | Lyrics.ovh | Song suggestions, artist/album metadata and Deezer track links | No account |
| Anime | AniList (unofficial) | Anime catalogue entries | No account |
| Anime/manga | MyAnimeList (Jikan) | Anime and manga information exposed by Jikan | No account |
| Manga | MangaUpdates | Series records | No account |
| Manga | MangaDex | Manga and related metadata | No account |
| Manga/novels | MangaBaka | Supported series, types and detail records | No account |

**Choosing sources:** Open **Settings → Providers** for native plugins, local documentation scan directories, and authentication; **Settings → Script packages** to review package identity, permissions, and approve/revoke/enable scripted sources; or **Settings → Source selection** for facet-based policies. Disabled and failed packages remain visible; changing approved code or permissions requires review again.

For **Local Documentation (`localdocs`)**, only plain text (`.txt`), Markdown (`.md`/`.markdown`), and already-rendered HTML (`.html`/`.htm`) files inside explicitly configured scan roots are searched; binary/native formats such as StarDict, dictd, WordNet, roff man pages, `.info`, or `.qch`/`.dsidx` remain out of scope (see [Planned improvements](#planned-improvements)).

### What the results look like

KLookup groups results by provider. A result may show a title, summary, type, original URL and other fields. On selection, sources that support it can retrieve fuller detail such as nested metadata. Additional result pages are loaded on demand, rather than requiring every provider to return an entire catalogue at once.

Useful result actions include:

| Action | Benefit |
| --- | --- |
| Select a result | Inspect its summary and available detail fields |
| Load full details | Request extra content only for the selected record, where supported |
| Load more results | Continue through matching entries for a provider, where supported |
| Open in Browser | Use the upstream website for the complete source |
| Copy Title / Copy URL | Reuse a reference without manually retyping it |
| Refresh Details | Fetch the selected result again, where supported |
| Open Details in New Window | Keep a reference visible independently of the original tab |
| Drag a source link | Transfer a canonical source URL to another compatible application |

These are **source-dependent** features: for example, a provider without a full-detail endpoint cannot fabricate one.

## KDE Plasma integration

### KRunner

With KLookup **and its KRunner plugin installed**, open KRunner (often **Alt+Space**) and type:

```text
lookup Linux kernel
```

The **`lookup `** prefix explicitly selects KLookup and prevents unwanted
results during unrelated KRunner searches. A bare query such as `Linux kernel`
will not trigger KLookup. KDE's `:q:` in the syntax metadata means *search term*;
it is a help placeholder, **not** text to type. Choose the KLookup result and
press Enter to send the term to the application; typing alone does not submit
the query to a remote provider.

Selecting the KLookup result sends the query to the app, starting it if needed through D-Bus activation. A running instance can receive new searches without spawning a separate service for each request.

If KRunner does not find the plugin immediately after installation, make sure the
**KLookup** runner is enabled in the KRunner settings. Restart KRunner or try a
fresh Plasma session and consult [Troubleshooting](#troubleshooting). This does
not bypass the separately tracked cold-start activation issue.

### Global shortcuts

KLookup exposes two configurable Plasma actions:

- **Show/focus KLookup search** — raise the application and focus its search field.
- **Search clipboard now** — explicitly look up current plain-text clipboard content.

Both are **unassigned by default**; no existing desktop shortcut is taken over. Configure them through **Settings → Configure Keyboard Shortcuts** or Plasma's shortcut settings.

A separate **Look up selected text** hotkey and compact popup are **planned, not shipped**; see [selected-text popup](https://github.com/arran4/klookup/issues/88).

### In-application keyboard shortcuts

| Keys | Action |
| --- | --- |
| **Ctrl+W** | Close the active search tab. The Dashboard cannot be closed |
| **Ctrl+T** | Start a new search by focusing the Dashboard input |
| **Ctrl+L** | Focus the Dashboard search input |
| **Ctrl+Tab** | Switch to the next tab, wrapping around |
| **Ctrl+Shift+Tab** | Switch to the previous tab, wrapping around |

These shortcuts operate only inside the KLookup window, not globally. Detached
search windows accept **Ctrl+W** to close that window without closing KLookup.
Global shortcut actions remain opt-in and unassigned by default.

### System tray and windows

KLookup can remain available from the tray when its window is closed, depending on your settings. The tray icon uses a palette-coloured symbolic silhouette for contrast on light and dark Plasma panels; the full-colour application icon remains in the launcher and window. Use **Pop Out to New Window** to move an existing search tab with its loaded results and pending requests. **Return to Main Window** reattaches it without searching again. Closing a popped-out window closes that search. **Open Details in New Window** remains a separate reference view; it keeps the search tab open. The dashboard can still start a new independent search.

You can also choose to **Start KLookup automatically on login** in **Settings → General**; see [Autostart configuration](docs/autostart.md). When enabled, KLookup starts quietly in the background without raising windows or triggering unrequested searches.

## History and clipboard controls

### Search history and recent activity

KLookup can offer completion based on previous deliberate searches and show recent searched queries or viewed results on its dashboard. You can revisit, remove or clear recent items. These are separate from the tabs that may be restored across application sessions.

In **Settings → General**, you can choose whether recent activity is shown, and whether clipboard-derived entries may be recorded or kept between sessions.

### Passive clipboard capture and automatic lookup (opt-in)

By default, **KLookup does not passively monitor the clipboard/selection or run automatic searches**. You can configure both independently in **Settings → General**:

| Setting | Options | What it controls |
| --- | --- | --- |
| **Automatic capture source** | Clipboard, Primary selection | Which desktop selection buffer is monitored when permitted |
| **Capture clipboard while** | Never (default), window visible, window focused, or while running | When KLookup is permitted to inspect clipboard text |
| **Automatically search clipboard** | Never (default), opened from tray, clipboard changed, window gains focus, or global shortcut used | Which event may run an automatic search |
| **Enable automatic lookup when a KLookup window is visible** | Checkbox (off by default) | Runs debounced automatic searches into a reusable session-only tab while KLookup is open, honoring char/word limits |

Automatic capture rejects URLs, email-like text, local file paths, password-manager secret hints, and multiline/binary input; eligible text runs only against verified local sources (such as `localdocs`) unless **Allow automatic online lookups** and specific online providers are explicitly enabled in Settings. See [Clipboard policy and privacy](docs/clipboard-policy.md) and [Recent activity](docs/recent-activity.md).

A standalone lightweight selection popup without an open KLookup window remains tracked as future desktop work under [#88](https://github.com/arran4/klookup/issues/88).

## Credentials, privacy and network access

Many sources need no authentication. Those that do can be connected from **Settings → Providers**:

- **OMDb / TMDb:** supply the appropriate API key or token for the service.
- **Spotify:** use a browser-based OAuth 2.0 PKCE connection. A Spotify developer application/client ID may be required; see the [Spotify setup guide](docs/spotify-provider.md).
- **GitHub:** a personal access token is optional for supported requests.

KLookup supports storing provider credentials in **KWallet**, or retaining them only for a session. Restoring persisted credentials is an explicit action; starting the app does not silently open wallet prompts. Do not put API keys or passwords directly into `klookuprc`.

**Important:** Searches to enabled *online* providers transmit their query to those third-party services. KWallet protects stored credentials but does not turn a remote search into an offline one. If a query must remain local, do not submit it to the current online provider set.

KLookup avoids executing active untrusted HTML as application code and validates source URLs before opening or copying them. Upstream data, usage limits, licensing and privacy policies still apply.

See [provider authentication](docs/provider-authentication.md) for a deeper technical description.

## Planned improvements

Specialized local document format proposals (already-rendered exports only; no native StarDict, dictd, WordNet, roff man-page, Info node, or SQLite docset parsers):

- User-configured plain-text / Markdown / HTML directories are **implemented in `localdocs`** ([#91](https://github.com/arran4/klookup/issues/91), [#98](https://github.com/arran4/klookup/issues/98)).
- Additional specialized local source presets remain open for future work: offline text dictionaries ([#86](https://github.com/arran4/klookup/issues/86)), pre-rendered man pages ([#87](https://github.com/arran4/klookup/issues/87)), Info HTML/text exports ([#90](https://github.com/arran4/klookup/issues/90)), KDE handbooks ([#92](https://github.com/arran4/klookup/issues/92)), Zeal/Dash HTML ([#93](https://github.com/arran4/klookup/issues/93)), Qt HTML docs ([#94](https://github.com/arran4/klookup/issues/94)), GTK-Doc/Devhelp HTML ([#95](https://github.com/arran4/klookup/issues/95)), tldr Markdown ([#96](https://github.com/arran4/klookup/issues/96)), package descriptions ([#97](https://github.com/arran4/klookup/issues/97)), and text word-list spelling suggestions ([#99](https://github.com/arran4/klookup/issues/99)).

## Troubleshooting

**A provider returns no results.** Check that the provider is enabled, your search matches its subject area, and any required API credentials are configured. An upstream service may also be unavailable or rate-limited.

**KRunner does not show KLookup.** Check that both the application *and KRunner plugin* were installed, not merely built. A new Plasma session can be necessary for discovery. Installation paths depend on your distribution and CMake prefix.

**A shortcut does not work.** Shortcuts have no assigned keys by default. Check Plasma's shortcut configuration, conflicts and whether the running desktop supports global activation.

**Clipboard searches do nothing.** Passive capture, automatic triggering, and **Enable automatic lookup when a KLookup window is visible** are off by default, and automatic lookups target verified local sources (`localdocs`) unless specific online providers are explicitly allowed.

**Authentication stops working after restart.** Saved credentials are not automatically restored through a surprise wallet prompt. Open provider settings and explicitly restore/reconnect. The upstream provider may also require fresh authorisation.

**Local Documentation returns nothing.** Add one or more readable local directories containing `.txt`, `.md`/`.markdown`, or `.html`/`.htm` files under **Settings → Providers → Local Documentation**.

**Reporting a bug:** Include the KDE/Qt version, which provider was enabled, steps to reproduce and relevant redacted diagnostics. **Never attach tokens, API keys or private copied text.** Use [Issues](https://github.com/arran4/klookup/issues) for bugs and [Discussions](https://github.com/arran4/klookup/discussions) for questions and proposals.

---

## For developers, contributors and packagers

The rest of this README is technical. It covers build requirements, plugin architecture, verification and integration contracts. Ordinary end users need only the sections above.

### Technology and build requirements

KLookup is written in **C++17** using **Qt 6**, **KDE Frameworks 6** and **CMake**. Building requires:

- CMake **3.16+**, a C++17 compiler (GCC or Clang) and KDE Extra CMake Modules (ECM).
- Qt 6: **Core, Gui, Widgets, Network, DBus, Qml**.
- KF6: **CoreAddons, GlobalAccel, Config, I18n, Runner, DBusAddons, WidgetsAddons, XmlGui, Wallet, Notifications**.
- zlib development headers/library (`zlib1g-dev` in the shared Debian development rootfs).
- Ninja (recommended; another CMake-supported generator can be used).
- Test tooling when building with `BUILD_TESTING=ON`.

Consult [AGENTS.md](AGENTS.md) for repository-specific environment notes, rather than treating those as installation requirements for end users.

### Build from source

From the checkout root:

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/src/klookup "KDE Plasma"
```

The `klookup_with_plugins` target includes the built-in providers. For a local installation, use your distribution's preferred prefix and packaging method; for example, a normal CMake installation after configuring an appropriate `CMAKE_INSTALL_PREFIX`:

```sh
cmake --install build
```

Installation to a protected system prefix may require administrator privileges. To test Plasma/KRunner integration, **install** the desktop entry, D-Bus service and runner plugin rather than relying only on the build-tree executable.

The repository also offers a development helper at `.jules/run.sh` for the documented project environment; see [AGENTS.md](AGENTS.md) before using it. Developers can run equivalent CMake/CTest commands through that helper.

### CLI and desktop activation

```sh
klookup                       # Show dashboard
klookup "Linux kernel"        # Search a phrase
klookup --new-window "Qt"     # Open a separate search window
klookup --show-search         # Show and focus the search field
klookup --search-clipboard    # Explicit clipboard lookup
klookup --background          # Start without raising the main window
```

For a build-tree run, use `./build/src/klookup` in place of `klookup`.

The installed runner uses a D-Bus-activated single-instance application. To exercise the service explicitly from an installed Plasma session:

```sh
qdbus6 org.kde.klookup / org.kde.klookup.performSearch "cold start"
```

KRunner activation and shortcut delivery must also be checked in a real desktop session; headless tests cannot fully validate compositor behaviour.

### Provider/plugin model

KLookup supports **both native C++ Qt plugins and scripted ECMAScript packages** using **Provider API v2**. Native providers are loaded from KDE plugin directories. Scripted packages consist of a `provider.json` manifest (`"apiVersion": 2`) and CommonJS-style JavaScript entry/helper files (`module.exports` + `host.require()`), discovered via XDG user/system data paths: `${XDG_DATA_HOME:-~/.local/share}/klookup/providers.d` and `${XDG_DATA_DIRS:-/usr/local/share:/usr/share}/klookup/providers.d`. ES module `import` / `export` syntax is unsupported.

Scripts run in memory inside a resource-bounded `QJSEngine` (`module.exports = { search, getPage, getDetails, getContent, selfTest }` with `host.apiVersion === 2`, `host.fetch()`, `host.getSecret()`, `host.require()`, `host.log()`). Host services enforce declared origin permissions, response bounds, deadlines, and CPU execution budgets. Package approval binds a SHA-256 fingerprint over every package file, canonical path, provenance, and permissions; changing code or permissions requires fresh review.

Build-tree runs do not silently load packages from the source checkout or current directory. Install the package assets under a normal XDG/install prefix, or test against them using `klookup-provider-check`.

Scripted KWallet credential provisioning remains incomplete ([#85](https://github.com/arran4/klookup/issues/85)); shipped script packages require no credentials. `QJSEngine` is **not an OS security sandbox**—only approve packages whose source you trust.

Relevant developer documentation (also published via MkDocs in [`docs/`](docs/index.md)):

- [Shipped scripts & Provider API v2 example](docs/script-providers.md)
- [Package installation & approval](docs/provider-package-approval.md), [classification](docs/provider-classification.md), and [continuation contract](docs/provider-continuation.md)
- [Operation lifecycle](docs/script-provider-lifecycle.md), [HTTP transport](docs/provider-http-transport.md), [authentication](docs/provider-authentication.md), and [diagnostics](docs/provider-diagnostics.md)
- [Structured metadata](docs/structured-metadata.md), [source selection](docs/source-selection.md), [clipboard privacy](docs/clipboard-policy.md), and [global shortcuts](docs/global-shortcuts.md)
- [Canonical release workflow](docs/releasing.md) and [Spotify setup](docs/spotify-provider.md)

### Diagnostics and verification

**Settings → Provider health** offers individual retry, Check all providers and cancellation. The dedicated tool emits versioned JSON:

```sh
klookup-provider-check --all                     # Passive preflight: no provider execution, HTTP or wallet
klookup-provider-check --provider cheapshark --offline  # Explicit approved fixture check, network disabled
```

Optional checks that a provider does not implement report **unsupported**. `--live` explicitly permits only provider-exported live probes; no live probe runs at startup. See [diagnostics modes and limits](docs/provider-diagnostics.md).

Turn on plugin-discovery diagnostics to inspect where modules are loaded from without exposing credentials:

```sh
QT_LOGGING_RULES="klookup.plugins.debug=true" ./build/src/klookup
```

For runner failures in an installed desktop session, enable `QT_LOGGING_RULES="klookup.runner.warning=true"` for the runner host and inspect the D-Bus error. KLookup's custom D-Bus search service is `org.kde.klookup` with object path `/` and method `performSearch`. The `.desktop` launcher intentionally uses `Exec=klookup` rather than the unimplemented `org.freedesktop.Application` activation object at `/org/kde/klookup`; KDBusService still forwards repeated application starts to the existing instance.

Use `ctest --test-dir build --output-on-failure` for automated tests and complement it with live Plasma verification for shortcut conflicts, Wayland selection/activation, wallet prompting, tray interactions, drag-and-drop and UI positioning. Source plugin APIs and provider outputs should be verified against fixtures rather than live API assumptions alone.

### Contributing

Bug reports, documentation improvements and provider contributions are welcome. Check [open issues](https://github.com/arran4/klookup/issues) and [pull requests](https://github.com/arran4/klookup/pulls) first to avoid duplicate work. Keep provider logic isolated, preserve user-facing privacy defaults, add deterministic tests, and prefer links to permanent documentation over reproducing large internal implementation specifications.

See [AGENTS.md](AGENTS.md) for current repository development conventions. Public-release and contributing-policy work is also tracked in [issue #73](https://github.com/arran4/klookup/issues/73).

### License and attribution

KLookup's application, providers, runner and tests use **GPL-3.0-or-later**. CMake/CI files use **BSD-2-Clause**; project documentation uses **CC-BY-SA-4.0**; original icon assets use **CC0-1.0**. See [LICENSE](LICENSE), [LICENSES/](LICENSES/) and [REUSE.toml](REUSE.toml) for the machine-readable licensing declarations and full texts.

Data obtained from Wikipedia, MangaBaka, MangaDex, Spotify and other upstream providers is subject to those services' own licences and API terms; KLookup's code licence does not relicense their data. In particular, see [MangaBaka's API documentation](https://mangabaka.org/data/api) for its data/attribution requirements.
